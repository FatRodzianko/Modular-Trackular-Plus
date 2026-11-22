#include "fsl_sd.h"
#include <stdio.h>
#include <string.h>

#include "SD.h"
#include "ctype.h"

#include "diskio.h"
#include "fsl_sd_disk.h"

SdCard SD;

void reportSdError(const char*, uint16_t)
{
}

SdFile SdCard::open(const char* path, uint8_t oflag)
{
    SdFile tmpFile;
    tmpFile.open(path, oflag);
    return tmpFile;
}

bool SdCard::rename(const char* oldPath, const char* newPath)
{
    FRESULT error = f_rename(oldPath, newPath);
    if (error)
    {
        reportSdError("rename - failed", error);
        return false;
    }

    return true;
}

bool SdCard::mkdir(uint8_t hidden, const char* path, bool pFlag)
{
    FRESULT error = f_mkdir(path);
    if (error)
    {
        if (error == FR_EXIST)
        {
            reportSdError("create dir - exist", error);
        }
        else
        {
            reportSdError("create dir - failed", error);
            return false;
        }
    }

    if (hidden)
    {
        f_chmod(path, AM_HID, AM_HID);
    }

    return true;
}

bool SdCard::remove(const char* path)
{
    FRESULT error = f_unlink(path);
    if (error)
    {
        if (error == FR_NO_FILE)
        {
            return false;
        }
        else if (error == FR_LOCKED)
        {
            reportSdError("remove failed - file locked", error);
            return false;
        }
        else
        {
            reportSdError("remove failed", error);
            return false;
        }
    }
    return true;
}

bool SdCard::removeDirWithFiles(const char* path)
{
    char buff[256];
    FILINFO fno;
    strcpy(buff, path);

    FRESULT error = delete_node(buff, 255, &fno);
    if (error)
    {
        reportSdError("remove dir with files failed", error);
        return false;
    }
    return true;
}

bool SdCard::removeOnlyDirFiles(const char* path)
{
    char buff[256];
    FILINFO fno;
    strcpy(buff, path);

    FRESULT error = delete_node((char*)path, 255, &fno, false);

    if (error)
    {
        reportSdError("remove dir files only failed", error);
        return false;
    }
    return true;
}

FRESULT SdCard::delete_node(TCHAR* path,  /* Path name buffer with the sub-directory to delete */
                            UINT sz_buff, /* Size of path name buffer (items) */
                            FILINFO* fno, /* Name read buffer */
                            bool removeDir)
{
    UINT i, j;
    FRESULT fr;
    DIR dir;

    fr = f_opendir(&dir, path); /* Open the directory */
    if (fr != FR_OK)
        return fr;

    for (i = 0; path[i]; i++)
        ; /* Get current path length */
    path[i++] = _T('/');

    for (;;)
    {
        fr = f_readdir(&dir, fno); /* Get a directory item */
        if (fr != FR_OK || !fno->fname[0])
            break; /* End of directory? */
        j = 0;
        do
        { /* Make a path name */
            if (i + j >= sz_buff)
            { /* Buffer over flow? */
                fr = (FRESULT)100;
                break; /* Fails with 100 when buffer overflow */
            }
            path[i + j] = fno->fname[j];
        } while (fno->fname[j++]);
        if (fno->fattrib & AM_DIR)
        { /* Item is a directory */
            fr = delete_node(path, sz_buff, fno, true);
        }
        else
        { /* Item is a file */
            fr = f_unlink(path);
        }
        if (fr != FR_OK)
            break;
    }

    path[--i] = 0; /* Restore the path name */
    f_closedir(&dir);

    if (fr == FR_OK && removeDir)
        fr = f_unlink(path); /* Delete the empty directory */
    return fr;
}

uint32_t SdCard::clusterCount()
{
    return 0; // todo (g_fileSystem.n_fatent - 2);
}

uint16_t SdCard::sectorsPerCluster()
{
    return 0; // todo g_fileSystem.csize;
}

uint32_t SdCard::freeClusterCount()
{
    FATFS* fs;
    DWORD fre_clust; //, fre_sect, tot_sect;

    /* Get volume information and free clusters of drive 1 */
    FRESULT res = f_getfree("2:", &fre_clust, &fs);
    if (res)
        return 0;

    /* Get total sectors and free sectors */
    // tot_sect = (fs->n_fatent - 2) * fs->csize ;
    // fre_sect = fre_clust * fs->csize;
    return fre_clust;
}

