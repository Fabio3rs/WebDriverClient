// tests/bidi_types_browsing_context_test.cpp - Unit tests for browsing
// context types
#include "bidi/types/browsing_context.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>
#include <variant>

using namespace bidi::types::browsing_context;

// ==================== UserPromptType Tests ====================

TEST(BidiTypesBrowsingContext, UserPromptTypeToString) {
    using enum UserPromptType;

    EXPECT_EQ(to_string(Alert), "alert");
    EXPECT_EQ(to_string(BeforeUnload), "beforeUnload");
    EXPECT_EQ(to_string(Confirm), "confirm");
    EXPECT_EQ(to_string(Prompt), "prompt");
}

TEST(BidiTypesBrowsingContext, UserPromptTypeConstexpr) {
    // Verify constexpr evaluation
    constexpr auto str = to_string(UserPromptType::Alert);
    static_assert(str == "alert");
}

TEST(BidiTypesBrowsingContext, UserPromptTypeBoostJson) {
    using enum UserPromptType;

    // Serialization
    auto jv1 = boost::json::value_from(Alert);
    EXPECT_EQ(jv1.as_string(), "alert");

    auto jv2 = boost::json::value_from(BeforeUnload);
    EXPECT_EQ(jv2.as_string(), "beforeUnload");

    auto jv3 = boost::json::value_from(Confirm);
    EXPECT_EQ(jv3.as_string(), "confirm");

    auto jv4 = boost::json::value_from(Prompt);
    EXPECT_EQ(jv4.as_string(), "prompt");
}

// ==================== LocateMatchType Tests ====================

TEST(BidiTypesBrowsingContext, LocateMatchTypeToString) {
    using enum LocateMatchType;

    EXPECT_EQ(to_string(Full), "full");
    EXPECT_EQ(to_string(Partial), "partial");
}

TEST(BidiTypesBrowsingContext, LocateMatchTypeConstexpr) {
    constexpr auto str = to_string(LocateMatchType::Full);
    static_assert(str == "full");
}

TEST(BidiTypesBrowsingContext, LocateMatchTypeBoostJson) {
    using enum LocateMatchType;

    auto jv1 = boost::json::value_from(Full);
    EXPECT_EQ(jv1.as_string(), "full");

    auto jv2 = boost::json::value_from(Partial);
    EXPECT_EQ(jv2.as_string(), "partial");
}

// ==================== CreateType Tests ====================

TEST(BidiTypesBrowsingContext, CreateTypeToString) {
    using enum CreateType;

    EXPECT_EQ(to_string(Tab), "tab");
    EXPECT_EQ(to_string(Window), "window");
}

TEST(BidiTypesBrowsingContext, CreateTypeConstexpr) {
    constexpr auto str = to_string(CreateType::Tab);
    static_assert(str == "tab");
}

TEST(BidiTypesBrowsingContext, CreateTypeBoostJson) {
    using enum CreateType;

    auto jv1 = boost::json::value_from(Tab);
    EXPECT_EQ(jv1.as_string(), "tab");

    auto jv2 = boost::json::value_from(Window);
    EXPECT_EQ(jv2.as_string(), "window");
}

// ==================== ReadinessState Tests ====================

TEST(BidiTypesBrowsingContext, ReadinessStateToString) {
    using enum ReadinessState;

    EXPECT_EQ(to_string(None), "none");
    EXPECT_EQ(to_string(Interactive), "interactive");
    EXPECT_EQ(to_string(Complete), "complete");
}

TEST(BidiTypesBrowsingContext, ReadinessStateConstexpr) {
    constexpr auto str = to_string(ReadinessState::Complete);
    static_assert(str == "complete");
}

TEST(BidiTypesBrowsingContext, ReadinessStateBoostJson) {
    using enum ReadinessState;

    auto jv1 = boost::json::value_from(None);
    EXPECT_EQ(jv1.as_string(), "none");

    auto jv2 = boost::json::value_from(Interactive);
    EXPECT_EQ(jv2.as_string(), "interactive");

    auto jv3 = boost::json::value_from(Complete);
    EXPECT_EQ(jv3.as_string(), "complete");
}

// ==================== Locator Struct Tests ====================

TEST(BidiTypesBrowsingContext, AccessibilityLocatorEquality) {
    AccessibilityLocator loc1;
    loc1.name = "Submit";
    loc1.role = "button";

    AccessibilityLocator loc2;
    loc2.name = "Submit";
    loc2.role = "button";

    AccessibilityLocator loc3;
    loc3.name = "Cancel";

    EXPECT_EQ(loc1, loc2);
    EXPECT_NE(loc1, loc3);
}

