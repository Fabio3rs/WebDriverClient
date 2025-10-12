#pragma once
/**
 * @file types/storage.hpp
 * @brief W3C WebDriver BiDi storage module types
 *
 * Types for storage and cookie management.
 * Implements types from W3C BiDi spec storage module.
 *
 * @see https://w3c.github.io/webdriver-bidi/#module-storage
 */

#include "bidi/types/network.hpp" // Reuse Cookie
#include <optional>
#include <string>

namespace bidi::types::storage {

/**
 * @brief Storage module reuses Cookie from network module
 */
using Cookie = network::Cookie;

/**
 * @brief Storage partition key (opaque string)
 */
using PartitionKey = std::string;

/**
 * @brief Storage partition descriptor
 */
struct PartitionDescriptor {
    std::optional<std::string> user_context;
    std::optional<std::string> source_origin;

    auto operator==(const PartitionDescriptor &) const -> bool = default;
};

} // namespace bidi::types::storage
