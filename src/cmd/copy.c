// Code for copying files and directories

#include "command.h"

// returns a pointer to the filename portion of a path (everything after the last '\' or '/')
static const char *basename_of(const char *path) {
    const char *lastSlash = strrchr(path, '\\');
    const char *lastFwd   = strrchr(path, '/');
    if (lastFwd && (!lastSlash || lastFwd > lastSlash)) {
        lastSlash = lastFwd;
    }
    return lastSlash ? lastSlash + 1 : path;
}

// recursively copies srcDir into destDir (destDir is created if missing)
static int copyDirRecursive(const char *srcDir, const char *destDir) {
    if (!CreateDirectoryA(destDir, NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            printf("cp: cannot create '%s'\n", destDir);
            return 0;
        }
    }

    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", srcDir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("cp: cannot access '%s'\n", srcDir);
        return 0;
    }

    int ok = 1;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        char srcPath[MAX_PATH];
        char destPath[MAX_PATH];
        snprintf(srcPath, sizeof(srcPath), "%s\\%s", srcDir, fd.cFileName);
        snprintf(destPath, sizeof(destPath), "%s\\%s", destDir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copyDirRecursive(srcPath, destPath)) {
                ok = 0;
            }
        } else {
            if (!CopyFileA(srcPath, destPath, FALSE)) {
                printf("cp: cannot copy '%s'\n", srcPath);
                ok = 0;
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    return ok;
}

int cmd_cp(int argc, char *argv[]) {
    if (argc < 3) {
        printf("cp: usage [cp <source> <destination>]\n");
        return 1;
    }

    char *src = argv[1];
    char *dest = argv[2];
    char destBuf[MAX_PATH];

    DWORD srcAttr = GetFileAttributesA(src);
    if (srcAttr == INVALID_FILE_ATTRIBUTES) {
        printf("cp: '%s' not found\n", src);
        return 1;
    }

    DWORD destAttr = GetFileAttributesA(dest);
    int destIsExistingDir = (destAttr != INVALID_FILE_ATTRIBUTES) &&
                             (destAttr & FILE_ATTRIBUTE_DIRECTORY);

    if (srcAttr & FILE_ATTRIBUTE_DIRECTORY) {
        if (destIsExistingDir) {
            snprintf(destBuf, sizeof(destBuf), "%s\\%s", dest, basename_of(src));
            dest = destBuf;
        }

        int ok = copyDirRecursive(src, dest);
        if (ok) {
            printf("'%s' copied to '%s'\n", src, dest);
        }
        return ok ? 0 : 1;
    }

    // copying a single file — existing behavior
    if (destIsExistingDir) {
        snprintf(destBuf, sizeof(destBuf), "%s\\%s", dest, basename_of(src));
        dest = destBuf;
    }

    if (!CopyFileA(src, dest, FALSE)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            printf("cp: '%s' not found\n", src);
        } else if (err == ERROR_ACCESS_DENIED) {
            printf("cp: cannot copy '%s' - access denied or destination is in use\n", src);
        } else {
            printf("cp: cannot copy '%s' to '%s'\n", src, dest);
        }
        return 1;
    }
    return 0;
}