//==============================================================================================================================
//==============================================================================================================================
//==============================================================================================================================
//==============================================================================================================================
//==============================================================================================================================
// SDDIR

// static char print_string[100];

bool SdDir::open(const char* path, uint8_t oflag)
{
    close();

    //---------------------------------------------
    //  monitorowanie ilosci otwartych jednoczenie folderów
    //	int opened_files_count = get_lock_count();
    //	if(opened_files_count > 1)
    //	{
    //		sprintf(print_string, "%d dir open: %s", opened_files_count, path);
    //		cpplog_println(print_string);
    //		cppserial_println(print_string);
    //	}
    //---------------------------------------------

    directory = new DIR;
    FRESULT error = f_opendir(directory, path);
    if (error)
    {
        reportSdError("dir open - failed", error);
        close();
        return false;
    }

    strncpy(dir_path, path, 255);

    return true;
}

bool SdDir::close()
{
    FRESULT error = f_closedir(directory);

    //---------------------------------------------
    //  monitorowanie ilosci otwartych jednoczenie folderów
    //	int opened_files_count = get_lock_count();
    //	if(opened_files_count > 1)
    //	{
    //		sprintf(print_string, "%d dir close: %s", opened_files_count, dir_path);
    //		cpplog_println(print_string);
    //		cppserial_println(print_string);
    //	}
    //---------------------------------------------

    delete directory;
    directory = nullptr;
    memset(dir_path, 0, 255);

    if (error == FR_INVALID_OBJECT)
    {
        return false;
    }
    else if (error)
    {
        reportSdError("dir close - failed", error);
        return false;
    }

    return true;
}

uint16_t SdDir::countFilesInSubDirectory(const char* dir_name, enListFileFilter filter)
{
    char* const path = new char[strlen(dir_name) + strlen(dir_path) + 2];
    FRESULT result;
    DIR dir;
    FILINFO file_info;
    uint16_t count = 0;

    strcpy(path, dir_path);
    strcat(path, "/");
    strcat(path, dir_name);

    result = f_opendir(&dir, path);
    if (result != FR_OK)
    {
        delete[] path;
        return 0;
    }

    f_readdir(&dir, nullptr);

    do
    {
        result = f_readdir(&dir, &file_info);

        if (file_info.fname[0] == '\0')
        {
            break;
        }
        if (!filterFileEntry(file_info, filter))
        {
            continue;
        }

        count++;
    } while (true);

    delete[] path;
    f_closedir(&dir);
    return count;
}

bool SdDir::filterFileEntry(const FILINFO& fno, enListFileFilter filter)
{
    if ((fno.fattrib & AM_HID) || fno.fname[0] == '.') // ukryty
    {
        return false;
    }

    if ((fno.fattrib & AM_DIR) != 0 && filter != enListFileFilter_folder &&
        filter != enListFileFilter_all)
    {
        return false;
    }

    uint8_t filenameLen = strlen(fno.fname);

    switch (filter)
    {
        case enListFileFilter_all:
            // do nothing
            return true;

        case enListFileFilter_folder:
            return (fno.fattrib & AM_DIR) != 0 && !isMacMetaFolder((FILINFO*)&fno);

        case enListFileFilter_wave_inst:
        case enListFileFilter_wave:
        {
            if (filenameLen < 5)
            {
                return false;
            }

            if (((fno.fname[filenameLen - 1] != 'V') && (fno.fname[filenameLen - 1] != 'v')) ||
                ((fno.fname[filenameLen - 2] != 'A') && (fno.fname[filenameLen - 2] != 'a')) ||
                ((fno.fname[filenameLen - 3] != 'W') && (fno.fname[filenameLen - 3] != 'w')) ||
                (fno.fname[filenameLen - 4] != '.'))
            {
                if (filter == enListFileFilter_wave_inst)
                {
                    if (((fno.fname[filenameLen - 1] != 'i') &&
                         (fno.fname[filenameLen - 1] != 'I')) ||
                        ((fno.fname[filenameLen - 2] != 't') &&
                         (fno.fname[filenameLen - 2] != 'T')) ||
                        ((fno.fname[filenameLen - 3] != 'p') &&
                         (fno.fname[filenameLen - 3] != 'P')) ||
                        (fno.fname[filenameLen - 4] != '.'))
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }
            }

            if ((strlen(dir_path) + filenameLen) > 253)
            {
                return false;
            }
            if (isMacMetaFile((FILINFO*)&fno))
            {
                return false;
            }
            return true;
        }
        case enListFileFilter_firmware:
        {
#ifdef TRACKER_MINI
            constexpr char kFirmwareFileNameStart[] = "TrackerMini";
#else
            constexpr char kFirmwareFileNameStart[] = "TrackerPlus";
#endif

            if (filenameLen < strlen(kFirmwareFileNameStart) + 4)
            {
                return false;
            }

            if (strncmp(&fno.fname[filenameLen - 4], ".ptf", 4) != 0)
            {
                return false;
            }

            if (strncmp(kFirmwareFileNameStart, fno.fname, strlen(kFirmwareFileNameStart)) != 0)
            {
                return false;
            }

            if (isMacMetaFile((FILINFO*)&fno))
            {
                return false;
            }

            return true;
        }
        case enListFileFilter_mod:
        {
            char lowCase[255];
            strcpy(lowCase, fno.fname);
            for (uint16_t a = 0; a < 255; a++)
            {
                lowCase[a] = tolower(lowCase[a]);
                if (lowCase[a] == 0)
                    break;
            }

            return strstr(lowCase, ".mod\0") != NULL || strstr(lowCase, ".it\0") != NULL;
        }
    }

    return false;
}

