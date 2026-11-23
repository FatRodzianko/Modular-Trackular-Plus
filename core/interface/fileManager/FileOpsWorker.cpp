#include "FileOpsWorker.h"

#include "fileManager.h"
#include "util_string.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

void FileOpsWorker::init(FileSystem* fileSystem,
                         uint8_t* scratchMem,
                         uint32_t scratchBytes,
                         uint8_t* ioBuf)
{
    fs = fileSystem;
    ioBuffer = ioBuf;
    if (scratchMem != nullptr && scratchBytes >= sizeof(ScratchLayout))
    {
        scratch = reinterpret_cast<ScratchLayout*>(scratchMem);
    }
    else
    {
        scratch = nullptr;
    }
    reset();
}

void FileOpsWorker::reset()
{
    closeOpenFiles();
    opType = OpType::kNone;
    state = State::kIdle;
    srcRootDir[0] = '\0';
    dstRootDir[0] = '\0';
    activeProjPath[0] = '\0';
    totalItems = 0;
    currentItemIdx = 0;
    completedItems = 0;
    skippedItems = 0;
    stickyCollisionAction = CollisionAction::kNone;
    pendingSrcPath[0] = '\0';
    pendingDstPath[0] = '\0';
    pendingItemName[0] = '\0';
    pendingIsFolder = false;
    dirDepth = 0;
    scanActive = false;
    scanItemIdx = 0;
    totalBytes = 0;
    bytesCompleted = 0;
    totalFiles = 0;
    filesCompleted = 0;
    currentItemLabel[0] = '\0';
    collisionTargetName[0] = '\0';
    errorMsg[0] = '\0';
}

void FileOpsWorker::normalizePath(const char* inPath, char* outPath, int outSize)
{
    if (outPath == nullptr || outSize <= 0)
    {
        return;
    }
    outPath[0] = '\0';
    if (inPath == nullptr)
    {
        return;
    }

    while (*inPath == '/')
    {
        ++inPath;
    }

    polystrncpy(outPath, inPath, outSize);
    int len = static_cast<int>(strlen(outPath));
    while (len > 0 && outPath[len - 1] == '/')
    {
        outPath[--len] = '\0';
    }
}

bool FileOpsWorker::joinPath(const char* dir, const char* name, char* outPath, int outSize)
{
    if (outPath == nullptr || outSize <= 0 || name == nullptr || name[0] == '\0')
    {
        return false;
    }

    char normDir[kMaxFilePathLen + 1];
    normalizePath(dir, normDir, sizeof(normDir));

    while (*name == '/')
    {
        ++name;
    }
    if (name[0] == '\0')
    {
        return false;
    }

    int written = 0;
    if (normDir[0] == '\0')
    {
        written = snprintf(outPath, static_cast<size_t>(outSize), "%s", name);
    }
    else
    {
        written = snprintf(outPath, static_cast<size_t>(outSize), "%s/%s", normDir, name);
    }

    return written > 0 && written < outSize;
}

bool FileOpsWorker::isProtectedPath(const char* relPath, const char* activeProjectPath)
{
    char norm[kMaxFilePathLen + 1];
    normalizePath(relPath, norm, sizeof(norm));
    if (norm[0] == '\0')
    {
        return true;
    }

    // Root-level system directories cannot be deleted, moved, or renamed
    static const char* const kProtectedRoots[] = {
        "System",
        ".system",
        "Workspace",
        "Firmware",
        "System Volume Information",
    };

    for (const char* root : kProtectedRoots)
    {
        if (strcasecmp(norm, root) == 0)
        {
            return true;
        }
    }

    // Internal Workspace contents are always protected (live project state).
    // Contents inside /System and /.system are user-manageable; only the root folders are protected.
    if (strncasecmp(norm, "Workspace/", 10) == 0)
    {
        return true;
    }

    // Currently loaded project (itself, any ancestor, or any file inside it)
    if (activeProjectPath != nullptr && activeProjectPath[0] != '\0')
    {
        char normProj[kMaxFilePathLen + 1];
        normalizePath(activeProjectPath, normProj, sizeof(normProj));
        if (normProj[0] != '\0')
        {
            size_t pathLen = strlen(norm);
            size_t projLen = strlen(normProj);

            if (strcasecmp(norm, normProj) == 0)
            {
                return true;
            }
            // Path is an ancestor of active project
            if (pathLen < projLen && strncasecmp(normProj, norm, pathLen) == 0 &&
                normProj[pathLen] == '/')
            {
                return true;
            }
            // Path is inside active project
            if (projLen < pathLen && strncasecmp(norm, normProj, projLen) == 0 &&
                norm[projLen] == '/')
            {
                return true;
            }
        }
    }

    return false;
}

