#pragma once

// Core type aliases, smart-pointer helpers and assertion machinery shared by
// every Slate subsystem. Keep this header free of heavy dependencies: it is
// included almost everywhere.

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <functional>
#include <filesystem>
#include <utility>

namespace Slate {

template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T>
using Scope = std::unique_ptr<T>;

template<typename T, typename... Args>
constexpr Ref<T> MakeRef(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
constexpr Scope<T> MakeScope(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

using UUIDValue = uint64_t;

// Bit helper used for flags and layer masks.
#define SLATE_BIT(x) (1u << (x))

// Binds a member function to the event callback signature used throughout the
// engine, capturing `this` and forwarding the arguments.
#define SLATE_BIND_EVENT_FN(fn) \
    [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

} // namespace Slate
