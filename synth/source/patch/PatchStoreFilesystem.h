#pragma once

#include <string>

#include "PolySynth/PatchStore.h"
#include "instrument/InstrumentTypeRegistry.h"

namespace polysynth
{
class FileSystem;
class SamplePool;

class PatchStoreFilesystem : public PatchStore
{
  public:
    /**
     * @param instrumentTypeFolderStructure
     * If true, the patch files are stored in a folder structure based on the instrument type.
     * Otherwise the structure is flat.
     */
    PatchStoreFilesystem(std::shared_ptr<FileSystem> fileSystem,
                         std::shared_ptr<InstrumentTypeRegistry> instrumentTypeRegistry,
                         std::shared_ptr<SamplePool> samplePool,
                         bool instrumentTypeFolderStructure);

    std::unique_ptr<Patch> createPatch(int instrumentTypeId) override;

    /**
     * This implementation allocates additional memory for the file names, not freed until next call
     */
    std::vector<std::pair<std::string_view, int>> listPatchesFilter(
      std::function<bool(InstrumentTypeId)> filter) override;

    std::vector<std::string_view> listPatchesByType(int typeId) override;

    bool loadPatch(const std::string& name, Patch& patch, bool synchronizeAudio) override;
    bool savePatch(const std::string& name, const Patch& patch) override;
    std::optional<InstrumentTypeId> loadPatchInstrumentType(const std::string& name) override;

  private:
    std::shared_ptr<InstrumentTypeRegistry> instrumentTypeRegistry;
    std::shared_ptr<FileSystem> fileSystem;
    std::shared_ptr<SamplePool> samplePool;

    std::vector<std::pair<std::string, int>> listPatchesBuffer = {};

    static bool filterByExtension(const std::string& name)
    {
        return name.ends_with(kFileExtension);
    };

    /**
     * @brief If true, the patch files are stored in a folder structure based on the instrument
     * type. Otherwise the structure is flat.
     */
    bool instrumentTypeFolderStructure;

    static std::string createPatchFileName(const std::string& patchName, int instrumentId);
    [[nodiscard]] std::string getPatchFilePath(const std::string& name, const Patch& patch) const;
};
}