// filter: 0-all, 1-folders only, 2-supported wav only, 3-ptf // always ignore hidden files //
// max_used_memory must be higher than 255
uint16_t SdDir::createFilesList(uint8_t start_line,
                                char** list,
                                uint8_t list_length,
                                uint16_t max_used_memory,
                                enListFileFilter filter,
                                uint16_t skip_count,
                                uint16_t max_files_in_subdir,
                                enListFileFilter folderSizeFilter)
{
    for (uint8_t i = 0; i < list_length; i++) // wyczyszczenie uzytej wczesniej pamieci
    {
        if (list[i] != nullptr)
        {
            delete list[i];
            list[i] = nullptr;
        }
    }

    uint8_t n = start_line;

    if (max_used_memory < 255)
    {
        reportSdError("create list - memory max used", max_used_memory);
        return n;
    }

    uint16_t memory_used = 0;
    FILINFO fno;
    FRESULT error;
    SdFile local_file;

    f_readdir(directory, nullptr);

    while (1)
    {
        if (n >= list_length)
            break;
        if (memory_used > max_used_memory - 255)
            break;

        error = f_readdir(directory, &fno);
        if (error)
        {
            reportSdError("create list - read dir item - failed", error);
            break;
        }

        else if (!fno.fname[0]) // koniec folderu
        {
            break;
        }

        if (!filterFileEntry(fno, filter))
        {
            continue;
        }

        if (skip_count > 0)
        {
            skip_count--;
            continue;
        }

        uint16_t len = strlen(fno.fname);

        if (fno.fattrib & AM_DIR)
            len += 2;
        else
            len += 1;

        list[n] = new char[len];
        memory_used += len;

        if (fno.fattrib & AM_DIR) // folder
        {
            strcpy(list[n], "/");
            strcat(list[n], fno.fname);
            int16_t files_count = countFilesInSubDirectory(fno.fname, folderSizeFilter);

            if (max_files_in_subdir != 0 && files_count > max_files_in_subdir)
            {
                uint16_t offset = 0;
                len += 4 + 2 * 5;

                while (files_count > 0 && n < list_length - 1)
                {
                    if (list[n] != nullptr)
                    {
                        delete list[n];
                        memory_used -= len;
                    }
                    list[n] = new char[len];
                    memory_used += len;
                    snprintf(list[n],
                             len,
                             "/%s (%d-%d)",
                             fno.fname,
                             offset + 1,
                             offset + max_files_in_subdir);
                    files_count -= max_files_in_subdir;

                    n++;
                    offset += max_files_in_subdir;
                }
                n--;
            }
        }
        else
        {
            strcpy(list[n], fno.fname);
        }

        n++;
    }

    return n;
}

