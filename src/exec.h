#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>
#include <windows.h>

int execFindOnPath(const char *name, char *out, size_t outSize);

int execRun(const char *resolvedPath, const char *originalLine);

// A spawned-but-not-yet-waited-on external process, as started by
// execSpawnRedirected below.
typedef struct {
    HANDLE hProcess;
    HANDLE hThread;
} ExecHandle;

// Starts resolvedPath with argv[0..argc-1] as its arguments and hIn/hOut/
// hErr as its std handles, and returns immediately without waiting for it
// to exit - unlike execRun, which is only ever used for a single
// standalone command and waits for it inline. This is what the pipeline
// executor uses instead: every external stage in a pipeline needs to be
// started before any of them are waited on, so they can all run
// concurrently and actually stream data through their pipes instead of
// deadlocking. The caller is responsible for waiting on the returned
// hProcess and closing both handles once it's done with them, and for
// closing its own copies of hIn/hOut/hErr afterward if they're not the
// shell's own console handles.
//
// Also unlike execRun, this builds its own command line from argv
// instead of reusing the original typed line, since by the time a
// pipeline stage gets here any "|"/"<"/">"/">>" tokens (and whatever
// they were pointing includes at) have already been stripped out of it.
int execSpawnRedirected(const char *resolvedPath, char *argv[], int argc,
                         HANDLE hIn, HANDLE hOut, HANDLE hErr, ExecHandle *out);

#endif // EXEC_H
