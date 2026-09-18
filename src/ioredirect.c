#define _CRT_SECURE_NO_WARNINGS
#include "ioredirect.h"
#include <io.h>
#include <fcntl.h>
#include <stdio.h>

void ioRedirectBegin(IoRedirectState *state, HANDLE hIn, HANDLE hOut) {
    state->savedStdin = -1;
    state->savedStdout = -1;
    state->tempStdinFd = -1;
    state->tempStdoutFd = -1;

    fflush(stdout);

    HANDLE proc = GetCurrentProcess();

    if (hIn != INVALID_HANDLE_VALUE) {
        HANDLE dup = INVALID_HANDLE_VALUE;
        if (DuplicateHandle(proc, hIn, proc, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            int fd = _open_osfhandle((intptr_t)dup, _O_RDONLY | _O_TEXT);
            if (fd != -1) {
                state->savedStdin = _dup(0);
                _dup2(fd, 0);
                state->tempStdinFd = fd;
            } else {
                CloseHandle(dup);
            }
        }
    }

    if (hOut != INVALID_HANDLE_VALUE) {
        HANDLE dup = INVALID_HANDLE_VALUE;
        if (DuplicateHandle(proc, hOut, proc, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            int fd = _open_osfhandle((intptr_t)dup, _O_WRONLY | _O_TEXT);
            if (fd != -1) {
                state->savedStdout = _dup(1);
                _dup2(fd, 1);
                state->tempStdoutFd = fd;
            } else {
                CloseHandle(dup);
            }
        }
    }
}

void ioRedirectEnd(IoRedirectState *state) {
    fflush(stdout);

    // Restore fd 1 first, then release both of the fds that were
    // involved: _dup2 only closes whatever *was* at the destination fd
    // number, so the backup (savedStdout) and the temporary wrapper
    // (tempStdoutFd) both still need an explicit _close after that.
    if (state->tempStdoutFd != -1) {
        _dup2(state->savedStdout, 1);
        _close(state->savedStdout);
        _close(state->tempStdoutFd);
    }
    if (state->tempStdinFd != -1) {
        _dup2(state->savedStdin, 0);
        _close(state->savedStdin);
        _close(state->tempStdinFd);
    }
}
