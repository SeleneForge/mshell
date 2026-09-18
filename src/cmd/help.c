#include "command.h"
#include "../commands.h"
#include <stdlib.h>
#include <string.h>

// Minimal line reader for the help menu's own prompts. The shell puts
// the console in raw mode for its whole lifetime (see mode.c), so
// there's no OS-level echo or line editing here either - this only
// needs to handle typed characters, backspace, and Enter, which is
// plenty for picking a menu number. Returns the line length, or -1 if
// the user backed out (Esc / Ctrl+C) or the input stream closed.
static int readMenuLine(char *buf, size_t bufSize) {
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    size_t len = 0;
    INPUT_RECORD ir;
    DWORD read;

    while (ReadConsoleInputA(hStdin, &ir, 1, &read)) {
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        char c = ir.Event.KeyEvent.uChar.AsciiChar;

        if (vk == VK_ESCAPE || c == 3 /* Ctrl+C */) {
            printf("\r\n");
            return -1;
        }
        if (c == '\r') {
            buf[len] = '\0';
            printf("\r\n");
            return (int)len;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (c != 0 && len < bufSize - 1) {
            buf[len++] = c;
            putchar(c);
            fflush(stdout);
        }
    }

    return -1; // input stream failed/closed - bail out of help entirely
}

static int isQuit(const char *s) {
    return strcmp(s, "q") == 0 || strcmp(s, "quit") == 0 || strcmp(s, "exit") == 0;
}

static void printCategoryMenu(const char *const categories[], size_t count) {
    printf("\n=== MShell Help ===\n");
    printf("Pick a category:\n\n");
    for (size_t i = 0; i < count; i++) {
        printf("  %d) %s\n", (int)i + 1, categories[i]);
    }
    printf("\n  0) Exit help\n");
}

static void printCommandsInCategory(const char *category) {
    printf("\n=== %s ===\n\n", category);
    int n = 1;
    const CommandInfo *cmd;
    while ((cmd = commandsNthInCategory(category, n)) != NULL) {
        printf("  %d) %-6s - %s\n", n, cmd->name, cmd->description);
        n++;
    }
    printf("\n  0) Back\n");
}

static void runHelpMenu(void) {
    const char *categories[COMMAND_MAX_CATEGORIES];
    size_t categoryCount = commandsCollectCategories(categories);

    printCategoryMenu(categories, categoryCount);

    char line[64];
    for (;;) {
        printf("\nhelp> ");
        fflush(stdout);

        int n = readMenuLine(line, sizeof(line));
        if (n < 0) return;   // Esc/Ctrl+C/closed - leave help
        if (n == 0) continue; // bare Enter - just reprompt

        if (isQuit(line)) return;

        char *end;
        long choice = strtol(line, &end, 10);
        if (*end != '\0') {
            printf("Not a valid choice.\n");
            continue;
        }
        if (choice == 0) return; // "0" at the top level exits help
        if (choice < 1 || (size_t)choice > categoryCount) {
            printf("Not a valid choice.\n");
            continue;
        }

        const char *category = categories[choice - 1];
        printCommandsInCategory(category);

        // Inner loop: browsing commands within this category. Picking
        // one just prints its description again and stays here, so
        // you can look at several before going back or exiting.
        for (;;) {
            printf("\nhelp/%s> ", category);
            fflush(stdout);

            n = readMenuLine(line, sizeof(line));
            if (n < 0) return;
            if (n == 0) continue;
            if (isQuit(line)) return;

            long inner = strtol(line, &end, 10);
            if (*end != '\0') {
                printf("Not a valid choice.\n");
                continue;
            }
            if (inner == 0) break; // back to the category list

            const CommandInfo *cmd = commandsNthInCategory(category, (int)inner);
            if (!cmd) {
                printf("Not a valid choice.\n");
                continue;
            }
            printf("  %s - %s\n", cmd->name, cmd->description);
        }

        printCategoryMenu(categories, categoryCount); // fresh listing when back
    }
}

int cmd_help(int argc, char *argv[]) {
    if (argc > 1) {
        // Direct lookup: "help ls" skips the menu entirely.
        const CommandInfo *cmd = commandsFind(argv[1]);
        if (cmd) {
            printf("%s - %s\n", cmd->name, cmd->description);
        } else {
            printf("No help found for \"%s\".\n", argv[1]);
            return 1;
        }
        return 0;
    }

    runHelpMenu();
    return 0;
}
