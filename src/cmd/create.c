// Code that anything to create file/dir

#include "command.h"

int cmd_blank(int argc, char *argv[]) {
    if (argc < 2) {
        printf("blank: usage [blank <filename>].\n");
        return 1;
    }

    HANDLE hFile = CreateFileA(
        argv[1],
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS) {
            printf("blank: '%s' already exist!.\n", argv[1]);
        } else {
            printf("blank: '%s' cannot be create!.\n", argv[1]);
        }
        return 1;
    }

    CloseHandle(hFile);
    return 0;
}

// make directory
int cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        printf("mkdir: usage [mkdir <foldername>].\n");
        return 1;
    }

    if (!CreateDirectoryA(argv[1], NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS) {
            printf("mkdir: '%s' already exist!.\n", argv[1]);
        } else if (err == ERROR_PATH_NOT_FOUND) {
            printf("mkdir: cannot create '%s' : parent directory doesnt exist.\n", argv[1]);
        } else {
            printf("mkdir: '%s' cannot be create!.\n", argv[1]);
        }
        return 1;
    }
    return 0;
}
