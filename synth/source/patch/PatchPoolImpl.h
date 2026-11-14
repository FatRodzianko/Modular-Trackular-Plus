#pragma once

#include "PolySynth/PatchPool.h"

#include "Patch.pb.h"

#include <map>
#include <memory>

namespace polysynth
{
class PatchStore;

class PatchPoolImpl : public PatchPool
{
  public:
    PatchPoolImpl(std::shared_ptr<PatchStore> store,
                  int slotCount,
                  std::function<tiny::optional<int>(const std::string&)> nameToIndex);

    bool getPatch(int idx, Patch* patch, bool loadFromStore) override;
    std::optional<std::string> getPatchName(int idx) override;

  private:
    struct Slot
    {
        static constexpr int kSlotIdxBusy = 100000;

        tiny::optional<int> idx = {};
        polysynth_Patch pbPatch = {};
        std::vector<polysynth_Param> params = {};
        int lastGetCounter = 0;
    };

    std::shared_ptr<PatchStore> store;
    std::function<tiny::optional<int>(const std::string&)> nameToIndex;

    std::vector<Slot> slots;
    std::map<int, std::string> patchNames;

    int getCounter = 0;

    void loadPatchNames();
    bool loadPatch(int idx, Patch* patch, bool loadFromStore);
};
}