#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <span>

#include "pb.h"

#include "config.h"

class ProtobufHelpers
{
  public:
    using WriterFunc = std::function<bool(std::span<const uint8_t> data)>;
    using ReaderFunc = std::function<bool(std::span<uint8_t> buf)>;

    static bool WriterCallback(pb_ostream_t* stream, const pb_byte_t* buf, size_t count);
    static bool ReaderCallback(pb_istream_t* stream, pb_byte_t* buf, size_t count);
};
