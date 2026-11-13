#pragma once

#include "PolySynth/Core.h"
#include "PolySynth/SampleStore.h"
#include "PolySynth/Synth.h"
#include "PolyUtil/Result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace polysynth
{
class FileSystem;

class SamplePool
{
  public:
    SamplePool(const Synth::Allocator& allocator,
               int arenaSize,
               std::shared_ptr<SampleStore> sampleStore);

    [[nodiscard]] const Sample& getSample(int id) const;
    [[nodiscard]] Result<int, LoadStatus> loadSample(const std::string& filePath);
    [[nodiscard]] Result<std::pair<int, Sample*>, LoadStatus> createSample(int size);
    void deleteSample(int id);
    [[nodiscard]] std::vector<std::string_view> listSamples(const std::string& folder) const
    {
        return sampleStore ? sampleStore->listSamples(folder) : std::vector<std::string_view>{};
    }

  private:
    std::span<sample_store_t> arena;
    std::shared_ptr<SampleStore> sampleStore;

    struct PoolItem
    {
        Sample sample = {};
        int refCount = 1;
    };

    static constexpr size_t kMaxPoolItems = 64;
    std::vector<std::optional<PoolItem>> pool = {};

    std::optional<int> getNewId();
    std::optional<std::span<sample_store_t>> findFreeArea(int size);
};
}