bool FileOpsWorker::isSelfOrDescendant(const char* srcPath, const char* dstDir)
{
    char normSrc[kMaxFilePathLen + 1];
    char normDst[kMaxFilePathLen + 1];
    normalizePath(srcPath, normSrc, sizeof(normSrc));
    normalizePath(dstDir, normDst, sizeof(normDst));

    if (normSrc[0] == '\0')
    {
        return true;
    }
    if (strcasecmp(normSrc, normDst) == 0)
    {
        return true;
    }

    size_t srcLen = strlen(normSrc);
    if (strlen(normDst) > srcLen && strncasecmp(normDst, normSrc, srcLen) == 0 &&
        normDst[srcLen] == '/')
    {
        return true;
    }
    return false;
}

bool FileOpsWorker::makeAutoRenamePath(FileSystem* fs,
                                       const char* dstDir,
                                       const char* origName,
                                       bool isFolder,
                                       char* outPath,
                                       int outSize)
{
    if (fs == nullptr || origName == nullptr || origName[0] == '\0')
    {
        return false;
    }

    char base[kMaxFileNameLen + 1]{};
    const char* ext = "";

    if (!isFolder)
    {
        const char* dot = strrchr(origName, '.');
        if (dot != nullptr && dot != origName)
        {
            size_t baseLen = static_cast<size_t>(dot - origName);
            if (baseLen > kMaxFileNameLen)
            {
                baseLen = kMaxFileNameLen;
            }
            memcpy(base, origName, baseLen);
            base[baseLen] = '\0';
            ext = dot;
        }
        else
        {
            polystrncpy(base, origName, sizeof(base));
        }
    }
    else
    {
        polystrncpy(base, origName, sizeof(base));
    }

    char candidateName[kMaxFileNameLen + 1]{};
    for (int idx = 1; idx <= 999; ++idx)
    {
        snprintf(candidateName, sizeof(candidateName), "%s_%d%s", base, idx, ext);
        if (!joinPath(dstDir, candidateName, outPath, outSize))
        {
            return false;
        }
        if (!fs->exists(outPath))
        {
            return true;
        }
    }
    return false;
}

bool FileOpsWorker::startBatch(OpType op,
                               const char* srcDir,
                               const char* dstDir,
                               const BatchItem* items,
                               int itemCount,
                               const char* activeProjectPath)
{
    reset();
    if (fs == nullptr || scratch == nullptr || ioBuffer == nullptr || items == nullptr ||
        itemCount <= 0)
    {
        failWith("Invalid operation setup");
        return false;
    }

    opType = op;
    normalizePath(srcDir, srcRootDir, sizeof(srcRootDir));
    if (dstDir != nullptr)
    {
        normalizePath(dstDir, dstRootDir, sizeof(dstRootDir));
    }
    if (activeProjectPath != nullptr)
    {
        normalizePath(activeProjectPath, activeProjPath, sizeof(activeProjPath));
    }

    totalItems = (itemCount > kMaxBatchItems) ? kMaxBatchItems : itemCount;
    for (int i = 0; i < totalItems; ++i)
    {
        scratch->items[i] = items[i];
    }

    state = State::kRunning;
    currentItemIdx = 0;
    completedItems = 0;
    skippedItems = 0;
    scanActive = (op == OpType::kCopy);
    scanItemIdx = 0;
    totalBytes = 0;
    bytesCompleted = 0;
    totalFiles = 0;
    filesCompleted = 0;
    return true;
}

