#define _CRT_SECURE_NO_WARNINGS
#include "pipeline.h"
#include "mshell.h"
#include "commands.h"
#include "exec.h"
#include "suggest.h"
#include "mode.h"
#include "ioredirect.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

// Anonymous pipes between stages get a generous buffer so that a builtin
// producing output (ls, echo, help, ...) essentially never blocks trying
// to write into one. Builtins run synchronously on the shell's own
// thread - there's no second thread free to drain the pipe while one is
// still running - so unlike a normal multi-process pipeline, a builtin
// whose output badly outgrows this buffer with nobody reading yet could
// stall the shell. 64KB comfortably covers everything this shell's own
// builtins produce.
#define PIPE_BUFFER_SIZE (64 * 1024)

static int isOperatorToken(const char *tok) {
    return strcmp(tok, "|") == 0 || strcmp(tok, "<") == 0 ||
           strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0;
}

static void resetStage(PipelineStage *st) {
    st->argc = 0;
    st->inFile = NULL;
    st->outFile = NULL;
    st->append = 0;
}

int pipelineParse(char *argv[], int argc, Pipeline *pl, char *errMsg, size_t errMsgSize) {
    pl->stageCount = 0;
    resetStage(&pl->stages[0]);
    PipelineStage *cur = &pl->stages[0];

    for (int i = 0; i < argc; i++) {
        char *tok = argv[i];

        if (strcmp(tok, "|") == 0) {
            if (cur->argc == 0) {
                snprintf(errMsg, errMsgSize, "syntax error near unexpected token '|'");
                return 0;
            }
            if (pl->stageCount + 1 >= PIPELINE_MAX_STAGES) {
                snprintf(errMsg, errMsgSize, "too many pipeline stages (max %d)", PIPELINE_MAX_STAGES);
                return 0;
            }
            pl->stageCount++;
            cur = &pl->stages[pl->stageCount];
            resetStage(cur);
            continue;
        }

        if (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
            if (i + 1 >= argc || isOperatorToken(argv[i + 1])) {
                snprintf(errMsg, errMsgSize, "syntax error: expected a filename after '%s'", tok);
                return 0;
            }
            char *target = argv[++i];
            if (strcmp(tok, "<") == 0) {
                cur->inFile = target;
            } else {
                cur->outFile = target;
                cur->append = (strcmp(tok, ">>") == 0);
            }
            continue;
        }

        if (cur->argc >= MAX_ARGS - 1) {
            snprintf(errMsg, errMsgSize, "too many arguments");
            return 0;
        }
        cur->argv[cur->argc++] = tok;
    }

    if (cur->argc == 0) {
        snprintf(errMsg, errMsgSize, "syntax error: expected a command after '|'");
        return 0;
    }
    pl->stageCount++;
    return 1;
}

static void reportUnknownCommand(const char *name) {
    printf("Unknown Command! : %s\n", name);
    char suggestion[SUGGEST_MAX_LEN + 1];
    if (suggestCommand(name, suggestion, sizeof(suggestion))) {
        printf("  Did you mean \"%s\"?\n", suggestion);
    }
}

