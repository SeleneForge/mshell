#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include "cmdio.h"

#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

FILE *cmdOpenRead(const char *cmd, const char *path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, "%s: '%s' is a directory\n", cmd, path);
        return NULL;
    }

    HANDLE h = CreateFileA(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ||
            err == ERROR_INVALID_NAME) {
            fprintf(stderr, "%s: '%s' not found\n", cmd, path);
        } else if (err == ERROR_ACCESS_DENIED) {
            fprintf(stderr, "%s: '%s' access denied\n", cmd, path);
        } else if (err == ERROR_SHARING_VIOLATION) {
            fprintf(stderr, "%s: '%s' is locked by another program\n", cmd, path);
        } else {
            fprintf(stderr, "%s: '%s' cannot be opened (error %lu)\n", cmd, path, (unsigned long)err);
        }
        return NULL;
    }

    int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY);
    if (fd == -1) {
        CloseHandle(h);
        fprintf(stderr, "%s: '%s' cannot be opened\n", cmd, path);
        return NULL;
    }
    FILE *f = _fdopen(fd, "rb");
    if (!f) {
        _close(fd); // also closes the handle
        fprintf(stderr, "%s: '%s' cannot be opened\n", cmd, path);
        return NULL;
    }
    return f;
}

static int fdIsConsole(int fd) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    DWORD mode;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
}

int cmdStdinIsConsole(void)  { return fdIsConsole(0); }
int cmdStdoutIsConsole(void) { return fdIsConsole(1); }

int cmdStdoutIsPipe(void) {
    HANDLE h = (HANDLE)_get_osfhandle(1);
    return h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_PIPE;
}

void cmdBinaryBegin(CmdBinaryMode *m) {
    fflush(stdout);
    m->in  = _setmode(0, _O_BINARY);
    m->out = _setmode(1, _O_BINARY);
}

void cmdBinaryEnd(CmdBinaryMode *m) {
    fflush(stdout);
    if (m->in  != -1) _setmode(0, m->in);
    if (m->out != -1) _setmode(1, m->out);
}

int cmdParseCount(const char *s, long long *out) {
    if (!s) return 0;
    if (*s == '+') s++;
    if (*s < '0' || *s > '9') return 0;

    long long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        if (v > (LLONG_MAX - 9) / 10) return 0; // would overflow
        v = v * 10 + (*s - '0');
    }
    *out = v;
    return 1;
}

long long cmdFileSize(FILE *f) {
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
    LARGE_INTEGER size;
    if (h == INVALID_HANDLE_VALUE || !GetFileSizeEx(h, &size)) return -1;
    return (long long)size.QuadPart;
}

void cmdSleepMs(unsigned ms) {
    Sleep(ms);
}

struct CmdKeyWatch {
    HANDLE h;
};

CmdKeyWatch *cmdKeyWatchOpen(void) {
    HANDLE h = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    CmdKeyWatch *w = (CmdKeyWatch *)malloc(sizeof(CmdKeyWatch));
    if (!w) { CloseHandle(h); return NULL; }
    w->h = h;
    return w;
}

int cmdKeyWatchPressed(CmdKeyWatch *w) {
    if (!w) return 0;

    DWORD pending = 0;
    if (!GetNumberOfConsoleInputEvents(w->h, &pending) || pending == 0) return 0;

    INPUT_RECORD rec[32];
    DWORD got = 0;
    if (!ReadConsoleInputA(w->h, rec, 32, &got)) return 0;

    for (DWORD i = 0; i < got; i++) {
        if (rec[i].EventType != KEY_EVENT || !rec[i].Event.KeyEvent.bKeyDown) continue;

        const KEY_EVENT_RECORD *k = &rec[i].Event.KeyEvent;
        int ctrl = (k->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

        // In the shell's raw mode Ctrl+C isn't a signal, it just shows up
        // as an ordinary key press with character code 3.
        if (k->uChar.AsciiChar == 3 || k->wVirtualKeyCode == VK_ESCAPE ||
            (ctrl && k->wVirtualKeyCode == 'C')) {
            return 1;
        }
    }
    return 0;
}

void cmdKeyWatchClose(CmdKeyWatch *w) {
    if (!w) return;
    CloseHandle(w->h);
    free(w);
}
