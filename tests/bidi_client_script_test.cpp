// tests/bidi_client_script_test.cpp - Integration tests (refactor) for
// script evaluation with dedicated io_context thread (no run()/restart loop)

#include "WebDriverClient.hpp"
#include "bidi/client.hpp"
#include "bidi/commands.hpp"
#include "bidi/script/function_wrapper.hpp"
#include "bidi/script_eval.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <optional>
#include <thread>

using namespace std::chrono_literals;
using namespace bidi::script;

class BiDiClientScriptTest : public ::testing::Test {
  protected:
    boost::asio::io_context io_context_;
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_guard_;
    std::jthread io_thread_;

    WebDriver legacy_driver_;
    std::shared_ptr<bidi::Client> bidi_client_;
    std::string session_id_;
    std::string websocket_url_;
    std::string context_id_;

    void SetUp() override {
        legacy_driver_.webDriverUrl = "http://localhost:9515";
        WebDriver::json args =
            WebDriver::json::array({"--headless", "--no-sandbox"});
        auto session_response = legacy_driver_.connect(args, "chrome", true);
        session_id_ = legacy_driver_.sessionId;

        ASSERT_TRUE(session_response.contains("capabilities"));
        ASSERT_TRUE(session_response["capabilities"].contains("webSocketUrl"));
        websocket_url_ =
            session_response["capabilities"]["webSocketUrl"].get<std::string>();
        ASSERT_FALSE(websocket_url_.empty());

        work_guard_.emplace(io_context_.get_executor());
        io_thread_ = std::jthread([this] { io_context_.run(); });

        auto fut = boost::asio::co_spawn(
            io_context_,
            [this]() -> boost::asio::awaitable<void> {
                auto client_ptr =
                    co_await bidi::Client::connect(io_context_, websocket_url_);
                if (!client_ptr) {
                    throw std::runtime_error(
                        "Client::connect returned nullptr");
                }
                bidi_client_ = client_ptr;
                context_id_ = co_await bidi_client_->create_context(
                    bidi::commands::browsing_context::CreateType::window);
                if (context_id_.empty()) {
                    throw std::runtime_error(
                        "create_context returned empty context_id");
                }
                co_return;
            },
            boost::asio::use_future);

        if (fut.wait_for(15s) != std::future_status::ready) {
            FAIL() << "Timeout during BiDi client setup";
        }
        // propagate exceptions
        fut.get();
    }

    void TearDown() override {
        try {
            if (bidi_client_ && bidi_client_->session()) {
                bidi_client_->session()->disconnect();
            }
            bidi_client_.reset();
            if (work_guard_) {
                work_guard_->reset();
            }
            io_context_.stop();
        } catch (const std::exception &e) {
            std::cerr << "TearDown non-fatal: " << e.what() << '\n';
        }
    }

    template <class T>
    auto run_task(const bidi::Task<T> &task,
                  std::chrono::milliseconds timeout = 10s) -> T {
        auto prom_ptr = std::make_shared<std::promise<T>>();
        auto fut = prom_ptr->get_future();
        task.finally([prom_ptr](std::optional<T> value_opt,
                                std::optional<boost::system::error_code> ec_opt,
                                const std::exception_ptr &eptr) mutable {
            try {
                if (value_opt) {
                    prom_ptr->set_value(std::move(*value_opt));
                    return;
                }
                if (eptr) {
                    prom_ptr->set_exception(eptr);
                    return;
                }
                if (ec_opt) {
                    prom_ptr->set_exception(std::make_exception_ptr(
                        boost::system::system_error(*ec_opt)));
                    return;
                }
                prom_ptr->set_exception(std::make_exception_ptr(
                    std::runtime_error("unknown task completion state")));
            } catch (...) {
                // estado já satisfeito ou erro ignorável
            }
        });
        if (fut.wait_for(timeout) != std::future_status::ready) {
            throw std::runtime_error("run_task timeout");
        }
        return fut.get();
    }
};