int FileOpsWorker::getDisplayCurrentIndex() const
{
    if (opType == OpType::kCopy && !scanActive && totalFiles > 0)
    {
        int cur = filesCompleted + 1;
        return (cur > totalFiles) ? totalFiles : cur;
    }
    if (totalItems <= 0)
    {
        return 0;
    }
    int cur = completedItems + 1;
    return (cur > totalItems) ? totalItems : cur;
}

int FileOpsWorker::getDisplayTotalCount() const
{
    if (opType == OpType::kCopy && !scanActive && totalFiles > 0)
    {
        return totalFiles;
    }
    return totalItems;
}

int FileOpsWorker::getProgressPercent() const
{
    if (totalItems <= 0)
    {
        return 0;
    }
    if (state == State::kDone)
    {
        return 100;
    }
    if (scanActive)
    {
        return 0;
    }

    if (opType == OpType::kCopy && totalBytes > 0)
    {
        uint64_t pct = (bytesCompleted * 100ULL) / totalBytes;
        if (pct > 99ULL)
        {
            pct = 99ULL;
        }
        return static_cast<int>(pct);
    }

    uint64_t basePct = (static_cast<uint64_t>(completedItems) * 100ULL) /
                       static_cast<uint64_t>(totalItems);
    if (basePct > 99ULL)
    {
        basePct = 99ULL;
    }
    return static_cast<int>(basePct);
}

void FileOpsWorker::cancel()
{
    if (state == State::kRunning || state == State::kWaitCollision)
    {
        closeOpenFiles();
        dirDepth = 0;
        state = State::kCancelled;
    }
}

void FileOpsWorker::closeOpenFiles()
{
    if (fs != nullptr)
    {
        if (srcFileHandle != nullptr)
        {
            fs->closeFile(srcFileHandle);
            srcFileHandle = nullptr;
        }
        if (dstFileHandle != nullptr)
        {
            fs->closeFile(dstFileHandle);
            dstFileHandle = nullptr;
        }
    }
    fileCopyActive = false;
    currentFileSize = 0;
    currentFileCopied = 0;
}

void FileOpsWorker::failWith(const char* msg)
{
    closeOpenFiles();
    dirDepth = 0;
    polystrncpy(errorMsg, (msg != nullptr) ? msg : "Operation failed", sizeof(errorMsg));
    state = State::kError;
}

void FileOpsWorker::step()
{
    if (state != State::kRunning)
    {
        return;
    }

    if (scanActive)
    {
        stepScan();
        return;
    }

    if (fileCopyActive)
    {
        stepActiveFileCopy();
        return;
    }

    if (dirDepth > 0)
    {
        stepDirCopy();
        return;
    }

    if (currentItemIdx >= totalItems)
    {
        state = State::kDone;
        return;
    }

    beginNextTopItem();
}

