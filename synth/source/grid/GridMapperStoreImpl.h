#pragma once

#include "GridMapper.pb.h"
#include "PolySynth/GridMapperStore.h"
#include "PolySynth/Synth.h"
#include "ProtobufHelpers.h"

namespace polysynth
{
class FileSystem;

class GridMapperStoreImpl : public GridMapperStore
{
  public:
    using GridMapperConfig = polysynth_GridMapperConfig;

    GridMapperStoreImpl(std::shared_ptr<FileSystem> fileSystem,
                        const Synth::Allocator& largeBufferAlloc);

    bool loadPreset(GridMapper& mapper, const std::string& name) override;
    bool savePreset(GridMapper& mapper, const std::string& name) override;
    std::vector<std::string_view> listPresets() override;

  protected:
    inline static const std::string kFileExtension = ".grid";

  private:
    std::shared_ptr<FileSystem> fileSystem;
    GridMapperConfig* buffer;

    static bool serialize(const GridMapper& mapper,
                          ProtobufHelpers::WriterFunc writer,
                          GridMapperConfig* buffer);
    static bool deserialize(GridMapper& mapper,
                            ProtobufHelpers::ReaderFunc reader,
                            GridMapperConfig* buffer);
};
}
