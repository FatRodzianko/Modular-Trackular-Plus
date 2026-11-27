#include "SdTextFile.h"

#include <cstring>

namespace
{
bool isWordDelimiter(char c)
{
    return c == '\n' || c == '\r' || c == '_' || c == ',' || c == '\t';
}

bool isInvalidFilenameChar(char c)
{
    switch (c)
    {
        case '\\':
        case '/':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return true;
        default:
            return static_cast<unsigned char>(c) < 0x20;
    }
}

bool extractFirstWord(const char* buf, size_t len, bool skipPartialFirst, char* outWord, size_t maxLen)
{
    size_t i = 0;
    if (skipPartialFirst)
    {
        while (i < len && !isWordDelimiter(buf[i]))
        {
            ++i;
        }
    }

    while (i < len && isWordDelimiter(buf[i]))
    {
        ++i;
    }

    size_t outIdx = 0;
    while (i < len && !isWordDelimiter(buf[i]))
    {
        char c = buf[i++];
        if (!isInvalidFilenameChar(c) && outIdx + 1 < maxLen)
        {
            outWord[outIdx++] = c;
        }
    }

    outWord[outIdx] = '\0';
    return outIdx > 0;
}
} // namespace

SdScopedFile::SdScopedFile(FileSystem* fs, const char* path, FileSystem::FileOpenMode mode)
  : fs_(fs)
  , handle_(nullptr)
  , open_(false)
{
    if (fs_ != nullptr && path != nullptr)
    {
        std::tie(open_, handle_) = SdTextFile::openWithFallback(fs_, path, mode);
    }
}

SdScopedFile::SdScopedFile(FileSystem* fs, FileSystem::FileHandle handle, bool isOpen)
  : fs_(fs)
  , handle_(handle)
  , open_(isOpen)
{
}

SdScopedFile::~SdScopedFile()
{
    if (fs_ != nullptr && open_ && handle_ != nullptr)
    {
        fs_->closeFile(handle_);
    }
}