void FileOpsWorker::stepScan()
{
    // Scan up to 16 entries per tick to compute total byte size & file count quickly
    for (int budget = 0; budget < 16; ++budget)
    {
        if (dirDepth > 0)
        {
            DirFrame& frame = scratch->dirStack[dirDepth - 1];
            if (!fs->nesDir_open(frame.srcDir))
            {
                --dirDepth;
                continue;
            }

            char entryName[kMaxFilePathLen + 1]{};
            uint8_t isDir = 0;
            uint8_t isHid = 0;
            uint32_t entrySize = 0;
            uint32_t idx = 0;
            bool found = false;

            while (fs->nesDir_readItem(entryName, &isDir, &isHid, &entrySize))
            {
                if (strcmp(entryName, ".") == 0 || strcmp(entryName, "..") == 0)
                {
                    continue;
                }
                if (idx == frame.entryIndex)
                {
                    found = true;
                    break;
                }
                ++idx;
            }
            fs->nesDir_close();

            if (!found)
            {
                --dirDepth;
                continue;
            }

            ++frame.entryIndex;

            if (isDir)
            {
                if (dirDepth < kMaxDirDepth)
                {
                    char childSrc[kMaxFilePathLen + 1];
                    if (joinPath(frame.srcDir, entryName, childSrc, sizeof(childSrc)))
                    {
                        DirFrame& nextFrame = scratch->dirStack[dirDepth++];
                        polystrncpy(nextFrame.srcDir, childSrc, sizeof(nextFrame.srcDir));
                        nextFrame.dstDir[0] = '\0';
                        nextFrame.entryIndex = 0;
                    }
                }
            }
            else
            {
                totalBytes += static_cast<uint64_t>(entrySize);
                ++totalFiles;
            }
            continue;
        }

        if (scanItemIdx >= totalItems)
        {
            scanActive = false;
            return;
        }

        const BatchItem& item = scratch->items[scanItemIdx++];
        char itemPath[kMaxFilePathLen + 1];
        if (!joinPath(srcRootDir, item.name, itemPath, sizeof(itemPath)))
        {
            continue;
        }

        if (item.isFolder)
        {
            if (!isSelfOrDescendant(itemPath, dstRootDir))
            {
                dirDepth = 1;
                polystrncpy(scratch->dirStack[0].srcDir, itemPath, sizeof(scratch->dirStack[0].srcDir));
                scratch->dirStack[0].dstDir[0] = '\0';
                scratch->dirStack[0].entryIndex = 0;
            }
        }
        else
        {
            totalBytes += static_cast<uint64_t>(fs->getFileSize(itemPath));
            ++totalFiles;
        }
    }
}

void FileOpsWorker::beginNextTopItem()
{
    const BatchItem& item = scratch->items[currentItemIdx];
    polystrncpy(pendingItemName, item.name, sizeof(pendingItemName));
    pendingIsFolder = item.isFolder;
    polystrncpy(currentItemLabel, item.name, sizeof(currentItemLabel));

    if (!joinPath(srcRootDir, item.name, pendingSrcPath, sizeof(pendingSrcPath)))
    {
        failWith("Source path too long");
        return;
    }

    if (opType == OpType::kDelete)
    {
        if (isProtectedPath(pendingSrcPath, activeProjPath))
        {
            ++skippedItems;
            ++completedItems;
            ++currentItemIdx;
            return;
        }

        bool ok = item.isFolder ? fs->removeDirWithFiles(pendingSrcPath) : fs->remove(pendingSrcPath);
        if (!ok)
        {
            failWith("Delete failed");
            return;
        }

        ++completedItems;
        ++currentItemIdx;
        return;
    }

    // Copy or Move
    if (opType == OpType::kMove && isProtectedPath(pendingSrcPath, activeProjPath))
    {
        failWith("Protected item");
        return;
    }

    if (item.isFolder && isSelfOrDescendant(pendingSrcPath, dstRootDir))
    {
        failWith("Cannot copy/move into self");
        return;
    }

    if (!joinPath(dstRootDir, item.name, pendingDstPath, sizeof(pendingDstPath)))
    {
        failWith("Target path too long");
        return;
    }

    // Copying onto exact same path (e.g. same-dir Paste) -> auto-rename or collision
    const bool samePath = (strcasecmp(pendingSrcPath, pendingDstPath) == 0);
    if (opType == OpType::kMove && samePath)
    {
        ++skippedItems;
        ++completedItems;
        ++currentItemIdx;
        return;
    }

    if (fs->exists(pendingDstPath))
    {
        if (samePath && opType == OpType::kCopy)
        {
            // Same-directory duplicate: auto-rename immediately
            char renamedDst[kMaxFilePathLen + 1];
            if (!makeAutoRenamePath(fs,
                                    dstRootDir,
                                    pendingItemName,
                                    pendingIsFolder,
                                    renamedDst,
                                    sizeof(renamedDst)))
            {
                failWith("Auto-rename failed");
                return;
            }
            polystrncpy(pendingDstPath, renamedDst, sizeof(pendingDstPath));
            executeResolvedTopItem(pendingSrcPath, pendingDstPath, pendingIsFolder);
            return;
        }

        if (stickyCollisionAction != CollisionAction::kNone)
        {
            resolveCollision(stickyCollisionAction, true);
            return;
        }

        polystrncpy(collisionTargetName, pendingItemName, sizeof(collisionTargetName));
        state = State::kWaitCollision;
        return;
    }

    executeResolvedTopItem(pendingSrcPath, pendingDstPath, pendingIsFolder);
}

