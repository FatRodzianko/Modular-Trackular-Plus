#ifndef INTERFACE_FILEMANAGER_FILEOPSWORKER_H_
#define INTERFACE_FILEMANAGER_FILEOPSWORKER_H_

#include "Platform.h"
#include "config.h"

#include <cstdint>

class FileOpsWorker
{
  public:
    enum class OpType : uint8_t
    {
        kNone = 0,
        kCopy,
        kMove,
        kDelete
    };

    enum class State : uint8_t
    {
        kIdle = 0,
        kRunning,
        kWaitCollision,
        kDone,
        kCancelled,
        kError
    };

    enum class CollisionAction : uint8_t
    {
        kNone = 0,
        kOverwrite,
        kSkip,
        kAutoRename,
        kCancel
    };

    struct BatchItem
    {
        char name[kMaxFileNameLen + 1];
        bool isFolder;
    };

    struct DirFrame
    {
        char srcDir[kMaxFilePathLen + 1];
        char dstDir[kMaxFilePathLen + 1];
        uint32_t entryIndex;
    };

    static constexpr int kMaxBatchItems = kMaxFilesListLength;
    static constexpr int kMaxDirDepth = 16;
    static constexpr uint32_t kCopyIoBufBytes = 32 * 1024;

    struct ScratchLayout
    {
        BatchItem items[kMaxBatchItems];
        DirFrame dirStack[kMaxDirDepth];
    };

    void init(FileSystem* fileSystem, uint8_t* scratchMem, uint32_t scratchBytes, uint8_t* ioBuf);

    bool startBatch(OpType op,
                    const char* srcDir,
                    const char* dstDir,
                    const BatchItem* items,
                    int itemCount,
                    const char* activeProjectPath);

    void step();
    void resolveCollision(CollisionAction action, bool applyToAll);
    void cancel();
    void reset();

    State getState() const { return state; }
    OpType getOpType() const { return opType; }
    bool isScanning() const { return scanActive; }
    int getCompletedCount() const { return completedItems; }
    int getTotalCount() const { return totalItems; }
    int getSkippedCount() const { return skippedItems; }
    int getDisplayCurrentIndex() const;
    int getDisplayTotalCount() const;
    int getProgressPercent() const;
    const char* getCurrentItemName() const { return currentItemLabel; }
    const char* getCollisionTargetName() const { return collisionTargetName; }
    const char* getErrorMessage() const { return errorMsg; }

    static bool isProtectedPath(const char* relPath, const char* activeProjectPath);
    static bool isSelfOrDescendant(const char* srcPath, const char* dstDir);
    static void normalizePath(const char* inPath, char* outPath, int outSize);
    static bool joinPath(const char* dir, const char* name, char* outPath, int outSize);
    static bool makeAutoRenamePath(FileSystem* fs,
                                   const char* dstDir,
                                   const char* origName,
                                   bool isFolder,
                                   char* outPath,
                                   int outSize);

  private:
    void stepScan();
    void beginNextTopItem();
    void executeResolvedTopItem(const char* srcPath, const char* dstPath, bool isFolder);
    void stepActiveFileCopy();
    void stepDirCopy();
    bool startFileCopy(const char* srcPath, const char* dstPath);
    void closeOpenFiles();
    void failWith(const char* msg);

    FileSystem* fs = nullptr;
    ScratchLayout* scratch = nullptr;
    uint8_t* ioBuffer = nullptr;

    OpType opType = OpType::kNone;
    State state = State::kIdle;

    char srcRootDir[kMaxFilePathLen + 1]{};
    char dstRootDir[kMaxFilePathLen + 1]{};
    char activeProjPath[kMaxFilePathLen + 1]{};

    int totalItems = 0;
    int currentItemIdx = 0;
    int completedItems = 0;
    int skippedItems = 0;

    CollisionAction stickyCollisionAction = CollisionAction::kNone;
    char pendingSrcPath[kMaxFilePathLen + 1]{};
    char pendingDstPath[kMaxFilePathLen + 1]{};
    char pendingItemName[kMaxFileNameLen + 1]{};
    bool pendingIsFolder = false;

    int dirDepth = 0;

    // Pre-scan state for byte-weighted copy progress
    bool scanActive = false;
    int scanItemIdx = 0;
    uint64_t totalBytes = 0;
    uint64_t bytesCompleted = 0;
    int totalFiles = 0;
    int filesCompleted = 0;

    bool fileCopyActive = false;
    FileSystem::FileHandle srcFileHandle = nullptr;
    FileSystem::FileHandle dstFileHandle = nullptr;
    uint64_t currentFileSize = 0;
    uint64_t currentFileCopied = 0;

    char currentItemLabel[64]{};
    char collisionTargetName[kMaxFileNameLen + 1]{};
    char errorMsg[64]{};
};

#endif /* INTERFACE_FILEMANAGER_FILEOPSWORKER_H_ */
