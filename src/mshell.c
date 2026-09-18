#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>

// Shell header
#include "mver.h"
#include "mode.h"
#include "tokenize.h"
#include "history.h"
#include "suggest.h"
#include "exec.h"
#include "chain.h"
#include "mshell.h"
#include "cmd/command.h"

// Ctrl+<letter> arrives as AsciiChar already reduced to its control code
// (same trick backspace relies on below: '\b' IS Ctrl+H, 0x08) - masking
// off the low 5 bits of the letter reproduces that code so we can name
// the shortcut by its letter instead of a magic number.
#define CTRL_KEY(k) ((k) & 0x1f)

int  isRunning = 1; // Flag to check shell running.

// Tries argv[0] as a built-in. Shared with pipeline.c so a pipeline stage
// ("ls | findstr foo") dispatches through the exact same table as a
// plain top-level command instead of keeping a second copy of this list
// that could quietly drift out of sync with this one. Returns -1 if
// argv[0] isn't a built-in at all; otherwise runs it and returns its
// exit status (0 = success) - see mshell.h.
int runBuiltin(int argc, char *argv[]) {

    // Basic

    if (strcmp(argv[0], "echo") == 0) {

        return cmd_echo(argc, argv);

    } else if (strcmp(argv[0], "date") == 0) {

        return cmd_date(argc, argv);

    } else if (strcmp(argv[0], "exit") == 0) {

        printf("Exit...\n");
        isRunning = 0;
        return 0;

    } else if (strcmp(argv[0], "clear") == 0 || strcmp(argv[0], "clr") == 0) {

        return cmd_clear(argc, argv);

    } else if (strcmp(argv[0], "help") == 0) {

        return cmd_help(argc, argv);

    //File/dir Commands

    } else if (strcmp(argv[0], "cd") == 0) {

        return cmd_cd(argc, argv);

    } else if (strcmp(argv[0], "ls") == 0) {

        return cmd_ls(argc, argv);

    } else if (strcmp(argv[0], "blank") == 0) {

        return cmd_blank(argc, argv);

    } else if (strcmp(argv[0], "mkdir") == 0) {

        return cmd_mkdir(argc, argv);

    } else if (strcmp(argv[0], "rmdir") == 0) {

        return cmd_rmdir(argc, argv);

    } else if (strcmp(argv[0], "rm") == 0) {

        return cmd_rm(argc, argv);

    } else if (strcmp(argv[0], "mv") == 0) {

        return cmd_mv(argc, argv);

    } else if (strcmp(argv[0], "cp") == 0) {

        return cmd_cp(argc, argv);

    } else if (strcmp(argv[0], "pcd") == 0) {

        return cmd_pcd(argc, argv);

    }

    return -1; // not a built-in - caller should try PATH
}

void runcmd(char *input, size_t inputCap) {

    char original[1024];
    strncpy(original, input, sizeof(original) - 1);
    original[sizeof(original) - 1] = '\0';

    char *argv[MAX_ARGS];
    int argc = tokenize(input, inputCap, argv);

    if (argc == 0) {
        // empty input - do nothing
        return;
    }

    if (chainHasOperators(argv, argc)) {
        CommandChain chain;
        char errMsg[128];
        if (!chainParse(argv, argc, &chain, errMsg, sizeof(errMsg))) {
            printf("mshell: %s\n", errMsg);
            return;
        }
        chainExecute(&chain);
        return;
    }

    if (runBuiltin(argc, argv) >= 0) {
        return;
    }

    // if command not found.
    char resolvedPath[MAX_PATH];
    if (execFindOnPath(argv[0], resolvedPath, sizeof(resolvedPath))) {

        execRun(resolvedPath, original);

    } else {
        printf("Unknown Command! : %s\n", argv[0]);
        if (argc > 1) {
            printf("  (parsed as command \"%s\" + %d argument%s - full line was: %s)\n",
                   argv[0], argc - 1, (argc - 1 == 1) ? "" : "s", original);
        }

        char suggestion[SUGGEST_MAX_LEN + 1];
        if (suggestCommand(argv[0], suggestion, sizeof(suggestion))) {
            printf("  Did you mean \"%s\"?\n", suggestion);
        }
    }
}

