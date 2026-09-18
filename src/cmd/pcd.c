#include "command.h"

int cmd_pcd(int argc, char *argv[]) {
    (void)argc; // pcd takes no arguments
    (void)argv;

    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd) == 0) {
        printf("pcd : cannot get the current directory");
        return 1;
    }

    printf("%s\n", cwd);
    return 0;
}
