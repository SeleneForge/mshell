// Code for moving/renaming files and directories

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

int cmd_mv(int argc, char *argv[]) {
    if (argc < 3) {
        printf("mv: usage [mv <source> <destination>]\n");
        return 1;
    }

    char *src = argv[1];
    char *dest = argv[2];
    char destBuf[MAX_PATH];

    // if dest is an existing directory, move the file INTO it
    // using the source's own filename - same as Linux mv
    DWORD destAttr = GetFileAttributesA(dest);
    if (destAttr != INVALID_FILE_ATTRIBUTES && (destAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(destBuf, sizeof(destBuf), "%s\\%s", dest, basename_of(src));
        dest = destBuf;
    }

    if (!MoveFileA(src, dest)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            printf("mv: '%s' not found\n", src);
        } else if (err == ERROR_ALREADY_EXISTS) {
            printf("mv: '%s' already exists\n", dest);
        } else if (err == ERROR_ACCESS_DENIED) {
            printf("mv: cannot move '%s' - access denied or in use\n", src);
        } else {
            printf("mv: cannot move '%s' to '%s'\n", src, dest);
        }
        return 1;
    }
    return 0;
}
