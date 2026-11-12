#include "SamplePool.h"
#include "PolySynth/FileSystem.h"

#include "PolyUtil/Result.h"

#include <algorithm>
#include <cassert>

namespace polysynth
{
SamplePool::SamplePool(const Synth::Allocator& allocator,
                       int arenaSize,
                       std::shared_ptr<SampleStore> sampleStore_)
  : arena(static_cast<sample_store_t*>(allocator(arenaSize)), arenaSize / sizeof(sample_store_t))
  , sampleStore(std::move(sampleStore_))
{
}

const Sample& SamplePool::getSample(int id) const
{
    const auto& item = pool.at(id);
    assert(item);
    return item->sample;
}

int SamplePool::getNewId()
{
    auto it = std::ranges::find_if(pool, [](const auto& item) { return !item; });

    if (it != pool.end())
    {
        return static_cast<int>(it - pool.begin());
    }

    pool.emplace_back(std::nullopt);
    return static_cast<int>(pool.size() - 1);
}

Result<int, LoadStatus> SamplePool::loadSample(const std::string& filePath)
{
    auto sampleSize = sampleStore->getSampleSize(filePath);

    if (!sampleSize)
    {
        return LoadStatus::kFileError;
    }

    auto freeArea = findFreeArea(*sampleSize);

    if (!freeArea)
    {
        return LoadStatus::kOutOfMemoryError;
    }

    auto sample = sampleStore->loadSample(filePath, *freeArea);

    if (!sample)
    {
        return LoadStatus::kFileError;
    }

    int id = getNewId();

    static_assert(std::is_same<sample_store_t, int16_t>::value);
    pool.at(id) = PoolItem{ .sample = *sample };

    return id;
}

std::optional<std::span<sample_store_t>> SamplePool::findFreeArea(int size)
{
    if (pool.empty())
    {
        return std::ssize(arena) > size ? std::optional{ arena.subspan(0, size) } : std::nullopt;
    }

    std::vector<const PoolItem*> poolOrdered;

    for (const auto& item : pool)
    {
        if (item)
        {
            poolOrdered.push_back(&*item);
        }
    }

    if (poolOrdered.empty())
    {
        return std::ssize(arena) > size ? std::optional{ arena.subspan(0, size) } : std::nullopt;
    }

    std::ranges::sort(poolOrdered,
                      [](const auto* item1, const auto* item2)
                      { return item1->sample.data.data() < item2->sample.data.data(); });

    if ((*poolOrdered.begin())->sample.data.begin() - arena.begin() >= size)
    {
        return arena.subspan(0, size);
    }

    for (int itemIdx = 0; itemIdx < std::ssize(poolOrdered); itemIdx++)
    {
        auto nextBegin = itemIdx + 1 < std::ssize(poolOrdered)
                           ? poolOrdered[itemIdx + 1]->sample.data.begin()
                           : arena.end();
        if (nextBegin - poolOrdered[itemIdx]->sample.data.end() >= size)
        {
            return std::span(poolOrdered[itemIdx]->sample.data.end(), size);
        }
    }

    return std::nullopt;
}

void SamplePool::deleteSample(int id)
{
    pool.at(id) = std::nullopt;
}
}