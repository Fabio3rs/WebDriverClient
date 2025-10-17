#pragma once
/**
 * @file types/browsing_context.hpp
 * @brief W3C WebDriver BiDi browsingContext module types
 *
 * Types for browsing context management, navigation, locators, and screenshots.
 * Implements types from W3C BiDi spec browsingContext module.
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-browsingContext
 */

#include "bidi/types/core.hpp"
#include "bidi/types/session.hpp"
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace bidi::types::browsing_context {

// ==================== Identifiers ====================

/**
 * @brief Browsing context identifier (opaque string)
 *
 * Strong type for context IDs (not just std::string)
 */
using BrowsingContextId = std::string;

/**
 * @brief Navigation identifier (opaque string)
 */
using NavigationId = std::string;

// ==================== Enums ====================

/**
 * @brief User prompt type enumeration
 */
enum class UserPromptType : std::uint8_t {
    Alert,
    BeforeUnload,
    Confirm,
    Prompt
};

[[nodiscard]] constexpr auto to_string(UserPromptType type) noexcept
    -> std::string_view {
    using enum UserPromptType;
    switch (type) {
    case Alert:
        return "alert";
    case BeforeUnload:
        return "beforeUnload";
    case Confirm:
        return "confirm";
    case Prompt:
        return "prompt";
    default:
        return "alert";
    }
}

[[nodiscard]] constexpr auto
parse_user_prompt_type(std::string_view text) noexcept
    -> std::optional<UserPromptType> {
    using enum UserPromptType;
    if (text == "alert") {
        return Alert;
    }
    if (text == "beforeUnload") {
        return BeforeUnload;
    }
    if (text == "confirm") {
        return Confirm;
    }
    if (text == "prompt") {
        return Prompt;
    }
    return std::nullopt;
}

/**
 * @brief Locator match type
 */
enum class LocateMatchType : std::uint8_t { Full, Partial };

[[nodiscard]] constexpr auto to_string(LocateMatchType type) noexcept
    -> std::string_view {
    using enum LocateMatchType;
    switch (type) {
    case Full:
        return "full";
    case Partial:
        return "partial";
    default:
        return "full";
    }
}

/**
 * @brief Create type (migrated from commands, kept for compatibility)
 */
enum class CreateType : std::uint8_t { Tab, Window };

[[nodiscard]] constexpr auto to_string(CreateType type) noexcept
    -> std::string_view {
    using enum CreateType;
    switch (type) {
    case Tab:
        return "tab";
    case Window:
        return "window";
    default:
        return "tab";
    }
}

/**
 * @brief Readiness state (migrated from commands, kept for compatibility)
 */
enum class ReadinessState : std::uint8_t { None, Interactive, Complete };

[[nodiscard]] constexpr auto to_string(ReadinessState state) noexcept
    -> std::string_view {
    using enum ReadinessState;
    switch (state) {
    case None:
        return "none";
    case Interactive:
        return "interactive";
    case Complete:
        return "complete";
    default:
        return "complete";
    }
}

// ==================== Locator System ====================

/**
 * @brief Accessibility locator (ARIA)
 */
struct AccessibilityLocator {
    std::optional<std::string> name;
    std::optional<std::string> role;

    auto operator==(const AccessibilityLocator &) const -> bool = default;
};

/**
 * @brief CSS selector locator
 */
struct CssLocator {
    std::string value;

    auto operator==(const CssLocator &) const -> bool = default;
};

/**
 * @brief Inner text locator
 */
struct InnerTextLocator {
    std::string value;
    std::optional<bool> ignore_case;
    std::optional<LocateMatchType> match_type;
    std::optional<std::uint64_t> max_depth;

    auto operator==(const InnerTextLocator &) const -> bool = default;
};

/**
 * @brief XPath locator
 */
struct XPathLocator {
    std::string value;

    auto operator==(const XPathLocator &) const -> bool = default;
};

/**
 * @brief Context locator (element from context)
 */
struct ContextLocator {
    BrowsingContextId context;

    auto operator==(const ContextLocator &) const -> bool = default;
};

/**
 * @brief Locator variant (sum type)
 *
 * Use std::variant for type-safe locator dispatch.
 * Visitor pattern enables exhaustive handling.
 */
using Locator = std::variant<AccessibilityLocator, CssLocator, InnerTextLocator,
                             XPathLocator, ContextLocator>;

// ==================== Navigation ====================

/**
 * @brief Navigation information
 */
struct NavigationInfo {
    BrowsingContextId context;
    std::optional<NavigationId> navigation;
    std::uint64_t timestamp_ms{0};
    std::string url;

    auto operator==(const NavigationInfo &) const -> bool = default;
};

// ==================== User Prompt Handling ====================

/**
 * @brief User prompt opened event parameters
 * @see
 * https://w3c.github.io/webdriver-bidi/#event-browsingContext-userPromptOpened
 */
struct UserPromptOpenedParameters {
    BrowsingContextId context;
    types::session::UserPromptAction handler;
    std::string message;
    UserPromptType type;
    std::optional<std::string> default_value;

    auto operator==(const UserPromptOpenedParameters &) const -> bool = default;
};

/**
 * @brief User prompt closed event parameters
 * @see
 * https://w3c.github.io/webdriver-bidi/#event-browsingContext-userPromptClosed
 */
struct UserPromptClosedParameters {
    BrowsingContextId context;
    bool accepted{false};
    UserPromptType type;
    std::optional<std::string> user_text;

    auto operator==(const UserPromptClosedParameters &) const -> bool = default;
};

/**
 * @brief Resolution to send with browsingContext.handleUserPrompt
 */
struct UserPromptResolution {
    bool accept{true};
    std::optional<std::string> user_text;

    auto operator==(const UserPromptResolution &) const -> bool = default;
};

// ==================== Screenshot/Capture ====================

/**
 * @brief Element clip rectangle (reference-based)
 */
struct ElementClipRectangle {
    std::string shared_reference; // script::SharedReference id

    auto operator==(const ElementClipRectangle &) const -> bool = default;
};

/**
 * @brief Box clip rectangle (coordinate-based)
 */
struct BoxClipRectangle {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    auto operator==(const BoxClipRectangle &) const -> bool = default;
};

/**
 * @brief Clip rectangle variant
 */
using ClipRectangle = std::variant<ElementClipRectangle, BoxClipRectangle>;

/**
 * @brief Image format specification
 */
struct ImageFormat {
    std::string type;              // "png" | "jpeg"
    std::optional<double> quality; // 0.0-1.0, only for jpeg

    auto operator==(const ImageFormat &) const -> bool = default;
};

} // namespace bidi::types::browsing_context

