#include "command.h"

int cmd_cd(int argc, char *argv[]) {
    if (argc < 2) {
        // no argument - go to the user's home directory
        char home[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
        if (n == 0 || n > sizeof(home)) {
            printf("cd: could not resolve home directory.\n");
            return 1;
        }
        if (!SetCurrentDirectoryA(home)) {
            printf("cd: cannot access (%s).\n", home);
            return 1;
        }
        return 0;
    }

    if (!SetCurrentDirectoryA(argv[1])) {
        printf("cd: cannot find path (%s).\n", argv[1]);
        return 1;
    }
    return 0;
}