TEST(BidiTypesBrowsingContext, AccessibilityLocatorOptionalFields) {
    AccessibilityLocator loc;

    // Optional fields empty by default
    EXPECT_FALSE(loc.name.has_value());
    EXPECT_FALSE(loc.role.has_value());

    // Set optional fields
    loc.name = "Login";
    loc.role = "button";

    EXPECT_TRUE(loc.name.has_value());
    EXPECT_EQ(*loc.name, "Login");
    EXPECT_TRUE(loc.role.has_value());
    EXPECT_EQ(*loc.role, "button");
}

TEST(BidiTypesBrowsingContext, CssLocatorEquality) {
    CssLocator loc1{"#submit-btn"};
    CssLocator loc2{"#submit-btn"};
    CssLocator loc3{".button"};

    EXPECT_EQ(loc1, loc2);
    EXPECT_NE(loc1, loc3);
}

TEST(BidiTypesBrowsingContext, InnerTextLocatorEquality) {
    InnerTextLocator loc1;
    loc1.value = "Click here";
    loc1.ignore_case = true;
    loc1.match_type = LocateMatchType::Partial;

    InnerTextLocator loc2;
    loc2.value = "Click here";
    loc2.ignore_case = true;
    loc2.match_type = LocateMatchType::Partial;

    InnerTextLocator loc3;
    loc3.value = "Different text";

    EXPECT_EQ(loc1, loc2);
    EXPECT_NE(loc1, loc3);
}

TEST(BidiTypesBrowsingContext, InnerTextLocatorOptionalFields) {
    InnerTextLocator loc;
    loc.value = "Search";

    // Optional fields empty by default
    EXPECT_FALSE(loc.ignore_case.has_value());
    EXPECT_FALSE(loc.match_type.has_value());
    EXPECT_FALSE(loc.max_depth.has_value());

    // Set optional fields
    loc.ignore_case = true;
    loc.match_type = LocateMatchType::Full;
    loc.max_depth = 10;

    EXPECT_TRUE(loc.ignore_case.has_value());
    EXPECT_EQ(*loc.ignore_case, true);
    EXPECT_TRUE(loc.match_type.has_value());
    EXPECT_EQ(*loc.match_type, LocateMatchType::Full);
    EXPECT_TRUE(loc.max_depth.has_value());
    EXPECT_EQ(*loc.max_depth, 10U);
}

TEST(BidiTypesBrowsingContext, XPathLocatorEquality) {
    XPathLocator loc1{"//button[@type='submit']"};
    XPathLocator loc2{"//button[@type='submit']"};
    XPathLocator loc3{"//div[@id='content']"};

    EXPECT_EQ(loc1, loc2);
    EXPECT_NE(loc1, loc3);
}

TEST(BidiTypesBrowsingContext, ContextLocatorEquality) {
    ContextLocator loc1{"ctx-123"};
    ContextLocator loc2{"ctx-123"};
    ContextLocator loc3{"ctx-456"};

    EXPECT_EQ(loc1, loc2);
    EXPECT_NE(loc1, loc3);
}

// ==================== Locator Variant Tests ====================

TEST(BidiTypesBrowsingContext, LocatorVariantConstruction) {
    // Test construction with each locator type
    AccessibilityLocator acc_loc;
    acc_loc.name = "Submit";
    acc_loc.role = "button";
    Locator loc1 = acc_loc;
    EXPECT_TRUE(std::holds_alternative<AccessibilityLocator>(loc1));

    Locator loc2 = CssLocator{"#login"};
    EXPECT_TRUE(std::holds_alternative<CssLocator>(loc2));

    InnerTextLocator inner_loc;
    inner_loc.value = "Click";
    Locator loc3 = inner_loc;
    EXPECT_TRUE(std::holds_alternative<InnerTextLocator>(loc3));

    Locator loc4 = XPathLocator{"//div"};
    EXPECT_TRUE(std::holds_alternative<XPathLocator>(loc4));

    Locator loc5 = ContextLocator{"ctx-1"};
    EXPECT_TRUE(std::holds_alternative<ContextLocator>(loc5));
}

TEST(BidiTypesBrowsingContext, LocatorVariantVisitor) {
    // Test std::visit pattern
    auto get_description = [](const Locator &loc) -> std::string {
        return std::visit(
            [](const auto &l) -> std::string {
                using T = std::decay_t<decltype(l)>;
                if constexpr (std::is_same_v<T, AccessibilityLocator>) {
                    return "accessibility";
                } else if constexpr (std::is_same_v<T, CssLocator>) {
                    return "css";
                } else if constexpr (std::is_same_v<T, InnerTextLocator>) {
                    return "innerText";
                } else if constexpr (std::is_same_v<T, XPathLocator>) {
                    return "xpath";
                } else if constexpr (std::is_same_v<T, ContextLocator>) {
                    return "context";
                }
                return "unknown";
            },
            loc);
    };

    EXPECT_EQ(get_description(AccessibilityLocator{}), "accessibility");
    EXPECT_EQ(get_description(CssLocator{"#id"}), "css");

    InnerTextLocator inner;
    inner.value = "text";
    EXPECT_EQ(get_description(inner), "innerText");

    EXPECT_EQ(get_description(XPathLocator{"//div"}), "xpath");
    EXPECT_EQ(get_description(ContextLocator{"ctx"}), "context");
}

