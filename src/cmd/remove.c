// Code that anything to remove file/dir

#include "command.h"

int cmd_rm(int argc, char *argv[]) {
    if (argc < 2) {
        printf("rm: usage [rm <filename>].\n");
        return 1;
    }

    if (!DeleteFileA(argv[1])) {
        DWORD err = GetLastError();

        if (err == ERROR_FILE_NOT_FOUND) {
            printf("rm: '%s' not found!.\n", argv[1]);
        } else if (err == ERROR_ACCESS_DENIED) {
            printf("rm: '%s' is read-only or in use.\n", argv[1]);
        } else {
            printf("rm: '%s' cannot be removed!.\n", argv[1]);
        }
        return 1;
    }
    return 0;
}

// remove directory
int cmd_rmdir(int argc, char *argv[]) {
    if (argc < 2) {
        printf("rmdir: usage [rmdir <foldername>].\n");
        return 1;
    }

    if (!RemoveDirectoryA(argv[1])) {
        DWORD err = GetLastError();
        if (err == ERROR_DIR_NOT_EMPTY) {
            printf("rmdir: '%s' is not empty!.\n", argv[1]);
        } else if (err == ERROR_PATH_NOT_FOUND || err == ERROR_FILE_NOT_FOUND) {
            printf("rmdir: cannot find '%s'.\n", argv[1]);
        } else {
            printf("rmdir: '%s' cannot be removed!.\n", argv[1]);
        }
        return 1;
    }
    return 0;
}