namespace SdTextFile
{
std::tuple<bool, FileSystem::FileHandle> openWithFallback(FileSystem* fs,
                                                          const char* path,
                                                          FileSystem::FileOpenMode mode)
{
    if (fs == nullptr || path == nullptr)
    {
        return { false, nullptr };
    }

    auto [isOpen, file] = fs->openFile(path, mode);
    if ((!isOpen || !file) && path[0] == '/')
    {
        std::tie(isOpen, file) = fs->openFile(path + 1, mode);
    }
    return { isOpen, file };
}

bool readAll(FileSystem* fs, const char* path, char* buf, size_t bufSize, size_t* outLen)
{
    if (buf == nullptr || bufSize < 2)
    {
        return false;
    }

    SdScopedFile file(fs, path, FileSystem::FileOpenMode::kRead);
    if (!file)
    {
        return false;
    }

    int32_t bytesRead = fs->readFile(buf, bufSize - 1, file.handle());
    if (bytesRead <= 0)
    {
        return false;
    }

    buf[bytesRead] = '\0';
    if (outLen != nullptr)
    {
        *outLen = static_cast<size_t>(bytesRead);
    }
    return true;
}

bool readRandomWord(FileSystem* fs, const char* path, uint32_t seed, char* outWord, size_t maxLen)
{
    if (outWord == nullptr || maxLen < 2)
    {
        return false;
    }

    SdScopedFile file(fs, path, FileSystem::FileOpenMode::kRead);
    if (!file)
    {
        return false;
    }

    size_t fileSize = fs->getFileSize(file.handle());
    if (fileSize == 0)
    {
        return false;
    }

    char chunk[96];
    uint32_t offset = seed % static_cast<uint32_t>(fileSize);
    if (fs->seekFile(offset, file.handle()))
    {
        int32_t n = fs->readFile(chunk, sizeof(chunk), file.handle());
        if (n > 0 && extractFirstWord(chunk, static_cast<size_t>(n), offset > 0, outWord, maxLen))
        {
            return true;
        }
    }

    // Wrapped near EOF: fall back to the beginning of the file.
    if (offset > 0 && fs->seekFile(0, file.handle()))
    {
        int32_t n = fs->readFile(chunk, sizeof(chunk), file.handle());
        if (n > 0 && extractFirstWord(chunk, static_cast<size_t>(n), false, outWord, maxLen))
        {
            return true;
        }
    }

    return false;
}

bool readSection(FileSystem* fs,
                 const char* path,
                 const char* key,
                 char* outBuf,
                 size_t outSize)
{
    if (outBuf == nullptr || outSize < 2)
    {
        if (outBuf != nullptr && outSize > 0)
        {
            outBuf[0] = '\0';
        }
        return false;
    }

    outBuf[0] = '\0';

    if (fs == nullptr || path == nullptr || key == nullptr || key[0] == '\0')
    {
        return false;
    }

    SdScopedFile file(fs, path, FileSystem::FileOpenMode::kRead);
    if (!file)
    {
        return false;
    }

    enum class ParseState : uint8_t
    {
        kLineStart,
        kMatchKey,
        kHeaderTailMatch,
        kSkipLine,
        kCopyLine,
    };

    ParseState state = ParseState::kLineStart;
    size_t keyIdx = 0;
    size_t outIdx = 0;
    bool inTarget = false;
    bool found = false;
    bool done = false;

    alignas(4) char chunk[256];
    while (!done)
    {
        int32_t bytesRead = fs->readFile(chunk, sizeof(chunk), file.handle());
        if (bytesRead <= 0)
        {
            break;
        }

        for (int32_t i = 0; i < bytesRead && !done; ++i)
        {
            char c = chunk[i];
            if (c == '\r')
            {
                continue;
            }

            switch (state)
            {
                case ParseState::kLineStart:
                    if (c == '#')
                    {
                        state = ParseState::kSkipLine;
                    }
                    else if (c == '[')
                    {
                        if (inTarget)
                        {
                            done = true;
                        }
                        else
                        {
                            keyIdx = 0;
                            state = ParseState::kMatchKey;
                        }
                    }
                    else if (c == '\n')
                    {
                        if (inTarget && outIdx + 1 < outSize)
                        {
                            outBuf[outIdx++] = '\n';
                        }
                    }
                    else if (inTarget)
                    {
                        if (outIdx + 1 < outSize)
                        {
                            outBuf[outIdx++] = c;
                        }
                        state = ParseState::kCopyLine;
                    }
                    else
                    {
                        state = ParseState::kSkipLine;
                    }
                    break;

                case ParseState::kMatchKey:
                    if (c == '\n')
                    {
                        state = ParseState::kLineStart;
                    }
                    else if (c == ']')
                    {
                        state = (key[keyIdx] == '\0') ? ParseState::kHeaderTailMatch
                                                      : ParseState::kSkipLine;
                    }
                    else if (key[keyIdx] != '\0' && c == key[keyIdx])
                    {
                        ++keyIdx;
                    }
                    else
                    {
                        state = ParseState::kSkipLine;
                    }
                    break;

                case ParseState::kHeaderTailMatch:
                    if (c == '\n')
                    {
                        inTarget = true;
                        found = true;
                        state = ParseState::kLineStart;
                    }
                    else if (c != ' ' && c != '\t')
                    {
                        state = ParseState::kSkipLine;
                    }
                    break;

                case ParseState::kSkipLine:
                    if (c == '\n')
                    {
                        state = ParseState::kLineStart;
                    }
                    break;

                case ParseState::kCopyLine:
                    if (c == '\n')
                    {
                        if (outIdx + 1 < outSize)
                        {
                            outBuf[outIdx++] = '\n';
                        }
                        state = ParseState::kLineStart;
                    }
                    else if (outIdx + 1 < outSize)
                    {
                        outBuf[outIdx++] = c;
                    }
                    break;
            }
        }
    }

    // Trim trailing blank / whitespace-only lines.
    while (outIdx > 0)
    {
        if (outBuf[outIdx - 1] == '\n')
        {
            --outIdx;
            continue;
        }
        size_t lineStart = outIdx;
        while (lineStart > 0 && outBuf[lineStart - 1] != '\n')
        {
            --lineStart;
        }
        bool allSpace = true;
        for (size_t k = lineStart; k < outIdx; ++k)
        {
            if (outBuf[k] != ' ' && outBuf[k] != '\t')
            {
                allSpace = false;
                break;
            }
        }
        if (allSpace)
        {
            outIdx = lineStart;
            continue;
        }
        break;
    }

    outBuf[outIdx] = '\0';
    return found && outIdx > 0;
}

void stripCr(char* buf)
{
    if (buf == nullptr)
    {
        return;
    }
    char* dst = buf;
    for (const char* src = buf; *src != '\0'; ++src)
    {
        if (*src != '\r')
        {
            *dst++ = *src;
        }
    }
    *dst = '\0';
}
} // namespace SdTextFile