void FileOpsWorker::resolveCollision(CollisionAction action, bool applyToAll)
{
    if (state != State::kWaitCollision && state != State::kRunning)
    {
        return;
    }

    if (action == CollisionAction::kCancel)
    {
        cancel();
        return;
    }

    if (applyToAll)
    {
        stickyCollisionAction = action;
    }

    state = State::kRunning;

    if (action == CollisionAction::kSkip)
    {
        if (opType == OpType::kCopy && !pendingIsFolder)
        {
            bytesCompleted += static_cast<uint64_t>(fs->getFileSize(pendingSrcPath));
            ++filesCompleted;
        }
        ++skippedItems;
        ++completedItems;
        ++currentItemIdx;
        return;
    }

    if (action == CollisionAction::kAutoRename)
    {
        char renamedDst[kMaxFilePathLen + 1];
        if (!makeAutoRenamePath(fs,
                                dstRootDir,
                                pendingItemName,
                                pendingIsFolder,
                                renamedDst,
                                sizeof(renamedDst)))
        {
            failWith("Auto-rename failed");
            return;
        }
        polystrncpy(pendingDstPath, renamedDst, sizeof(pendingDstPath));
        executeResolvedTopItem(pendingSrcPath, pendingDstPath, pendingIsFolder);
        return;
    }

    // Overwrite
    if (isProtectedPath(pendingDstPath, activeProjPath))
    {
        failWith("Target is protected");
        return;
    }

    if (opType == OpType::kMove)
    {
        // FatFs f_rename fails if destination exists; remove destination first
        if (pendingIsFolder)
        {
            fs->removeDirWithFiles(pendingDstPath);
        }
        else
        {
            fs->remove(pendingDstPath);
        }
    }
    else if (opType == OpType::kCopy && !pendingIsFolder)
    {
        fs->remove(pendingDstPath);
    }

    executeResolvedTopItem(pendingSrcPath, pendingDstPath, pendingIsFolder);
}

void FileOpsWorker::executeResolvedTopItem(const char* srcPath, const char* dstPath, bool isFolder)
{
    if (opType == OpType::kMove)
    {
        if (!fs->rename(srcPath, dstPath))
        {
            failWith("Move failed");
            return;
        }
        if (isFolder)
        {
            newFileManager.updateProjectFileMetadata(dstPath);
        }
        else
        {
            newFileManager.updatePtiInstrumentName(dstPath);
        }
        ++completedItems;
        ++currentItemIdx;
        return;
    }

    // Copy
    if (!isFolder)
    {
        if (!startFileCopy(srcPath, dstPath))
        {
            failWith("File copy failed");
            return;
        }
        return;
    }

    // Folder copy: create target folder and push iterative traversal frame
    if (!fs->exists(dstPath) && !fs->mkdir(0, dstPath, true))
    {
        failWith("Create folder failed");
        return;
    }

    dirDepth = 1;
    polystrncpy(scratch->dirStack[0].srcDir, srcPath, sizeof(scratch->dirStack[0].srcDir));
    polystrncpy(scratch->dirStack[0].dstDir, dstPath, sizeof(scratch->dirStack[0].dstDir));
    scratch->dirStack[0].entryIndex = 0;
}

