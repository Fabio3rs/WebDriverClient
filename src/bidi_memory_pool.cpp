// src/bidi_memory_pool.cpp — Arena allocation implementation
#include "bidi/memory_pool.hpp"

namespace bidi::core {

JsonArenaPool::Arena::Arena(std::size_t size) : resource_{size} {}

auto JsonArenaPool::Arena::parse(std::string_view json_text)
    -> boost::json::value {
    return boost::json::parse(json_text, resource());
}

void JsonArenaPool::Arena::reset() noexcept { resource_.release(); }

} // namespace bidi::core
