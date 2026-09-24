#ifndef CMDIO_H
#define CMDIO_H

// Small helpers shared by the text-reading built-ins (cat, head, tail,
// grep) - they all read files or stdin the same way, so the fiddly
// Windows-specific parts live here once instead of four times.

#include <stdio.h>

// Opens `path` for reading as raw bytes. Every sharing flag is set, so it
// works on a file another program is still writing to (a log, say) and
// doesn't stop that program from renaming or deleting it in the meantime.
// On failure prints "<cmd>: '<path>' <reason>" to stderr and returns NULL.
FILE *cmdOpenRead(const char *cmd, const char *path);

// Is stdin still the interactive console (i.e. nothing was piped or
// redirected into this command)? Reading from it would just sit there:
// the shell keeps the console in raw mode, so there's no line editing,
// no echo, and no Ctrl+Z / Ctrl+C to end the input.
int cmdStdinIsConsole(void);

int cmdStdoutIsConsole(void);
int cmdStdoutIsPipe(void);

// The shell's stdin/stdout are text mode, which quietly rewrites line
// endings (CRLF <-> LF) and treats Ctrl+Z as end-of-file. These commands
// shouldn't alter a single byte they pass through, so they switch both to
// binary for the duration of the command and put them back afterwards.
typedef struct { int in; int out; } CmdBinaryMode;
void cmdBinaryBegin(CmdBinaryMode *m);
void cmdBinaryEnd(CmdBinaryMode *m);

// Parses a plain non-negative decimal number (a leading '+' is allowed).
// Returns 1 on success.
int cmdParseCount(const char *s, long long *out);

// -- used by tail -f ---------------------------------------------------

// Current size in bytes of the file behind `f`, or -1.
long long cmdFileSize(FILE *f);

void cmdSleepMs(unsigned ms);

// Watches the real console for Ctrl+C / Esc. Opens the console directly
// rather than using stdin, so it still works when stdin is redirected.
typedef struct CmdKeyWatch CmdKeyWatch;
CmdKeyWatch *cmdKeyWatchOpen(void);
int cmdKeyWatchPressed(CmdKeyWatch *w);   // drains pending key presses
void cmdKeyWatchClose(CmdKeyWatch *w);

#endif // CMDIO_H