uint16_t SdDir::createInstrumentsList(char** list, uint8_t list_length, uint32_t* sizeOut)
{
    uint8_t n = 0;
    FILINFO fno;
    FRESULT error;
    SdFile local_file;

    f_readdir(directory, nullptr);

    while (1)
    {
        if (n >= list_length)
            break;

        error = f_readdir(directory, &fno);
        if (error)
        {
            reportSdError("create instruments list - read dir item - failed", error);
            break;
        }
        else if (!fno.fname[0]) // koniec folderu
        {
            break;
        }

        if ((fno.fattrib & AM_HID) || fno.fname[0] == '.') // ukryty
        {
            continue;
        }

        if (fno.fattrib & AM_DIR)
            continue;

        int nameLen = strlen(fno.fname);

        if (nameLen < 5)
            continue;

        if (((fno.fname[nameLen - 1] != 'i') && (fno.fname[nameLen - 1] != 'I')) ||
            ((fno.fname[nameLen - 2] != 't') && (fno.fname[nameLen - 2] != 'T')) ||
            ((fno.fname[nameLen - 3] != 'p') && (fno.fname[nameLen - 3] != 'P')) ||
            (fno.fname[nameLen - 4] != '.'))
            continue;

        char* p = fno.fname;

        int idx1 = -1, idx2 = -1;

        if (*p >= 48 && *p < 58)
        {
            idx1 = (*p - 48);
        }
        else
        {
            continue;
        }

        p++;
        if (*p >= 48 && *p < 58)
        {
            idx2 = (*p - 48);
            p++;
        }

        if (*p != 32)
        {
            continue;
        }
        p++;

        if (idx2 >= 0)
        {
            idx1 = idx1 * 10 + idx2;
        }

        // if(nameLen - (p - fno.fname) > 31) continue; // max dlugosc nazwy

        if (idx1 < 1 || idx1 > 48)
        {
            continue;
        }

        idx1--;

        if (list[idx1] != nullptr)
        {
            continue;
        }

        int len = nameLen - (p - fno.fname) - 4;
        list[idx1] = new char[len + 1];
        strncpy(list[idx1], p, len);
        list[idx1][len] = 0;

        *sizeOut += fno.fsize;
        n++;
    }

    return n;
}

uint16_t SdDir::countContainedFiles()
{
    FILINFO fno;
    FRESULT error;
    SdFile local_file;
    uint8_t n = 0;

    f_readdir(directory, nullptr);

    while (1)
    {
        error = f_readdir(directory, &fno);
        if (error)
        {
            reportSdError("create list - read dir item - failed", error);
            break;
        }
        else if (!fno.fname[0]) // koniec folderu
        {
            break;
        }

        if ((fno.fattrib & AM_HID) || fno.fname[0] == '.') // ukryty
        {
            continue;
        }

        if ((fno.fattrib & AM_DIR) == 0) // JESLI NIE FOLDER TO SPRAWDZ CZY WAVE
        {
            uint8_t filenameLen = strlen(fno.fname);

            if (filenameLen < 5)
                continue;

            if (((fno.fname[filenameLen - 1] != 'V') && (fno.fname[filenameLen - 1] != 'v')) ||
                ((fno.fname[filenameLen - 2] != 'A') && (fno.fname[filenameLen - 2] != 'a')) ||
                ((fno.fname[filenameLen - 3] != 'W') && (fno.fname[filenameLen - 3] != 'w')) ||
                (fno.fname[filenameLen - 4] != '.'))
                continue;

            if ((strlen(dir_path) + filenameLen) > 253)
                continue;
        }

        n++;
    }

    return n;
}

FRESULT SdDir::readNextInOpenedDir(FILINFO* fno)
{
    FRESULT error = f_readdir(directory, fno);

    if (error)
        reportSdError("create list - read dir item - failed", error);

    return error;
}

bool SdDir::readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size)
{
    FILINFO fno;
    FRESULT error = f_readdir(directory, &fno);
    if (error)
    {
        reportSdError("read dir item - failed", error);
        return false;
    }
    else if (!fno.fname[0]) // koniec folderu
    {
        return false;
    }

    if (fno.fattrib & AM_DIR)
    {
        *isDir = 1;
    }
    else
    {
        *isDir = 0;
    }

    if (fno.fattrib & AM_HID || fno.fattrib & AM_SYS)
    {
        *isHid = 1;
    }
    else
    {
        *isHid = 0;
    }

    if (size != nullptr)
        *size = fno.fsize;

    strcpy(path, fno.fname);
    return true;
}

bool SdDir::isEndOfFolder(FILINFO* fno)
{
    return !fno->fname[0];
}

bool SdDir::isHidden(FILINFO* fno)
{
    return fno->fattrib & AM_HID;
}

bool SdDir::isFolder(FILINFO* fno)
{
    return fno->fattrib & AM_DIR;
}

bool SdDir::isMacMetaFile(FILINFO* fno)
{
    return (fno->fname[0] == '.' && fno->fname[1] == '_');
}

bool SdDir::isMacMetaFolder(FILINFO* fno)
{
    return strcmp(fno->fname, "__MACOSX") == 0;
}