// Convenience aliases at bidi:: level
namespace bidi {
using BrowsingContextId = types::browsing_context::BrowsingContextId;
using NavigationId = types::browsing_context::NavigationId;
} // namespace bidi

// ==================== Boost.JSON Integration ====================

namespace boost::json {

// LocateMatchType serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::browsing_context::LocateMatchType type) {
    jv = bidi::types::browsing_context::to_string(type);
}

// CreateType serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::browsing_context::CreateType type) {
    jv = bidi::types::browsing_context::to_string(type);
}

// ReadinessState serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::browsing_context::ReadinessState state) {
    jv = bidi::types::browsing_context::to_string(state);
}

// UserPromptType serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       bidi::types::browsing_context::UserPromptType type) {
    jv = bidi::types::browsing_context::to_string(type);
}

inline auto tag_invoke(
    value_to_tag<bidi::types::browsing_context::UserPromptType> /*unused*/,
    const value &jv) -> bidi::types::browsing_context::UserPromptType {
    auto text = value_to<std::string_view>(jv);
    auto type = bidi::types::browsing_context::parse_user_prompt_type(text);
    if (!type) {
        throw std::runtime_error("Invalid user prompt type");
    }
    return *type;
}

inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::browsing_context::ImageFormat &format) {
    object obj;
    obj["type"] = format.type;
    if (format.quality.has_value()) {
        obj["quality"] = *format.quality;
    }
    jv = std::move(obj);
}

namespace detail {
struct ClipRectangleSerializer {
    object &target;

    void operator()(
        const bidi::types::browsing_context::ElementClipRectangle &clip) {
        object element_obj;
        element_obj["sharedId"] = clip.shared_reference;
        target["type"] = "element";
        target["element"] = std::move(element_obj);
    }

    void
    operator()(const bidi::types::browsing_context::BoxClipRectangle &clip) {
        target["type"] = "box";
        target["x"] = clip.x;
        target["y"] = clip.y;
        target["width"] = clip.width;
        target["height"] = clip.height;
    }
};
} // namespace detail

inline void
tag_invoke(value_from_tag /*unused*/, value &jv,
           const bidi::types::browsing_context::ClipRectangle &clip) {
    object obj;
    std::visit(detail::ClipRectangleSerializer{obj}, clip);
    jv = std::move(obj);
}

// ==================== Locator Serialization ====================

namespace detail {
struct LocatorSerializer {
    object &target;

    void
    operator()(const bidi::types::browsing_context::AccessibilityLocator &loc) {
        object value_obj;
        if (loc.name.has_value()) {
            value_obj["name"] = *loc.name;
        }
        if (loc.role.has_value()) {
            value_obj["role"] = *loc.role;
        }
        target["type"] = "accessibility";
        target["value"] = std::move(value_obj);
    }

    void operator()(const bidi::types::browsing_context::CssLocator &loc) {
        target["type"] = "css";
        target["value"] = loc.value;
    }

    void
    operator()(const bidi::types::browsing_context::InnerTextLocator &loc) {
        object value_obj;
        value_obj["value"] = loc.value;
        if (loc.ignore_case.has_value()) {
            value_obj["ignoreCase"] = *loc.ignore_case;
        }
        if (loc.match_type.has_value()) {
            value_obj["matchType"] =
                bidi::types::browsing_context::to_string(*loc.match_type);
        }
        if (loc.max_depth.has_value()) {
            value_obj["maxDepth"] = *loc.max_depth;
        }
        target["type"] = "innerText";
        target["value"] = std::move(value_obj);
    }

    void operator()(const bidi::types::browsing_context::XPathLocator &loc) {
        target["type"] = "xpath";
        target["value"] = loc.value;
    }

    void operator()(const bidi::types::browsing_context::ContextLocator &loc) {
        target["type"] = "context";
        target["value"] = loc.context;
    }
};
} // namespace detail

// Locator variant serialization
inline void tag_invoke(value_from_tag /*unused*/, value &jv,
                       const bidi::types::browsing_context::Locator &locator) {
    object obj;
    std::visit(detail::LocatorSerializer{obj}, locator);
    jv = std::move(obj);
}

} // namespace boost::json
