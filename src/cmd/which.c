// which - say what a command name would actually run

#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include "../commands.h"
#include "../exec.h"

int cmd_which(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "which: usage [which <command>...]\n");
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        const char *name = argv[i];

        // Built-ins win over anything on PATH - the shell checks them
        // first when running a command, so report them first too.
        if (commandsFind(name)) {
            printf("%s: shell built-in command\n", name);
            continue;
        }

        // Same lookup the shell uses to run external programs (PATH,
        // PATHEXT, and so on), so the answer matches what would run.
        char path[MAX_PATH];
        if (execFindOnPath(name, path, sizeof(path))) {
            printf("%s\n", path);
        } else {
            fprintf(stderr, "which: no '%s' in PATH\n", name);
            status = 1;
        }
    }
    return status;
}
