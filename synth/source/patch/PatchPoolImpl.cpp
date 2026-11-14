#include "PatchPoolImpl.h"
#include "PatchImpl.h"
#include "PolySynth/PatchStore.h"
#include "PolyUtil/AudioCS.h"

namespace polysynth
{
PatchPoolImpl::PatchPoolImpl(std::shared_ptr<PatchStore> store_,
                             int slotCount,
                             std::function<tiny::optional<int>(const std::string&)> nameToIndex_)
  : store(std::move(store_))
  , nameToIndex(std::move(nameToIndex_))
{
    loadPatchNames();

    slots.resize(slotCount, Slot{ .idx = std::nullopt });
}

bool PatchPoolImpl::loadPatch(int idx, Patch* patch, bool loadFromStore)
{
    if (!loadFromStore)
    {
        return false;
    }

    auto nameIt = patchNames.find(idx);

    if (nameIt == patchNames.end())
    {
        return false;
    }

    std::unique_ptr<Patch> tempPatch = nullptr;

    if (patch == nullptr)
    {
        auto instrumentTypeId = store->loadPatchInstrumentType(nameIt->second);
        if (!instrumentTypeId)
        {
            return false;
        }
        tempPatch = store->createPatch(**instrumentTypeId);
    }

    if (!store->loadPatch(nameIt->second, patch != nullptr ? *patch : *tempPatch, false))
    {
        return false;
    }

    auto newSlotIt = slots.end();

    {
        AudioCS cs;

        newSlotIt =
          std::find_if(slots.begin(), slots.end(), [](const auto& slot) { return !slot.idx; });

        if (newSlotIt == slots.end())
        {
            newSlotIt = std::min_element(slots.begin(),
                                         slots.end(),
                                         [](const auto& a, const auto& b)
                                         { return a.lastGetCounter < b.lastGetCounter; });
            assert(newSlotIt != slots.end());
        }

        newSlotIt->idx = Slot::kSlotIdxBusy; // prevent audio thread using it while serializing
        newSlotIt->lastGetCounter = getCounter;
    }

    PatchImpl::serialize(
      patch != nullptr ? *patch : *tempPatch, newSlotIt->params, newSlotIt->pbPatch);
    newSlotIt->idx = idx;
    return true;
}

bool PatchPoolImpl::getPatch(int idx, Patch* patch, bool loadFromStore)
{
    getCounter++;

    auto entryIt =
      std::find_if(slots.begin(), slots.end(), [idx](const auto& slot) { return slot.idx == idx; });

    if (entryIt == slots.end())
    {
        return loadPatch(idx, patch, loadFromStore);
    }

    if (patch != nullptr)
    {
        entryIt->lastGetCounter = getCounter;
        auto result = PatchImpl::deserialize(*patch, entryIt->pbPatch);
        assert(result);
        if (!result)
        {
            return false;
        }
    }

    return true;
}

void PatchPoolImpl::loadPatchNames()
{
    const auto list = store->listPatchesFilter({});
    patchNames.clear();

    for (const auto& [name, _] : list)
    {
        std::string nameCopy{ name };
        auto idx = nameToIndex(nameCopy);

        if (!idx || patchNames.contains(*idx))
        {
            assert(false); // fail in debug to detect invalid patch content
            continue;
        }

        patchNames.emplace(*idx, std::move(nameCopy));
    }
}

std::optional<std::string> PatchPoolImpl::getPatchName(int idx)
{
    auto it = patchNames.find(idx);
    return it != patchNames.end() ? std::optional(it->second) : std::nullopt;
}

}