#include "PatchStoreFilesystem.h"
#include "PatchImpl.h"
#include "PolySynth/FileSystem.h"

#include "PolyUtil/ScopeExit.h"

#include <algorithm>
#include <cassert>

#include "enum_util.h"

namespace polysynth
{

PatchStoreFilesystem::PatchStoreFilesystem(
  std::shared_ptr<FileSystem> fileSystem,
  std::shared_ptr<InstrumentTypeRegistry> instrumentTypeRegistry,
  std::shared_ptr<SamplePool> samplePool_,
  bool instrumentTypeFolderStructure)
  : instrumentTypeRegistry(std::move(instrumentTypeRegistry))
  , fileSystem(std::move(fileSystem))
  , samplePool(std::move(samplePool_))
  , instrumentTypeFolderStructure(instrumentTypeFolderStructure)
{
}

std::unique_ptr<Patch> PatchStoreFilesystem::createPatch(int instrumentTypeId)
{
    auto instrumentType = instrumentTypeRegistry->getInstrumentType(instrumentTypeId);
    if (instrumentType == nullptr)
    {
        instrumentType =
          instrumentTypeRegistry->getInstrumentType(instrumentTypeRegistry->getFallbackSynthTypeId());
    }
    if (instrumentType == nullptr)
    {
        return nullptr;
    }
    return std::make_unique<PatchImpl>(instrumentType, samplePool);
}

std::vector<std::pair<std::string_view, int>> PatchStoreFilesystem::listPatchesFilter(
  std::function<bool(InstrumentTypeId)> filter)
{
    listPatchesBuffer.clear();

    std::vector<std::pair<std::string_view, int>> files;

    for (const auto& [id, instrumentType] : instrumentTypeRegistry->getInstrumentTypes())
    {
        auto typeId = static_cast<InstrumentTypeId>(id);
        if (filter(typeId))
        {
            auto patches =
              fileSystem->listFiles(instrumentType->name, filterByExtension, std::nullopt);
            for (auto& patch : patches)
            {
                listPatchesBuffer.emplace_back(std::string(patch), id);
            }
        }
    }

    std::ranges::transform(
      listPatchesBuffer,
      std::back_inserter(files),
      [](std::pair<std::string, int>& fileView)
      {
          assert(fileView.first.size() >= kFileExtension.size());
          return std::pair{ std::string_view{ fileView.first }.substr(
                              0, fileView.first.size() - kFileExtension.size()),
                            fileView.second };
      });

    return files;
}

std::vector<std::string_view> PatchStoreFilesystem::listPatchesByType(int typeId)
{
    auto instrumentType = instrumentTypeRegistry->getInstrumentType(typeId);
    if (instrumentType == nullptr)
    {
        return {};
    }

    auto files = fileSystem->listFiles(instrumentType->name, filterByExtension, std::nullopt);

    std::ranges::transform(files,
                           files.begin(),
                           [](std::string_view& fileView)
                           {
                               assert(fileView.size() >= kFileExtension.size());
                               return fileView.substr(0, fileView.size() - kFileExtension.size());
                           });

    return files;
}

std::string PatchStoreFilesystem::createPatchFileName(const std::string& patchName,
                                                      int instrumentId)
{
    return patchName + "." + std::to_string(instrumentId) + kFileExtension;
}

bool PatchStoreFilesystem::loadPatch(const std::string& name, Patch& patch, bool synchronizeAudio)
{
    auto file = fileSystem->open(getPatchFilePath(name, patch), FileSystem::OpenOptions::kNone);

    if (!file)
    {
        return false;
    }

    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });

    return PatchImpl::deserialize(
      patch,
      [this, &file](std::span<uint8_t> buf) -> bool
      { return fileSystem->read(*file, buf) == std::ssize(buf); },
      synchronizeAudio);
}

bool PatchStoreFilesystem::savePatch(const std::string& name, const Patch& patch)
{
    auto file = fileSystem->open(getPatchFilePath(name, patch), FileSystem::OpenOptions::kWrite);

    if (!file)
    {
        if (instrumentTypeFolderStructure)
        {
            auto instType =
              instrumentTypeRegistry->getInstrumentType(patch.getMetadata().instrumentTypeId);
            if (!instType)
            {
                instType = instrumentTypeRegistry->getInstrumentType(
                  instrumentTypeRegistry->getFallbackSynthTypeId());
            }
            if (instType)
            {
                fileSystem->createFolder(instType->name);
            }
            file = fileSystem->open(getPatchFilePath(name, patch), FileSystem::OpenOptions::kWrite);
            if (!file)
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });

    auto writer = [this, &file](std::span<const uint8_t> data) -> bool
    { return fileSystem->write(*file, data); };

    return PatchImpl::serialize(patch, writer);
}

std::string PatchStoreFilesystem::getPatchFilePath(const std::string& name,
                                                   const Patch& patch) const
{
    auto instType =
      instrumentTypeRegistry->getInstrumentType(patch.getMetadata().instrumentTypeId);
    if (!instType)
    {
        instType = instrumentTypeRegistry->getInstrumentType(
          instrumentTypeRegistry->getFallbackSynthTypeId());
    }
    return (instrumentTypeFolderStructure && instType
              ? (std::string(instType->name) + '/')
              : "") +
           name + kFileExtension;
}

std::optional<InstrumentTypeId> PatchStoreFilesystem::loadPatchInstrumentType(
  const std::string& name)
{
    auto file = fileSystem->open(name + kFileExtension, FileSystem::OpenOptions::kNone);

    if (!file)
    {
        return {};
    }
    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });
    return PatchImpl::deserializeInstrumentType(
      [this, &file](std::span<uint8_t> buf) -> bool
      { return fileSystem->read(*file, buf) == std::ssize(buf); });
}

}
