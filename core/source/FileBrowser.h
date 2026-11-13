#ifndef SOURCE_FILEBROWSER_H_
#define SOURCE_FILEBROWSER_H_

#include "config.h"

enum FileBrowserMode
{
    kNotInitBrowser = 0,

    kProjectBrowser,
    kModBrowser,
    kSamplesBrowser,

};

struct FileBrowserData
{
    // inputs
    FileBrowserMode mode;
    bool selectionEnabled;

    int maxFileNameLen;
    int maxBrowserListLen;

    int fileNamesBufferSize;
    char* fileNamesBuffer;

    const char* defaultPath;
    const char* customExtension;
    const char* packageMarkerFile = nullptr;

    // outputs
    int browserListLength;
    char** browserList;
    char* selectionList;

    // controllable
    int browserListPosition;

    char currentPath[kMaxFilePathLen + 1];
};

class FileSystem;

class FileBrowser
{
  public:
    void start(FileSystem* fileSystem, FileBrowserData* fileBrowserData);
    void refresh();

    bool isRoot();
    bool isFolderSelected();
    bool isFolder(int position);
    bool isPositionOnSelectionList();

    void browse();
    void resetPath();

    void getParentFolderName(char* name);
    void getSelectedItemWithPath(char* path, int maxLen);
    char* getSelectedItemName();
    char* getSelectedFolderName();
    char* getCurrentPath();

    void selectSpecificFolderName(char* folderNameToSelect);

    bool createFolderInCurrentPath(char* folderName);

    int getPathFolderLevel(char* path);

    static bool checkFilenameExtension(const char* filename, const char* extension);

  private:
    static constexpr char kWavFileExtension[] = "wav";
    static constexpr char kModFileExtension1[] = "mod";
    static constexpr char kModFileExtension2[] = "it";

    void processCurrentPathFolderList(char* buffer,
                                      int bufferSize,
                                      int maxNamesCount,
                                      int selectionListOffset);
    void processCurrentPathFileList(char* buffer, int bufferSize, int maxNamesCount);

    void openFolder(int position);
    void openParentFolder();

    FileSystem* fileSystem;
    FileBrowserData* data;

    int fileListLength;
    char** fileList;

    int folderListLength;
    char** folderList;
};

#endif /* SOURCE_FILEBROWSER_H_ */
