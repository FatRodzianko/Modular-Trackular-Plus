#include "SampleStoreImpl.h"

#include <cctype>
#include <charconv>

namespace polysynth
{
namespace
{
tiny_optional<note_t> getNoteValue(std::string_view notePart)
{
    if (notePart == "C")
        return 0;
    if (notePart == "C#")
        return 1;
    if (notePart == "D")
        return 2;
    if (notePart == "D#")
        return 3;
    if (notePart == "E")
        return 4;
    if (notePart == "F")
        return 5;
    if (notePart == "F#")
        return 6;
    if (notePart == "G")
        return 7;
    if (notePart == "G#")
        return 8;
    if (notePart == "A")
        return 9;
    if (notePart == "A#")
        return 10;
    if (notePart == "B")
        return 11;
    return std::nullopt;
}

tiny_optional<note_t> extractNoteFromFilename(const std::string& filepath)
{
    auto postfixEnd = filepath.find_last_of('.');
    auto postfixBegin = filepath.find_last_of('_');
    if (postfixEnd == std::string::npos || postfixBegin == std::string::npos ||
        postfixEnd <= postfixBegin + 1)
    {
        return std::nullopt;
    }
    std::string_view note(filepath.data() + postfixBegin + 1, postfixEnd - postfixBegin - 1);
    if (note.empty())
    {
        return std::nullopt;
    }
    int idx = 0;
    while (idx < std::ssize(note) &&
           ((std::isalpha(static_cast<unsigned char>(note[idx])) != 0) || note[idx] == '#'))
    {
        ++idx;
    }
    if (idx == 0 || idx > 3)
    {
        return std::nullopt;
    }
    auto notePart = note.substr(0, idx);
    auto octavePart = note.substr(idx);
    auto noteValue = getNoteValue(notePart);
    if (!noteValue || octavePart.empty())
    {
        return std::nullopt;
    }
    int octave = 0;
    auto [_, ec] =
      std::from_chars(octavePart.data(), octavePart.data() + octavePart.size(), octave);
    if (ec != std::errc())
    {
        return std::nullopt;
    }
    int midiNote = (octave + 1) * kOneOctaveNotes + *noteValue;
    if (midiNote < kNoteMin || midiNote > kNoteMax)
    {
        return std::nullopt;
    }
    return midiNote;
}
} // namespace

SampleStoreImpl::SampleStoreImpl(const std::shared_ptr<FileSystem>& fileSystem,
                                 const Synth::Allocator& allocator)
  : fileSystem(fileSystem)
  , fileIO(fileSystem, allocator)
{
}

std::optional<Sample> SampleStoreImpl::loadSample(const std::string& filePath,
                                                  std::span<sample_store_t> destBuf)
{
    int sampleRate = 0;
    int sampleCount = 0;
    auto result = fileIO.load(filePath, destBuf, sampleRate, sampleCount, [](float) {});

    if (result != SampleFileIO::OperationResult::kNoError)
    {
        return std::nullopt;
    }

    return Sample{ .name = filePath,
                   .data = destBuf.subspan(0, sampleCount),
                   .format = Sample::Format{ .channels = 1, .sampleRate = sampleRate },
                   .waveSize = getWaveSize(filePath),
                   .ready = true,
                   .basePitch = getBasePitch(filePath) };
}

note_t SampleStoreImpl::getBasePitch(const std::string& filePath)
{
    return extractNoteFromFilename(filePath).value_or(kNoteC4);
}

tiny_optional<int> SampleStoreImpl::getSampleSize(const std::string& filePath)
{
    return fileIO.getSampleCount(filePath);
}

std::vector<std::string_view> SampleStoreImpl::listSamples(const std::string& folder)
{
    return fileSystem->listFiles(
      folder.empty() ? "." : folder,
      [](const std::string& name)
      {
          constexpr std::string_view ext = SampleFileIO::kWavExtension;
          if (name.size() <= ext.size())
          {
              return false;
          }
          const size_t offset = name.size() - ext.size();
          for (size_t i = 0; i < ext.size(); ++i)
          {
              if (std::tolower(static_cast<unsigned char>(name[offset + i])) !=
                  std::tolower(static_cast<unsigned char>(ext[i])))
              {
                  return false;
              }
          }
          return true;
      },
      std::nullopt);
};

// NOLINTBEGIN(*-magic-numbers)
tiny_optional<int> SampleStoreImpl::getWaveSize(const std::string& filePath)
{
    if (filePath.find("128") != std::string::npos)
    {
        return 128;
    }
    if (filePath.find("256") != std::string::npos)
    {
        return 256;
    }
    if (filePath.find("512") != std::string::npos)
    {
        return 512;
    }
    if (filePath.find("1024") != std::string::npos)
    {
        return 1024;
    }
    if (filePath.find("2048") != std::string::npos)
    {
        return 2048;
    }

    return std::nullopt;
}
// NOLINTEND(*-magic-numbers)

}