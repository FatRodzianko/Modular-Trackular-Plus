#ifndef SOURCE_UTIL_STRING_H_
#define SOURCE_UTIL_STRING_H_

#include <cctype>
#include <cstring>

// compares strings alphabetically and with numbers inside strings when function find first char
// that is less than the character being compared
bool compareStrings(const char* a, const char* b);

// extension should not include the leading dot
inline bool hasFileExtension(const char* filename, const char* extension)
{
    if (filename == nullptr || extension == nullptr)
    {
        return false;
    }

    size_t filenameLength = std::strlen(filename);
    size_t extensionLength = std::strlen(extension);

    if (extensionLength == 0 || filenameLength <= extensionLength ||
        filename[filenameLength - extensionLength - 1] != '.')
    {
        return false;
    }

    const char* filenameExtension = filename + filenameLength - extensionLength;
    for (size_t idx = 0; idx < extensionLength; idx++)
    {
        if (std::tolower(static_cast<unsigned char>(filenameExtension[idx])) !=
            std::tolower(static_cast<unsigned char>(extension[idx])))
        {
            return false;
        }
    }

    return true;
}

// copies string like standard strncpy and insert 0 at the last character to avoid memory leaks
// destSize is size of destination buffer (not string length as in standard strncpy)
void polystrncpy(char* dest, const char* src, int destSize);

#endif /* SOURCE_UTIL_STRING_H_ */
