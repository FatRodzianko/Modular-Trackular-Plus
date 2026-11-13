#include "GridMapperStoreImpl.h"
#include "PolySynth/FileSystem.h"

#include "PolyUtil/AudioCS.h"
#include "PolyUtil/ScopeExit.h"
#include "pb_decode.h"
#include "pb_encode.h"

namespace polysynth
{

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
chordscheme_t getChordSchemeFromPb(const uint64_t (&pbChordScheme)[2])
{
    chordscheme_t chordScheme{ chordscheme_t{ pbChordScheme[0] } |
                               (chordscheme_t{ pbChordScheme[1] } << 64) };
    return chordScheme;
}

void fillPbChordScheme(chordscheme_t chordScheme, uint64_t (&pbChordScheme)[2])
{
    std::ranges::fill(pbChordScheme, 0);
    static constexpr int kChordSchemeElementSize = kChordMaxWidth / 2;
    for (size_t i = 0; i < kChordMaxWidth; ++i)
    {
        if (chordScheme[i])
        {
            size_t idx = i / kChordSchemeElementSize;
            size_t bit = i % kChordSchemeElementSize;
            (pbChordScheme)[idx] |= static_cast<uint64_t>(1) << bit;
        }
    }
}
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)

GridMapperStoreImpl::GridMapperStoreImpl(std::shared_ptr<FileSystem> fileSystem,
                                         const Synth::Allocator& largeBufferAlloc)
  : fileSystem(std::move(fileSystem))
  , buffer(static_cast<GridMapperConfig*>(largeBufferAlloc(sizeof(GridMapperConfig))))
{
}

bool GridMapperStoreImpl::loadPreset(GridMapper& mapper, const std::string& name)
{
    auto file =
      fileSystem->open(kGridPresetsFolder + name + kFileExtension, FileSystem::OpenOptions::kNone);

    if (!file)
    {
        return false;
    }

    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });

    return deserialize(
      mapper,
      [this, &file](std::span<uint8_t> buf) -> bool
      { return fileSystem->read(*file, buf) == std::ssize(buf); },
      buffer);
}

bool GridMapperStoreImpl::savePreset(GridMapper& mapper, const std::string& name)
{
    auto file =
      fileSystem->open(kGridPresetsFolder + name + kFileExtension, FileSystem::OpenOptions::kWrite);

    if (!file)
    {
        return false;
    }

    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });

    return serialize(
      mapper,
      [this, &file](std::span<const uint8_t> data) -> bool
      { return fileSystem->write(*file, data); },
      buffer);
}

std::vector<std::string_view> GridMapperStoreImpl::listPresets()
{

    std::vector<std::string_view> files = fileSystem->listFiles(
      kGridPresetsFolder,
      [](const std::string& name)
      {
          std::string_view nameView(name);

          if (nameView.size() < kFileExtension.size())
          {
              return false;
          }

          return nameView.substr(nameView.size() - kFileExtension.size()) == kFileExtension;
      },
      std::nullopt);

    std::ranges::transform(files,
                           files.begin(),
                           [](auto& fileView)
                           {
                               assert(fileView.size() > kFileExtension.size());
                               return fileView.substr(0, fileView.size() - kFileExtension.size());
                           });

    return files;
}

bool GridMapperStoreImpl::serialize(const GridMapper& mapper,
                                    ProtobufHelpers::WriterFunc writer,
                                    GridMapperConfig* buffer)
{
    {
        AudioCS cs;
        for (int row = 0; row < kGridRows; row++)
        {
            for (int column = 0; column < kGridColumns; column++)
            {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                auto& destConfig = buffer->padConfigs[row * kGridColumns + column];
                const auto& srcConfig = mapper.getPadConfig(row, column);

                destConfig.valid = srcConfig.has_value();
                if (destConfig.valid)
                {
                    destConfig.note = srcConfig->chord.rootNote;
                    fillPbChordScheme(srcConfig->chord.scheme, destConfig.chordScheme);
                    destConfig.instrument = srcConfig->instrument;
                    destConfig.scaleRoot = srcConfig->scale.rootNote;
                    destConfig.scaleScheme = srcConfig->scale.scheme.to_ullong();
                    destConfig.has_scaleScheme = true;
                }
            }
        }
        auto&& scale = mapper.getScale();
        buffer->scaleRoot = scale.rootNote;
        buffer->scaleScheme = scale.scheme.to_ullong();
    }

    pb_ostream_t stream = {
        &ProtobufHelpers::WriterCallback, static_cast<void*>(&writer), SIZE_MAX, 0, nullptr
    };
    bool const result = pb_encode(&stream, &polysynth_GridMapperConfig_msg, buffer);
    return result;
}

bool GridMapperStoreImpl::deserialize(GridMapper& mapper,
                                      ProtobufHelpers::ReaderFunc reader,
                                      GridMapperConfig* buffer)
{
    pb_istream_t stream = {
        &ProtobufHelpers::ReaderCallback, static_cast<void*>(&reader), SIZE_MAX, nullptr
    };

    if (!pb_decode(&stream, &polysynth_GridMapperConfig_msg, buffer))
    {
        return false;
    }

    {
        AudioCS cs;
        for (int row = 0; row < kGridRows; row++)
        {
            for (int column = 0; column < kGridColumns; column++)
            {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                const auto& srcConfig = buffer->padConfigs[row * kGridColumns + column];
                if (srcConfig.valid)
                {
                    chordscheme_t chordScheme = getChordSchemeFromPb(srcConfig.chordScheme);
                    GridMapper::PadConfig const destConfig{

                        .chord{ (chordScheme == Musicology::kEmptyChordScheme)
                                  ? Musicology::rootNoteChordScheme()
                                  : chordScheme,
                                srcConfig.note },
                        .scale = (srcConfig.has_scaleScheme)
                                   ? Musicology::Scale{ srcConfig.scaleScheme, srcConfig.scaleRoot }
                                   : Musicology::createScale(0, srcConfig.scaleRoot),
                        .instrument = srcConfig.instrument,

                    };
                    mapper.setPadConfig(row, column, destConfig);
                }
            }
        }
        mapper.setScale((buffer->scaleScheme == 0)
                          ? Musicology::createScale(0, buffer->scaleRoot)
                          : Musicology::Scale{ buffer->scaleScheme, buffer->scaleRoot });
    }
    return true;
}

}