TEST_F(BiDiClientScriptTest, EvaluateWithReturnOutcomePolicy) {
    auto outcome = run_task(
        bidi_client_->evaluate("throw new TypeError('test error')", context_id_,
                               script_eval_policy::return_outcome, false));
    ASSERT_TRUE(outcome.has_exception());
    ASSERT_TRUE(outcome.exception.has_value());
    // Chrome nem sempre preenche className; aceitar vazio ou TypeError
    EXPECT_TRUE(outcome.exception->name.empty() ||
                outcome.exception->name == "TypeError")
        << "unexpected exception name: '" << outcome.exception->name << "'";
    // Garantir que o texto contenha o prefixo esperado
    EXPECT_NE(outcome.exception->text.find("TypeError"), std::string::npos)
        << "exception text does not contain TypeError: "
        << outcome.exception->text;
    EXPECT_EQ(outcome.exception->exception_type, "exception");
}

TEST_F(BiDiClientScriptTest, EvaluateThrowsScriptEvaluateException) {
    EXPECT_THROW((void)run_task(bidi_client_->evaluate(
                     "throw new ReferenceError('ref err')", context_id_,
                     script_eval_policy::throw_on_script_exception, false)),
                 ScriptEvaluateException);
}

TEST_F(BiDiClientScriptTest, CallFunctionExceptionHandling) {
    const char *throwing_fn = R"js(
        function throwError() { throw new Error('function threw error'); }
    )js";
    run_task(bidi_client_->evaluate(throwing_fn, context_id_));
    boost::json::array empty_args;
    auto result = run_task(bidi_client_->call_function(
        "throwError", context_id_, empty_args, false));
    ASSERT_TRUE(result.contains("type"));
    EXPECT_EQ(result.at("type").as_string(), "exception");
}

TEST_F(BiDiClientScriptTest, CallFunctionWithWrapper) {
    run_task(bidi_client_->evaluate(R"js(
        function add(a, b) { return a + b; }
    )js",
                                    context_id_));

    auto fun = bidi::script::make_function_caller<int, int, int>(
        bidi_client_, context_id_, "add");
    auto result = run_task(fun(2, 3));
    ASSERT_EQ(result, 5);
}

TEST_F(BiDiClientScriptTest, CallFunctionWithWrapperThrowing) {
    run_task(bidi_client_->evaluate(R"js(
        function willThrow() { throw new Error('function error'); }
    )js",
                                    context_id_));

    auto fun = bidi::script::make_function_caller<int>(
        bidi_client_, context_id_, "willThrow");
    EXPECT_THROW((void)run_task(fun()), ScriptEvaluateException);
}

TEST_F(BiDiClientScriptTest, ThrowPolicyExceptionWithoutDetails) {
    EXPECT_THROW((void)run_task(bidi_client_->evaluate(
                     "throw null", context_id_,
                     script_eval_policy::throw_on_script_exception, false)),
                 ScriptEvaluateException);
}

TEST_F(BiDiClientScriptTest, AsyncExceptionPropagation) {
    auto chain =
        bidi_client_
            ->evaluate("1+1", context_id_) // original basic
            .and_then([this](const auto &) {
                return bidi_client_->evaluate(
                    "throw new Error('chain error')", context_id_,
                    script_eval_policy::throw_on_script_exception, false);
            })
            .map([](const ScriptEvalOutcome &outcome) {
                return outcome.result; // não alcançado em erro
            });
    EXPECT_THROW((void)run_task(std::move(chain)), ScriptEvaluateException);
}

TEST_F(BiDiClientScriptTest, LargeLineNumberHandled) {
    auto outcome = run_task(bidi_client_->evaluate(
        R"js(
            function test(){ throw new Error('line number test'); }
            test();
        )js",
        context_id_, script_eval_policy::return_outcome, false));
    ASSERT_TRUE(outcome.has_exception());
    if (outcome.exception->line_number) {
        EXPECT_GT(*outcome.exception->line_number, 0);
    }
}

TEST_F(BiDiClientScriptTest, EmptyExceptionDetailsObject) {
    auto outcome = run_task(
        bidi_client_->evaluate("throw 'plain string error'", context_id_,
                               script_eval_policy::return_outcome, false));
    ASSERT_TRUE(outcome.has_exception());
    ASSERT_TRUE(outcome.exception.has_value());
    EXPECT_EQ(outcome.exception->exception_type, "exception");
}
