#include "bidi/commands.hpp"
#include "bidi/ids.hpp"
#include "bidi_methods.hpp"
#include <gtest/gtest.h>

using namespace bidi;
using namespace bidi::commands;

TEST(BidiBuilders, SessionSubscribeUnsubscribeStatus) {
    std::vector<std::string> events = {
        std::string(bidi::ids::events::log_entryAdded),
        std::string(bidi::ids::events::net_beforeRequestSent)};
    std::vector<std::string> ctxs = {"CTX1", "CTX2"};
    auto sub = session::subscribe(events, ctxs);
    ASSERT_TRUE(sub.if_contains("events"));
    ASSERT_TRUE(sub["events"].is_array());
    EXPECT_EQ(sub["events"].as_array().size(), 2U);
    ASSERT_TRUE(sub.if_contains("contexts"));
    EXPECT_EQ(sub["contexts"].as_array().size(), 2U);

    auto unsub = session::unsubscribe(events, {});
    ASSERT_TRUE(unsub.if_contains("events"));
    EXPECT_FALSE(unsub.if_contains("contexts"));

    auto st = session::status();
    EXPECT_TRUE(st.empty());
}

TEST(BidiBuilders, BrowsingContextCommands) {
    auto create_tab =
        browsing_context::create(browsing_context::CreateType::tab);
    EXPECT_EQ(create_tab["type"].as_string(), "tab");
    auto create_win =
        browsing_context::create(browsing_context::CreateType::window, "REF");
    EXPECT_EQ(create_win["type"].as_string(), "window");
    EXPECT_EQ(create_win["referenceContext"].as_string(), "REF");

    auto nav = browsing_context::navigate(
        "CTX", "https://x", browsing_context::ReadinessState::interactive);
    EXPECT_EQ(nav["context"].as_string(), "CTX");
    EXPECT_EQ(nav["url"].as_string(), "https://x");
    EXPECT_EQ(nav["wait"].as_string(), "interactive");

    auto close = browsing_context::close("CTX");
    EXPECT_EQ(close["context"].as_string(), "CTX");

    auto tree = browsing_context::get_tree("ROOT", 3);
    EXPECT_EQ(tree["root"].as_string(), "ROOT");
    EXPECT_EQ(tree["maxDepth"].as_int64(), 3);

    auto reload_params = browsing_context::reload(
        "CTX", true, browsing_context::ReadinessState::none);
    EXPECT_EQ(reload_params["context"].as_string(), "CTX");
    EXPECT_TRUE(reload_params["ignoreCache"].as_bool());
    EXPECT_EQ(reload_params["wait"].as_string(), "none");

    // Test handle_user_prompt with all parameters
    auto prompt_full =
        browsing_context::handle_user_prompt("CTX", true, "test input");
    EXPECT_EQ(prompt_full["context"].as_string(), "CTX");
    EXPECT_TRUE(prompt_full["accept"].as_bool());
    EXPECT_EQ(prompt_full["userText"].as_string(), "test input");

    // Test handle_user_prompt with minimal parameters (only context)
    auto prompt_minimal = browsing_context::handle_user_prompt("CTX");
    EXPECT_EQ(prompt_minimal["context"].as_string(), "CTX");
    EXPECT_FALSE(prompt_minimal.if_contains("accept"));
    EXPECT_FALSE(prompt_minimal.if_contains("userText"));

    // Test handle_user_prompt with accept=false
    auto prompt_dismiss = browsing_context::handle_user_prompt("CTX", false);
    EXPECT_EQ(prompt_dismiss["context"].as_string(), "CTX");
    EXPECT_FALSE(prompt_dismiss["accept"].as_bool());
    EXPECT_FALSE(prompt_dismiss.if_contains("userText"));
}

TEST(BidiBuilders, ScriptCommands) {
    script::Target target{.context = "CTX", .sandbox = "SB"};
    auto eval_params =
        script::evaluate("1+1", target, true, script::ResultOwnership::root);
    EXPECT_EQ(eval_params["expression"].as_string(), "1+1");
    EXPECT_TRUE(eval_params["awaitPromise"].as_bool());
    EXPECT_EQ(eval_params["resultOwnership"].as_string(), "root");
    auto &tobj = eval_params["target"].as_object();
    EXPECT_EQ(tobj["context"].as_string(), "CTX");
    EXPECT_EQ(tobj["sandbox"].as_string(), "SB");

    boost::json::array args;
    args.emplace_back(commands::local_value_string("hello"));
    auto call_params = script::call_function("() => 42", target, args, false,
                                             script::ResultOwnership::none);
    EXPECT_FALSE(call_params["awaitPromise"].as_bool());
    EXPECT_EQ(call_params["resultOwnership"].as_string(), "none");
    ASSERT_TRUE(call_params.if_contains("arguments"));
    EXPECT_EQ(call_params["arguments"].as_array().size(), 1U);

    boost::json::array handles;
    handles.emplace_back("h1");
    auto disown_params = script::disown(handles, target);
    EXPECT_EQ(disown_params["handles"].as_array().size(), 1U);
}

TEST(BidiBuilders, NetworkCommands) {
    boost::json::object hdrs;
    hdrs["X"] = "Y";
    auto cont_req =
        network::continue_request("REQ1", "https://a", "POST", hdrs, "body");
    EXPECT_EQ(cont_req["request"].as_string(), "REQ1");
    EXPECT_EQ(cont_req["url"].as_string(), "https://a");
    EXPECT_EQ(cont_req["method"].as_string(), "POST");
    EXPECT_EQ(cont_req["headers"].as_object()["X"].as_string(), "Y");
    auto body_obj = cont_req["body"].as_object();
    EXPECT_EQ(body_obj["type"].as_string(), "string");
    EXPECT_EQ(body_obj["value"].as_string(), "body");

    auto fail_req = network::fail_request("REQ2");
    EXPECT_EQ(fail_req["request"].as_string(), "REQ2");
}

namespace BC = bidi::commands::browsing_context;

TEST(BidiBuilders, EnumAndLocalValues) {
    EXPECT_EQ(to_string(BC::CreateType::tab), "tab");
    EXPECT_EQ(to_string(BC::CreateType::window), "window");
    EXPECT_EQ(to_string(BC::ReadinessState::none), "none");
    EXPECT_EQ(to_string(BC::ReadinessState::interactive), "interactive");
    EXPECT_EQ(to_string(BC::ReadinessState::complete), "complete");

    EXPECT_EQ(to_string(script::ResultOwnership::root), "root");
    EXPECT_EQ(to_string(script::ResultOwnership::none), "none");

    EXPECT_EQ(to_string(log::Level::debug), "debug");
    EXPECT_EQ(to_string(log::Level::info), "info");
    EXPECT_EQ(to_string(log::Level::warn), "warn");
    EXPECT_EQ(to_string(log::Level::error), "error");

    auto lv_str = local_value_string("v");
    EXPECT_EQ(lv_str["type"].as_string(), "string");
    EXPECT_EQ(lv_str["value"].as_string(), "v");

    auto lv_num = local_value_number(3.14);
    EXPECT_EQ(lv_num["type"].as_string(), "number");
    EXPECT_TRUE(lv_num["value"].is_double());

    auto lv_bool = local_value_boolean(true);
    EXPECT_EQ(lv_bool["type"].as_string(), "boolean");
    EXPECT_TRUE(lv_bool["value"].as_bool());

    auto lv_null = local_value_null();
    EXPECT_EQ(lv_null["type"].as_string(), "null");
}
