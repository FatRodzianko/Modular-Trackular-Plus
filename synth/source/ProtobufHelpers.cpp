#include "ProtobufHelpers.h"

#include "pb_decode.h"
#include "pb_encode.h"

bool ProtobufHelpers::WriterCallback(pb_ostream_t* stream, const pb_byte_t* buf, size_t count)
{
    auto* writer = static_cast<WriterFunc*>(stream->state);
    return (*writer)(std::span<const uint8_t>(buf, count));
}

bool ProtobufHelpers::ReaderCallback(pb_istream_t* stream, pb_byte_t* buf, size_t count)
{
    auto* reader = static_cast<ReaderFunc*>(stream->state);
    auto result = (*reader)(std::span<uint8_t>(buf, count));

    if (!result)
    {
        stream->bytes_left = 0;
    }

    return result;
}
