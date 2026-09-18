#define _CRT_SECURE_NO_WARNINGS
#include "exec.h"
#include "mode.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *afterFirstToken(const char *line) {
    const char *p = line;
    while (*p == ' ') p++;

    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        p = end ? end + 1 : p + strlen(p);
    } else {
        while (*p != ' ' && *p != '\0') p++;
    }

    while (*p == ' ') p++;
    return p;
}

static int hasExtension(const char *path, const char *ext) {
    size_t pathLen = strlen(path);
    size_t extLen = strlen(ext);
    if (pathLen < extLen) return 0;
    return _stricmp(path + pathLen - extLen, ext) == 0;
}

int execFindOnPath(const char *name, char *out, size_t outSize) {
    char pathExt[512];
    DWORD n = GetEnvironmentVariableA("PATHEXT", pathExt, sizeof(pathExt));
    if (n == 0 || n >= sizeof(pathExt)) {
        strcpy(pathExt, ".COM;.EXE;.BAT;.CMD");
    }

    for (char *ext = strtok(pathExt, ";"); ext; ext = strtok(NULL, ";")) {
        DWORD len = SearchPathA(NULL, name, ext, (DWORD)outSize, out, NULL);
        if (len > 0 && len < outSize) return 1;
    }

    // `name` might already carry its own extension (or a path) -
    // let SearchPath take it exactly as given as a last try.
    DWORD len = SearchPathA(NULL, name, NULL, (DWORD)outSize, out, NULL);
    return (len > 0 && len < outSize);
}

// Appends one argument to a command-line-in-progress being built up in
// out, quoting it if it contains a space (or is empty) so it survives as
// a single argument. This is the same level of quoting sophistication
// tokenize() offers on the way in (no support for escaping a literal
// quote inside an argument) - good enough for the plain paths and
// filenames this shell's own commands and most external tools deal with.
static void appendArg(char *out, size_t outSize, const char *arg) {
    size_t len = strlen(out);
    if (len > 0 && len + 1 < outSize) {
        out[len] = ' ';
        out[len + 1] = '\0';
        len++;
    }

    int needsQuotes = (strchr(arg, ' ') != NULL) || (*arg == '\0');
    if (needsQuotes) {
        snprintf(out + len, outSize - len, "\"%s\"", arg);
    } else {
        snprintf(out + len, outSize - len, "%s", arg);
    }
}

int execSpawnRedirected(const char *resolvedPath, char *argv[], int argc,
                         HANDLE hIn, HANDLE hOut, HANDLE hErr, ExecHandle *out) {
    char cmdLine[2048];

    if (hasExtension(resolvedPath, ".bat") || hasExtension(resolvedPath, ".cmd")) {
        char innerArgs[1536] = "";
        for (int i = 1; i < argc; i++) {
            appendArg(innerArgs, sizeof(innerArgs), argv[i]);
        }
        snprintf(cmdLine, sizeof(cmdLine), "cmd.exe /c \"\"%s\" %s\"", resolvedPath, innerArgs);
    } else {
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\"", resolvedPath);
        for (int i = 1; i < argc; i++) {
            appendArg(cmdLine, sizeof(cmdLine), argv[i]);
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn;
    si.hStdOutput = hOut;
    si.hStdError = hErr;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (!ok) return 0;

    out->hProcess = pi.hProcess;
    out->hThread = pi.hThread;
    return 1;
}

int execRun(const char *resolvedPath, const char *originalLine) {
    const char *args = afterFirstToken(originalLine);

    char cmdLine[2048];
    if (hasExtension(resolvedPath, ".bat") || hasExtension(resolvedPath, ".cmd")) {

        snprintf(cmdLine, sizeof(cmdLine), "cmd.exe /c \"\"%s\" %s\"", resolvedPath, args);
    } else {
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\" %s", resolvedPath, args);
    }

    fflush(stdout);
    disableRawMode(); // let the child see a normal console, the same way
                       // nvim restores terminal state before running ":!cmd"

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

    if (ok) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    enableRawMode(); // resume the shell's own raw-mode input handling

    if (!ok) {
        printf("Failed to run \"%s\" (error %lu).\n", resolvedPath, (unsigned long)GetLastError());
    }

    return ok ? 1 : 0;
}
