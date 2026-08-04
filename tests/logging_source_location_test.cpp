#include <bidi/logging.hpp>
#include <boost/json.hpp>
#include <gtest/gtest.h>

using namespace bidi::logging;

TEST(LoggingSourceLocationTest, IncludesSourceByDefault) {
    // Configura sink para capturar saída em string
    std::string captured;
    set_log_sink([&](const std::string &s) { captured = s; });

    log_info("test-message");

    ASSERT_FALSE(captured.empty());
    auto obj = boost::json::parse(captured).as_object();
    // Campo source deve existir quando não stripado
    ASSERT_TRUE(obj.if_contains("source") != nullptr);
    auto &src = obj.at("source").as_object();
    ASSERT_TRUE(src.if_contains("file") != nullptr);
    ASSERT_TRUE(src.if_contains("line") != nullptr);
    ASSERT_TRUE(src.if_contains("function") != nullptr);

    // Restaura sink padrão
    reset_log_sink();
}

#ifdef WEBDRIVER_STRIP_LOG_LOCATION
TEST(LoggingSourceLocationTest, StripsSourceWhenConfigured) {
    std::string captured;
    set_log_sink([&](const std::string &s) { captured = s; });
    log_info("strip-check");
    auto obj = boost::json::parse(captured).as_object();
    // Quando strip ativo, 'source' não deve aparecer
    ASSERT_TRUE(obj.if_contains("source") == nullptr);
    reset_log_sink();
}
#endif
