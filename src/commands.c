#include "commands.h"
#include <string.h>

const CommandInfo COMMAND_TABLE[] = {
    { "echo",  "Basic", "Print text back to the screen" },
    { "date",  "Basic", "Show the current date and time" },
    { "exit",  "Basic", "Quit the shell" },
    { "clear", "Basic", "Clear the screen and scrollback" },
    { "clr",   "Basic", "Alias for clear" },
    { "help",  "Basic", "Show this interactive help menu" },
    { "which", "Basic", "Show what a command name runs: built-in or path" },

    { "cd",    "File & Directory", "Change the current directory" },
    { "ls",    "File & Directory", "List files and folders in the current directory" },
    { "blank", "File & Directory", "Create a new empty file" },
    { "mkdir", "File & Directory", "Create a new directory" },
    { "rmdir", "File & Directory", "Remove a directory" },
    { "rm",    "File & Directory", "Remove a file" },
    { "mv",    "File & Directory", "Move or rename a file" },
    { "cp",    "File & Directory", "Copy a file" },
    { "copy",  "File & Directory", "Alias for cp" },
    { "del",   "File & Directory", "Alias for rm" },
    { "pcd",   "File & Directory", "Print the current/working directory" },

    { "cat",   "Text", "Print files (or piped input) to the screen" },
    { "head",  "Text", "Show the first lines of a file (-n <count>)" },
    { "tail",  "Text", "Show the last lines of a file (-n <count>, -f to follow)" },
    { "grep",  "Text", "Search for lines matching a pattern (-i -n -r -v -c ...)" },
};

const size_t COMMAND_TABLE_COUNT = sizeof(COMMAND_TABLE) / sizeof(COMMAND_TABLE[0]);

size_t commandsCollectCategories(const char *out[COMMAND_MAX_CATEGORIES]) {
    size_t count = 0;
    for (size_t i = 0; i < COMMAND_TABLE_COUNT && count < COMMAND_MAX_CATEGORIES; i++) {
        int seen = 0;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(out[j], COMMAND_TABLE[i].category) == 0) { seen = 1; break; }
        }
        if (!seen) out[count++] = COMMAND_TABLE[i].category;
    }
    return count;
}

const CommandInfo *commandsNthInCategory(const char *category, int n) {
    if (n < 1) return NULL;
    int seen = 0;
    for (size_t i = 0; i < COMMAND_TABLE_COUNT; i++) {
        if (strcmp(COMMAND_TABLE[i].category, category) == 0) {
            seen++;
            if (seen == n) return &COMMAND_TABLE[i];
        }
    }
    return NULL;
}

const CommandInfo *commandsFind(const char *name) {
    for (size_t i = 0; i < COMMAND_TABLE_COUNT; i++) {
        if (strcmp(COMMAND_TABLE[i].name, name) == 0) return &COMMAND_TABLE[i];
    }
    return NULL;
}
