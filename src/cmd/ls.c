#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include <io.h>

typedef struct {
    char name[MAX_PATH];
    int isDir;
} Entry;

int cmd_ls(int argc, char *argv[]) {
    char searchPath[MAX_PATH];
    if (argc < 2) {
        snprintf(searchPath, sizeof(searchPath), "*");
    } else {
        snprintf(searchPath, sizeof(searchPath), "%s\\*", argv[1]);
    }

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);

    if (hFind == INVALID_HANDLE_VALUE) {
        printf("ls: cannot access '%s'\n", argc < 2 ? "." : argv[1]);
        return 1;
    }

    static Entry entries[MAX_ENTRIES];
    int count = 0;
    size_t maxLen = 0;

    do {
        // skip "." and ".." like real ls does by default
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (count >= MAX_ENTRIES) break;

        strncpy(entries[count].name, fd.cFileName, MAX_PATH - 1);
        entries[count].name[MAX_PATH - 1] = '\0';
        entries[count].isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        size_t len = strlen(entries[count].name);
        if (len > maxLen) maxLen = len;
        count++;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int termWidth = 80; // fallback if the query fails
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        termWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }

    int colWidth = (int)maxLen + 2; // +2 for spacing between columns
    int maxCols = termWidth / colWidth;
    if (maxCols < 1) maxCols = 1;

    // Only colorize directories when stdout is an actual console - if
    // it's been redirected to a file or piped into another command
    // (`ls > out.txt`, `ls | findstr foo`), _isatty() comes back false
    // and out.txt/findstr get plain text instead of raw ANSI escapes.
    int colorize = _isatty(_fileno(stdout));

    int col = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].isDir && colorize) {
            printf("\x1b[1;34m%-*s\x1b[0m", colWidth, entries[i].name);
        } else {
            printf("%-*s", colWidth, entries[i].name);
        }

        col++;
        if (col >= maxCols) {
            printf("\n");
            col = 0;
        }
    }
    if (col != 0) printf("\n");
    return 0;
}
