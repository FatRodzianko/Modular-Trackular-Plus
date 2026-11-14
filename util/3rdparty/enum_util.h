#pragma once

/**
 * From https://www.thecodeteacher.com/question/59748/Max-and-min-values-in-a-C++-enum
 */

#include <cstddef>
#include <limits>
#include <type_traits>

#include "constexpr-string/constexpr-string.hpp"

namespace enumutil
{
template<typename T, T TValue>
struct EnumValid
{
    static constexpr bool valid()
    {
        return String(__PRETTY_FUNCTION__)
                 .find('(', String(__PRETTY_FUNCTION__).find("TValue = ")) == StringNpos;
    }

    static constexpr bool value = valid();
};

/// MIN
template<typename T, int8_t, bool TValid>
struct EnumMinImpl;

template<typename T, int8_t TIndex>
struct EnumMinImpl<T, TIndex, false>
{
    static constexpr typename std::underlying_type<T>::type value =
      EnumMinImpl<T, TIndex + 1, EnumValid<T, static_cast<T>(TIndex + 1)>::value>::value;
};

template<typename T, int8_t TIndex>
struct EnumMinImpl<T, TIndex, true>
{
    static constexpr typename std::underlying_type<T>::type value = TIndex;
};

template<typename T>
struct EnumMinImpl<T, std::numeric_limits<int8_t>::max(), false>
{
    static constexpr typename std::underlying_type<T>::type value =
      std::numeric_limits<int8_t>::max();
};

template<typename T>
struct EnumMin
{
    static constexpr typename std::underlying_type<T>::type value =
      EnumMinImpl<T, std::numeric_limits<int8_t>::min(), false>::value;
};

/// MAX
template<typename T, int8_t TIndex, bool TValid>
struct EnumMaxImpl;

template<typename T, int8_t TIndex>
struct EnumMaxImpl<T, TIndex, false>
{
    static constexpr typename std::underlying_type<T>::type value =
      EnumMaxImpl<T, TIndex - 1, EnumValid<T, static_cast<T>(TIndex - 1)>::value>::value;
};

template<typename T, int8_t TIndex>
struct EnumMaxImpl<T, TIndex, true>
{
    static constexpr typename std::underlying_type<T>::type value = TIndex;
};

template<typename T>
struct EnumMaxImpl<T, std::numeric_limits<int8_t>::min(), false>
{
    static constexpr typename std::underlying_type<T>::type value =
      std::numeric_limits<int8_t>::min();
};

template<typename T>
struct EnumMax
{
    static constexpr typename std::underlying_type<T>::type value =
      EnumMaxImpl<T, std::numeric_limits<int8_t>::max(), false>::value;
};

template<typename T, T TValue, typename std::enable_if<EnumValid<T, TValue>::value, int>::type = 1>
auto getName()
{
    constexpr std::size_t beginFullPath = String(__PRETTY_FUNCTION__).find("TValue = ") + 9;
    constexpr std::size_t end = String(__PRETTY_FUNCTION__).find(';', beginFullPath);
    constexpr std::size_t begin = String(__PRETTY_FUNCTION__).rfind(':', end) + 1;
    static_assert(end >= begin, "begin > end");
    return String(__PRETTY_FUNCTION__).template substr<begin, end>();
}

template<typename T, T TValue, typename std::enable_if<!EnumValid<T, TValue>::value, int>::type = 2>
auto getName()
{
    constexpr std::size_t begin = String(__PRETTY_FUNCTION__).find("TValue = ") + 9;
    constexpr std::size_t end = String(__PRETTY_FUNCTION__).find(';', begin);
    static_assert(end >= begin, "begin > end");
    return String(__PRETTY_FUNCTION__).template substr<begin, end>();
}
}