TEST(BidiTypesBrowsingContext, LocatorVariantAccess) {
    // Test std::get and std::get_if
    Locator loc = CssLocator{"#submit"};

    // std::get (throws if wrong type)
    const auto &css = std::get<CssLocator>(loc);
    EXPECT_EQ(css.value, "#submit");

    // std::get_if (returns nullptr if wrong type)
    auto *css_ptr = std::get_if<CssLocator>(&loc);
    ASSERT_NE(css_ptr, nullptr);
    EXPECT_EQ(css_ptr->value, "#submit");

    auto *xpath_ptr = std::get_if<XPathLocator>(&loc);
    EXPECT_EQ(xpath_ptr, nullptr);
}

// ==================== NavigationInfo Tests ====================

TEST(BidiTypesBrowsingContext, NavigationInfoEquality) {
    NavigationInfo nav1;
    nav1.context = "ctx-1";
    nav1.navigation = "nav-123";
    nav1.timestamp_ms = 1234567890;
    nav1.url = "https://example.com";

    NavigationInfo nav2;
    nav2.context = "ctx-1";
    nav2.navigation = "nav-123";
    nav2.timestamp_ms = 1234567890;
    nav2.url = "https://example.com";

    NavigationInfo nav3;
    nav3.context = "ctx-2";
    nav3.url = "https://different.com";

    EXPECT_EQ(nav1, nav2);
    EXPECT_NE(nav1, nav3);
}

TEST(BidiTypesBrowsingContext, NavigationInfoOptionalFields) {
    NavigationInfo nav;
    nav.context = "ctx-1";
    nav.url = "https://example.com";

    // navigation is optional
    EXPECT_FALSE(nav.navigation.has_value());

    nav.navigation = "nav-456";
    EXPECT_TRUE(nav.navigation.has_value());
    EXPECT_EQ(*nav.navigation, "nav-456");
}

TEST(BidiTypesBrowsingContext, NavigationInfoDefaultValues) {
    NavigationInfo nav;

    // Test default values
    EXPECT_TRUE(nav.context.empty());
    EXPECT_FALSE(nav.navigation.has_value());
    EXPECT_EQ(nav.timestamp_ms, 0U);
    EXPECT_TRUE(nav.url.empty());
}

// ==================== ClipRectangle Tests ====================

TEST(BidiTypesBrowsingContext, ElementClipRectangleEquality) {
    ElementClipRectangle clip1{"ref-123"};
    ElementClipRectangle clip2{"ref-123"};
    ElementClipRectangle clip3{"ref-456"};

    EXPECT_EQ(clip1, clip2);
    EXPECT_NE(clip1, clip3);
}

TEST(BidiTypesBrowsingContext, BoxClipRectangleEquality) {
    BoxClipRectangle box1{.x=10.0, .y=20.0, .width=100.0, .height=200.0};
    BoxClipRectangle box2{.x=10.0, .y=20.0, .width=100.0, .height=200.0};
    BoxClipRectangle box3{.x=0.0, .y=0.0, .width=50.0, .height=50.0};

    EXPECT_EQ(box1, box2);
    EXPECT_NE(box1, box3);
}

TEST(BidiTypesBrowsingContext, BoxClipRectangleDefaultValues) {
    BoxClipRectangle box;

    EXPECT_DOUBLE_EQ(box.x, 0.0);
    EXPECT_DOUBLE_EQ(box.y, 0.0);
    EXPECT_DOUBLE_EQ(box.width, 0.0);
    EXPECT_DOUBLE_EQ(box.height, 0.0);
}

TEST(BidiTypesBrowsingContext, ClipRectangleVariantConstruction) {
    // Test construction with each clip type
    ClipRectangle clip1 = ElementClipRectangle{"ref-123"};
    EXPECT_TRUE(std::holds_alternative<ElementClipRectangle>(clip1));

    ClipRectangle clip2 = BoxClipRectangle{.x=10.0, .y=20.0, .width=100.0, .height=200.0};
    EXPECT_TRUE(std::holds_alternative<BoxClipRectangle>(clip2));
}

