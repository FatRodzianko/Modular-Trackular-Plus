#pragma once

// Copyright (c) 2026 Polyend Tracker Community Edition Contributors
// SPDX-License-Identifier: MIT
//
// Clean-room C++17 compile-time enum bounds and reflection helpers.
// Scans enumerator values in [int8_t::min(), int8_t::max()] using compiler
// function signatures (__PRETTY_FUNCTION__) to detect named enumerators.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace enumutil
{
namespace detail
{
template<typename T, T Value>
constexpr std::string_view rawSignature()
{
    return __PRETTY_FUNCTION__;
}

template<typename T, T Value>
constexpr std::string_view extractValueToken()
{
    constexpr std::string_view sig = rawSignature<T, Value>();
    constexpr std::string_view marker = "Value = ";
    constexpr std::size_t pos = sig.find(marker);
    if constexpr (pos == std::string_view::npos)
    {
        return {};
    }
    else
    {
        constexpr std::size_t start = pos + marker.size();
        constexpr std::size_t endSemi = sig.find(';', start);
        constexpr std::size_t endBracket = sig.find(']', start);
        constexpr std::size_t end = (endSemi < endBracket) ? endSemi : endBracket;
        return sig.substr(start, end - start);
    }
}

template<typename T, int Value>
constexpr bool isEnumeratorValid()
{
    constexpr std::string_view token = extractValueToken<T, static_cast<T>(Value)>();
    return !token.empty() && token.find('(') == std::string_view::npos;
}

template<typename T, int Current, int Limit>
constexpr typename std::underlying_type<T>::type findMin()
{
    if constexpr (isEnumeratorValid<T, Current>())
    {
        return static_cast<typename std::underlying_type<T>::type>(Current);
    }
    else if constexpr (Current < Limit)
    {
        return findMin<T, Current + 1, Limit>();
    }
    else
    {
        return static_cast<typename std::underlying_type<T>::type>(Limit);
    }
}

template<typename T, int Current, int Limit>
constexpr typename std::underlying_type<T>::type findMax()
{
    if constexpr (isEnumeratorValid<T, Current>())
    {
        return static_cast<typename std::underlying_type<T>::type>(Current);
    }
    else if constexpr (Current > Limit)
    {
        return findMax<T, Current - 1, Limit>();
    }
    else
    {
        return static_cast<typename std::underlying_type<T>::type>(Limit);
    }
}
} // namespace detail

template<typename T, T TValue>
struct EnumValid
{
    static constexpr bool valid()
    {
        return detail::isEnumeratorValid<T, static_cast<int>(TValue)>();
    }

    static constexpr bool value = valid();
};

template<typename T>
struct EnumMin
{
    static constexpr typename std::underlying_type<T>::type value =
      detail::findMin<T, std::numeric_limits<int8_t>::min(), std::numeric_limits<int8_t>::max()>();
};

template<typename T>
struct EnumMax
{
    static constexpr typename std::underlying_type<T>::type value =
      detail::findMax<T, std::numeric_limits<int8_t>::max(), std::numeric_limits<int8_t>::min()>();
};

template<typename T, T TValue>
constexpr std::string_view getName()
{
    constexpr std::string_view token = detail::extractValueToken<T, TValue>();
    if constexpr (EnumValid<T, TValue>::value)
    {
        constexpr std::size_t sep = token.rfind(':');
        if constexpr (sep != std::string_view::npos)
        {
            return token.substr(sep + 1);
        }
    }
    return token;
}
} // namespace enumutil