bool FileOpsWorker::startFileCopy(const char* srcPath, const char* dstPath)
{
    closeOpenFiles();

    auto [srcOk, srcH] = fs->openFile(srcPath, FileSystem::kRead);
    if (!srcOk || srcH == nullptr)
    {
        return false;
    }

    if (fs->exists(dstPath))
    {
        fs->remove(dstPath);
    }

    auto [dstOk, dstH] = fs->openFile(dstPath, FileSystem::kWriteCreateNew);
    if (!dstOk || dstH == nullptr)
    {
        fs->closeFile(srcH);
        return false;
    }

    srcFileHandle = srcH;
    dstFileHandle = dstH;
    currentFileSize = fs->getFileSize(srcFileHandle);
    currentFileCopied = 0;
    fileCopyActive = true;
    return true;
}

void FileOpsWorker::stepActiveFileCopy()
{
    int32_t bytesRead = fs->readFile(ioBuffer, kCopyIoBufBytes, srcFileHandle);
    if (bytesRead < 0)
    {
        failWith("Read error");
        return;
    }

    if (bytesRead > 0)
    {
        int32_t bytesWritten = fs->writeFile(ioBuffer, static_cast<size_t>(bytesRead), dstFileHandle);
        if (bytesWritten != bytesRead)
        {
            failWith("Write error (SD full?)");
            return;
        }
        currentFileCopied += static_cast<uint64_t>(bytesRead);
        bytesCompleted += static_cast<uint64_t>(bytesRead);
    }

    if (bytesRead < static_cast<int32_t>(kCopyIoBufBytes))
    {
        closeOpenFiles();
        ++filesCompleted;
        if (dirDepth == 0)
        {
            newFileManager.updatePtiInstrumentName(pendingDstPath);
            ++completedItems;
            ++currentItemIdx;
        }
    }
}

void FileOpsWorker::stepDirCopy()
{
    DirFrame& frame = scratch->dirStack[dirDepth - 1];

    if (!fs->nesDir_open(frame.srcDir))
    {
        // Finished or unreadable directory: pop frame
        --dirDepth;
        if (dirDepth == 0)
        {
            ++completedItems;
            ++currentItemIdx;
        }
        return;
    }

    char entryName[kMaxFilePathLen + 1]{};
    uint8_t isDir = 0;
    uint8_t isHid = 0;
    uint32_t entrySize = 0;
    uint32_t idx = 0;
    bool found = false;

    while (fs->nesDir_readItem(entryName, &isDir, &isHid, &entrySize))
    {
        if (strcmp(entryName, ".") == 0 || strcmp(entryName, "..") == 0)
        {
            continue;
        }
        if (idx == frame.entryIndex)
        {
            found = true;
            break;
        }
        ++idx;
    }
    fs->nesDir_close();

    if (!found)
    {
        newFileManager.updateProjectFileMetadata(frame.dstDir);
        --dirDepth;
        if (dirDepth == 0)
        {
            ++completedItems;
            ++currentItemIdx;
        }
        return;
    }

    ++frame.entryIndex;

    char childSrc[kMaxFilePathLen + 1];
    char childDst[kMaxFilePathLen + 1];
    if (!joinPath(frame.srcDir, entryName, childSrc, sizeof(childSrc)) ||
        !joinPath(frame.dstDir, entryName, childDst, sizeof(childDst)))
    {
        failWith("Nested path too long");
        return;
    }

    polystrncpy(currentItemLabel, entryName, sizeof(currentItemLabel));

    if (isDir)
    {
        if (dirDepth >= kMaxDirDepth)
        {
            failWith("Folder nesting too deep");
            return;
        }
        if (!fs->exists(childDst) && !fs->mkdir(0, childDst, true))
        {
            failWith("Create subfolder failed");
            return;
        }
        DirFrame& nextFrame = scratch->dirStack[dirDepth++];
        polystrncpy(nextFrame.srcDir, childSrc, sizeof(nextFrame.srcDir));
        polystrncpy(nextFrame.dstDir, childDst, sizeof(nextFrame.dstDir));
        nextFrame.entryIndex = 0;
    }
    else
    {
        if (!startFileCopy(childSrc, childDst))
        {
            failWith("Subfile copy failed");
            return;
        }
    }
}