TEST(BidiTypesBrowsingContext, ClipRectangleVariantVisitor) {
    auto get_type = [](const ClipRectangle &clip) -> std::string {
        return std::visit(
            [](const auto &c) -> std::string {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, ElementClipRectangle>) {
                    return "element";
                } else if constexpr (std::is_same_v<T, BoxClipRectangle>) {
                    return "box";
                }
                return "unknown";
            },
            clip);
    };

    EXPECT_EQ(get_type(ElementClipRectangle{"ref"}), "element");
    EXPECT_EQ(get_type(BoxClipRectangle{0, 0, 100, 100}), "box");
}

TEST(BidiTypesBrowsingContext, ClipRectangleVariantAccess) {
    ClipRectangle clip = BoxClipRectangle{.x=10.0, .y=20.0, .width=100.0, .height=200.0};

    // std::get (throws if wrong type)
    const auto &box = std::get<BoxClipRectangle>(clip);
    EXPECT_DOUBLE_EQ(box.x, 10.0);
    EXPECT_DOUBLE_EQ(box.width, 100.0);

    // std::get_if (returns nullptr if wrong type)
    auto *box_ptr = std::get_if<BoxClipRectangle>(&clip);
    ASSERT_NE(box_ptr, nullptr);
    EXPECT_DOUBLE_EQ(box_ptr->height, 200.0);

    auto *elem_ptr = std::get_if<ElementClipRectangle>(&clip);
    EXPECT_EQ(elem_ptr, nullptr);
}

// ==================== ImageFormat Tests ====================

TEST(BidiTypesBrowsingContext, ImageFormatEquality) {
    ImageFormat fmt1;
    fmt1.type = "png";

    ImageFormat fmt2;
    fmt2.type = "png";

    ImageFormat fmt3;
    fmt3.type = "jpeg";
    fmt3.quality = 0.9;

    EXPECT_EQ(fmt1, fmt2);
    EXPECT_NE(fmt1, fmt3);
}

TEST(BidiTypesBrowsingContext, ImageFormatOptionalQuality) {
    ImageFormat fmt;
    fmt.type = "png";

    // quality is optional
    EXPECT_FALSE(fmt.quality.has_value());

    fmt.quality = 0.85;
    EXPECT_TRUE(fmt.quality.has_value());
    EXPECT_DOUBLE_EQ(*fmt.quality, 0.85);
}

TEST(BidiTypesBrowsingContext, ImageFormatDefaultValues) {
    ImageFormat fmt;

    EXPECT_TRUE(fmt.type.empty());
    EXPECT_FALSE(fmt.quality.has_value());
}

// ==================== Type Safety Tests ====================

TEST(BidiTypesBrowsingContext, StructDefaultConstruction) {
    // Verify all structs are default-constructible
    AccessibilityLocator acc_loc;
    EXPECT_FALSE(acc_loc.name.has_value());
    EXPECT_FALSE(acc_loc.role.has_value());

    CssLocator css_loc;
    EXPECT_TRUE(css_loc.value.empty());

    InnerTextLocator inner_loc;
    EXPECT_TRUE(inner_loc.value.empty());
    EXPECT_FALSE(inner_loc.ignore_case.has_value());

    XPathLocator xpath_loc;
    EXPECT_TRUE(xpath_loc.value.empty());

    ContextLocator ctx_loc;
    EXPECT_TRUE(ctx_loc.context.empty());

    NavigationInfo nav;
    EXPECT_EQ(nav.timestamp_ms, 0U);

    ElementClipRectangle elem_clip;
    EXPECT_TRUE(elem_clip.shared_reference.empty());

    BoxClipRectangle box_clip;
    EXPECT_DOUBLE_EQ(box_clip.x, 0.0);

    ImageFormat fmt;
    EXPECT_TRUE(fmt.type.empty());
}

TEST(BidiTypesBrowsingContext, VariantIndexTest) {
    // Verify variant indices are correct
    Locator loc1 = AccessibilityLocator{};
    EXPECT_EQ(loc1.index(), 0U);

    Locator loc2 = CssLocator{""};
    EXPECT_EQ(loc2.index(), 1U);

    InnerTextLocator inner_text_empty;
    inner_text_empty.value = "";
    Locator loc3 = inner_text_empty;
    EXPECT_EQ(loc3.index(), 2U);

    Locator loc4 = XPathLocator{""};
    EXPECT_EQ(loc4.index(), 3U);

    Locator loc5 = ContextLocator{""};
    EXPECT_EQ(loc5.index(), 4U);

    ClipRectangle clip1 = ElementClipRectangle{""};
    EXPECT_EQ(clip1.index(), 0U);

    ClipRectangle clip2 = BoxClipRectangle{};
    EXPECT_EQ(clip2.index(), 1U);
}
