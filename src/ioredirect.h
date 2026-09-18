#ifndef IOREDIRECT_H
#define IOREDIRECT_H

#include <windows.h>

// Built-in commands (cmd_echo, cmd_ls, ...) just call printf, which goes
// wherever the CRT's stdin/stdout (fd 0/1) currently point - normally the
// console. To make "|", ">", ">>" and "<" work for them too, the pipeline
// executor temporarily repoints fd 0/1 at a file or pipe handle for the
// duration of one built-in call, then puts them back.
typedef struct {
    int savedStdin;
    int savedStdout;
    int tempStdinFd;
    int tempStdoutFd;
} IoRedirectState;

// Pass INVALID_HANDLE_VALUE for whichever side shouldn't be touched (a
// command with only ">" redirection leaves stdin alone, for example).
// This only ever works on a private duplicate of hIn/hOut, so the caller
// keeps ownership of the handles it passed in - ioRedirectBegin/End never
// closes them, only its own copies.
void ioRedirectBegin(IoRedirectState *state, HANDLE hIn, HANDLE hOut);

// Restores fd 0/1 to what they were before the matching ioRedirectBegin.
void ioRedirectEnd(IoRedirectState *state);

#endif // IOREDIRECT_H
