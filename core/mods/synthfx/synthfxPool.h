// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// A fixed pool of memory handed out in blocks, first fit, for the Synth FX plugins' code and
// instances (synthfxPlugins.cpp). Nothing moves once given out: the audio interrupt may be
// running a block's code or state, so a freed block leaves a hole the next fit can use. No
// heap. Main loop only. Header only, so the host tests build it as it is.

#ifndef MODS_SYNTHFX_SYNTHFXPOOL_H_
#define MODS_SYNTHFX_SYNTHFXPOOL_H_

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace synthfx
{

// Blocks: up to MaxBlocks given out at once, each aligned to Align bytes from the pool's
// start (the pool itself must be aligned as much).
template <size_t MaxBlocks, uint32_t Align = 32>
class Pool
{
  public:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    // The pool's memory: size bytes at base. Forgets every block.
    void init(uint8_t* base, uint32_t size)
    {
        base_ = base;
        size_ = size;
        count_ = 0;
    }

    // An offset into the pool for size bytes, or kNone if no hole is big enough (or the
    // table of blocks is full).
    uint32_t alloc(uint32_t size)
    {
        if (size == 0 || size > size_ || count_ >= MaxBlocks)
        {
            return kNone;
        }
        const uint32_t need = (size + Align - 1u) / Align * Align;
        uint32_t at = 0;
        size_t i = 0;
        for (; i <= count_; i++) // blocks are kept in order of offset: try the hole before each, then the end
        {
            const uint32_t end = i < count_ ? blocks_[i].offset : size_;
            if (end >= at && end - at >= need)
            {
                break;
            }
            if (i < count_)
            {
                at = blocks_[i].offset + blocks_[i].size;
            }
        }
        if (i > count_)
        {
            return kNone;
        }
        for (size_t j = count_; j > i; j--)
        {
            blocks_[j] = blocks_[j - 1];
        }
        blocks_[i] = { at, need };
        count_++;
        return at;
    }

    // A block given out by alloc goes back. Any other offset does nothing.
    void free(uint32_t offset)
    {
        for (size_t i = 0; i < count_; i++)
        {
            if (blocks_[i].offset == offset)
            {
                for (size_t j = i; j + 1 < count_; j++)
                {
                    blocks_[j] = blocks_[j + 1];
                }
                count_--;
                return;
            }
        }
    }

    uint8_t* at(uint32_t offset) const { return base_ + offset; }
    size_t blocks() const { return count_; }
    uint32_t used() const
    {
        uint32_t n = 0;
        for (size_t i = 0; i < count_; i++)
        {
            n += blocks_[i].size;
        }
        return n;
    }
    // The biggest block alloc could give now
    uint32_t largestFree() const
    {
        uint32_t best = 0, at = 0;
        for (size_t i = 0; i <= count_; i++)
        {
            const uint32_t end = i < count_ ? blocks_[i].offset : size_;
            if (end > at && end - at > best)
            {
                best = end - at;
            }
            if (i < count_)
            {
                at = blocks_[i].offset + blocks_[i].size;
            }
        }
        return count_ >= MaxBlocks ? 0 : best / Align * Align;
    }

  private:
    struct Block
    {
        uint32_t offset;
        uint32_t size;
    };
    uint8_t* base_ = nullptr;
    uint32_t size_ = 0;
    size_t count_ = 0;
    Block blocks_[MaxBlocks] = {};
};

} // namespace synthfx
} // namespace mods

#endif // MODS_SYNTHFX_SYNTHFXPOOL_H_