void printPrompt(void) {
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(sizeof(cwd), cwd);

    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

    //lime
    SetConsoleTextAttribute(console, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    printf("$MShell ");

    //cyan
    SetConsoleTextAttribute(console, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    printf("%s", cwd);

    // > -> white
    SetConsoleTextAttribute(console, FOREGROUND_RED |
                                     FOREGROUND_GREEN |
                                     FOREGROUND_BLUE);
    printf("> ");

    fflush(stdout);
}

static void redrawWholeLine(const char *input, size_t len, size_t cursor) {
    printf("\r\x1b[2K"); // \x1b[2K = erase the ENTIRE line, not just cursor-to-end
    printPrompt();
    if (len) printf("%.*s", (int)len, input); // print exactly len bytes - never
                                               // trust a NUL terminator to be in
                                               // the right place
    if (cursor < len) printf("\x1b[%zuD", len - cursor);
    fflush(stdout);
}

void runPrompt(void){
    enableRawMode();

    PrintVer();

    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);

    char input[1024]; // User Type input
    size_t len = 0;    // bytes currently in the buffer
    size_t cursor = 0; // where in the buffer the terminal cursor sits

    printPrompt();

    INPUT_RECORD ir;
    DWORD read;

    while (isRunning && ReadConsoleInputA(hStdin, &ir, 1, &read)){
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            redrawWholeLine(input, len, cursor);
            continue;
        }

        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown){continue;}

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        char c = ir.Event.KeyEvent.uChar.AsciiChar;

        WORD repeat = ir.Event.KeyEvent.wRepeatCount;
        if (repeat < 1) repeat = 1;

        if (vk == VK_LEFT) {
            if (cursor > 0) {
                size_t n = repeat;
                if (n > cursor) n = cursor;
                cursor -= n;
                redrawWholeLine(input, len, cursor);
            }
            continue;

        } else if (vk == VK_RIGHT) {
            if (cursor < len) {
                size_t n = repeat;
                size_t avail = len - cursor;
                if (n > avail) n = avail;
                cursor += n;
                redrawWholeLine(input, len, cursor);
            }
            continue;

        } else if (vk == VK_HOME) {
            if (cursor > 0) {
                cursor = 0;
                redrawWholeLine(input, len, cursor);
            }
            continue;

        } else if (vk == VK_END) {
            if (cursor < len) {
                cursor = len;
                redrawWholeLine(input, len, cursor);
            }
            continue;

        } else if (vk == VK_DELETE) {
            if (cursor < len) {
                size_t n = repeat;
                size_t avail = len - cursor;
                if (n > avail) n = avail;
                memmove(&input[cursor], &input[cursor + n], len - cursor - n);
                len -= n;
                input[len] = '\0';
                redrawWholeLine(input, len, cursor);
            }
            continue;

        } else if (vk == VK_UP) {
            char recalled[sizeof(input)];
            int n = -1;
            for (WORD i = 0; i < repeat; i++) {
                int r = historyUp(input, len, recalled, sizeof(recalled));
                if (r < 0) break; // hit the oldest entry - stop early
                n = r;
            }
            if (n >= 0) {
                memcpy(input, recalled, (size_t)n + 1);
                len = (size_t)n;
                cursor = len;
                redrawWholeLine(input, len, cursor);
            }
            continue;

        } else if (vk == VK_DOWN) {
            char recalled[sizeof(input)];
            int n = -1;
            for (WORD i = 0; i < repeat; i++) {
                int r = historyDown(recalled, sizeof(recalled));
                if (r < 0) break; // already back at the live line - stop early
                n = r;
            }
            if (n >= 0) {
                memcpy(input, recalled, (size_t)n + 1);
                len = (size_t)n;
                cursor = len;
                redrawWholeLine(input, len, cursor);
            }
            continue;

        } else if (vk == 'C' &&
                   (ir.Event.KeyEvent.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))) {
            if (len > 0) {
                len = 0;
                cursor = 0;
                input[0] = '\0';
                redrawWholeLine(input, len, cursor);
            }
            continue;
        }

        if (c == 0) continue; // other non-character keys (shift, ctrl, alt...)

        if (c == '\r'){  //enter
            input[len] = '\0';
            historyPush(input); // must happen before runcmd(): tokenize() splits
                                 // the buffer in place, so anything after that
                                 // only has the first token left
            printf("\r\n"); // \r first: guarantee the new row starts at column 0
                             // even if the cursor had drifted off its expected spot
            runcmd(input, sizeof(input));

            if (!isRunning) break;

            len = 0;
            cursor = 0;
            printf("\x1b[J");
            printPrompt();

        } else if ( c == '\b') {  //backspace
            if (cursor > 0){
                size_t n = repeat;
                if (n > cursor) n = cursor;
                memmove(&input[cursor - n], &input[cursor], len - cursor);
                len -= n;
                cursor -= n;
                input[len] = '\0';
                redrawWholeLine(input, len, cursor);
            }
        } else if (c == CTRL_KEY('w')) { // delete the word behind the cursor
            if (cursor > 0) {
                size_t end = cursor; // where the deleted range stops
                for (WORD i = 0; i < repeat && cursor > 0; i++) {
                    while (cursor > 0 && input[cursor - 1] == ' ') cursor--; // skip trailing spaces
                    while (cursor > 0 && input[cursor - 1] != ' ') cursor--; // skip the word itself
                }
                size_t n = end - cursor;
                if (n > 0) {
                    memmove(&input[cursor], &input[end], len - end);
                    len -= n;
                    input[len] = '\0';
                    redrawWholeLine(input, len, cursor);
                }
            }
        } else if (c == CTRL_KEY('u')) { // kill from the cursor to the start of the line
            if (cursor > 0) {
                memmove(&input[0], &input[cursor], len - cursor);
                len -= cursor;
                cursor = 0;
                input[len] = '\0';
                redrawWholeLine(input, len, cursor);
            }
        } else if (c == CTRL_KEY('k')) { // kill from the cursor to the end of the line
            if (cursor < len) {
                len = cursor;
                input[len] = '\0';
                redrawWholeLine(input, len, cursor);
            }
        } else if (len < sizeof(input) - 1){
            size_t n = repeat;
            size_t roomLeft = sizeof(input) - 1 - len;
            if (n > roomLeft) n = roomLeft;
            if (n > 0) {
                memmove(&input[cursor + n], &input[cursor], len - cursor);
                memset(&input[cursor], c, n);
                len += n;
                cursor += n;
                input[len] = '\0'; 
                redrawWholeLine(input, len, cursor);
            }
        }
    }
}

int main(int argc, char *argv[]){
    // Title
    printf("\033]0;Moon Shell\007");
    fflush(stdout); // Force the terminal to process the buffer immediately
    
    if (argc < 2){
        SetCurrentDirectoryA("C:\\");
    } else if (strcmp(argv[1], ".") == 0) {// pass nothing
    } else {
        if (!SetCurrentDirectoryA(argv[1])){
            fprintf(stderr, "Moon Shell cannot find path (%s)", argv[1]);
            return 1;
        }
    }

    // running shell
    runPrompt();

    return 0;
}