int pipelineExecute(Pipeline *pl) {
    int n = pl->stageCount;
    if (n <= 0) return 0;
    if (n > PIPELINE_MAX_STAGES) n = PIPELINE_MAX_STAGES; // defensive; pipelineParse already caps this

    HANDLE pipeRead[PIPELINE_MAX_STAGES - 1];
    HANDLE pipeWrite[PIPELINE_MAX_STAGES - 1];
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    for (int i = 0; i < n - 1; i++) {
        if (!CreatePipe(&pipeRead[i], &pipeWrite[i], &sa, PIPE_BUFFER_SIZE)) {
            printf("mshell: could not create pipe (error %lu)\n", (unsigned long)GetLastError());
            for (int j = 0; j < i; j++) {
                CloseHandle(pipeRead[j]);
                CloseHandle(pipeWrite[j]);
            }
            return 1;
        }
    }

    // Builtins run in-process no matter what, so raw mode only needs to
    // come down if something in this pipeline is actually going to be a
    // separate child process taking over the console.
    int anyExternal = 0;
    for (int i = 0; i < n; i++) {
        if (commandsFind(pl->stages[i].argv[0]) == NULL) { anyExternal = 1; break; }
    }
    if (anyExternal) {
        fflush(stdout);
        disableRawMode();
    }

    HANDLE realIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE realOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE realErr = GetStdHandle(STD_ERROR_HANDLE);

    ExecHandle procs[PIPELINE_MAX_STAGES];
    int procCount = 0;

    // The pipeline's overall exit status is whatever its *last* stage
    // ends up with (standard shell convention - `a | b`'s status is b's,
    // regardless of whether a succeeded). If the last stage is external
    // we don't know its real code until we've waited on it below, so
    // lastStageProcIndex records which entry in procs[] to check;
    // lastStageStatus holds the answer directly for anything resolved
    // immediately (a builtin, or a stage that never got to run at all).
    int lastStageStatus = 0;
    int lastStageProcIndex = -1;

    for (int i = 0; i < n; i++) {
        PipelineStage *st = &pl->stages[i];
        int isLast = (i == n - 1);

        HANDLE hFileIn = INVALID_HANDLE_VALUE;
        HANDLE hFileOut = INVALID_HANDLE_VALUE;
        HANDLE stageIn = realIn;
        HANDLE stageOut = realOut;
        int failed = 0;

        // ----- resolve this stage's stdin -----
        if (st->inFile) {
            hFileIn = CreateFileA(st->inFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFileIn == INVALID_HANDLE_VALUE) {
                printf("mshell: %s: cannot open for reading\n", st->inFile);
                failed = 1;
            } else {
                stageIn = hFileIn;
            }
        } else if (i > 0) {
            stageIn = pipeRead[i - 1];
        }

        // ----- resolve this stage's stdout -----
        if (!failed) {
            if (st->outFile) {
                DWORD disposition = st->append ? OPEN_ALWAYS : CREATE_ALWAYS;
                hFileOut = CreateFileA(st->outFile, GENERIC_WRITE, FILE_SHARE_READ,
                                       &sa, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFileOut == INVALID_HANDLE_VALUE) {
                    printf("mshell: %s: cannot open for writing\n", st->outFile);
                    failed = 1;
                } else {
                    if (st->append) SetFilePointer(hFileOut, 0, NULL, FILE_END);
                    stageOut = hFileOut;
                }
            } else if (i < n - 1) {
                stageOut = pipeWrite[i];
            }
        }

        // ----- run it -----
        if (failed) {
            if (isLast) lastStageStatus = 1;
        } else if (commandsFind(st->argv[0]) != NULL) {
            IoRedirectState rst;
            HANDLE rin = (stageIn == realIn) ? INVALID_HANDLE_VALUE : stageIn;
            HANDLE rout = (stageOut == realOut) ? INVALID_HANDLE_VALUE : stageOut;
            ioRedirectBegin(&rst, rin, rout);
            int status = runBuiltin(st->argc, st->argv);
            ioRedirectEnd(&rst);
            if (isLast) lastStageStatus = status;
        } else {
            char resolvedPath[MAX_PATH];
            if (execFindOnPath(st->argv[0], resolvedPath, sizeof(resolvedPath))) {
                ExecHandle eh;
                if (execSpawnRedirected(resolvedPath, st->argv, st->argc, stageIn, stageOut, realErr, &eh)) {
                    if (isLast) lastStageProcIndex = procCount;
                    procs[procCount++] = eh;
                } else {
                    printf("mshell: failed to run \"%s\" (error %lu)\n",
                           resolvedPath, (unsigned long)GetLastError());
                    if (isLast) lastStageStatus = 126; // found on PATH, but couldn't be started
                }
            } else {
                reportUnknownCommand(st->argv[0]);
                if (isLast) lastStageStatus = 127; // matches the shell convention for "not found"
            }
        }

        // ----- release this stage's own handles -----
        // Every handle this stage could have used gets closed here,
        // whether or not it actually ended up being the chosen one - the
        // parent must give up its copy of each pipe end once both the
        // writer and reader on that end have been dispatched, or the
        // other side can never see EOF (or, on failure, a prompt broken
        // pipe instead of hanging forever).
        if (hFileIn != INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
        if (hFileOut != INVALID_HANDLE_VALUE) CloseHandle(hFileOut);
        if (i > 0) CloseHandle(pipeRead[i - 1]);
        if (i < n - 1) CloseHandle(pipeWrite[i]);

        // Note: if this stage was "exit", isRunning is now 0. We still
        // finish dispatching the rest of the pipeline (harmless - any
        // stage downstream of "exit" just sees an immediately-empty
        // input) rather than special-case unwinding it; runPrompt()'s
        // main loop is what actually stops the shell once runcmd()
        // returns.
    }

    for (int i = 0; i < procCount; i++) {
        WaitForSingleObject(procs[i].hProcess, INFINITE);
    }

    if (lastStageProcIndex >= 0) {
        DWORD code = 0;
        GetExitCodeProcess(procs[lastStageProcIndex].hProcess, &code);
        lastStageStatus = (int)code;
    }

    for (int i = 0; i < procCount; i++) {
        CloseHandle(procs[i].hProcess);
        CloseHandle(procs[i].hThread);
    }

    if (anyExternal) {
        enableRawMode();
    }

    return lastStageStatus;
}
