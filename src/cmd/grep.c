// grep - print the lines that match a pattern

#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include "cmdio.h"
#include "../regex.h"

#include <stdlib.h>

#define MAX_PATTERNS 64
#define BLOCK_SIZE   65536

static struct {
    Regex *re[MAX_PATTERNS];
    int nre;

    int ignoreCase, invert, number, count, listFiles, quiet, recursive;
    int only, wholeLine, word, extended, fixed, asText;
    int withName;   // 1 = -H, 0 = -h, -1 = decide from how many files there are
    int color;      // resolved: 1 = emit ANSI colors

    int anySelected, hadError, outFailed;
} G;

// ---------------------------------------------------------------------------
// Output. Matching lines are usually short and there can be a lot of them,
// so they're collected here and written in big blocks - the console is slow
// when it's fed a line at a time.
// ---------------------------------------------------------------------------

static char outBuf[BLOCK_SIZE];
static size_t outLen;

static void outFlush(void) {
    if (outLen && !G.outFailed && fwrite(outBuf, 1, outLen, stdout) != outLen) G.outFailed = 1;
    outLen = 0;
}

static void outWrite(const char *s, size_t n) {
    if (G.outFailed || n == 0) return;
    if (n >= sizeof(outBuf)) {
        outFlush();
        if (fwrite(s, 1, n, stdout) != n) G.outFailed = 1;
        return;
    }
    if (outLen + n > sizeof(outBuf)) outFlush();
    memcpy(outBuf + outLen, s, n);
    outLen += n;
}

static void outStr(const char *s) { outWrite(s, strlen(s)); }

static void outColor(const char *code, const char *s, size_t n) {
    if (!G.color) { outWrite(s, n); return; }
    outStr("\x1b[");
    outStr(code);
    outStr("m");
    outWrite(s, n);
    outStr("\x1b[0m");
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

// Finds the leftmost-longest match at or after `from` across all patterns.
static int findMatch(const char *s, size_t len, size_t from, size_t *ms, size_t *me) {
    int found = 0;
    for (int k = 0; k < G.nre; k++) {
        size_t a = 0, b = 0;
        int hit;
        if (G.wholeLine) {
            // -x: only a match covering the entire line counts
            hit = from == 0 && reMatchAt(G.re[k], s, len, 0, &b) && b == len;
        } else {
            hit = reSearch(G.re[k], s, len, from, &a, &b);
        }
        if (hit && (!found || a < *ms || (a == *ms && b > *me))) {
            *ms = a;
            *me = b;
            found = 1;
        }
    }
    return found;
}

static int addPatterns(const char *text, char *err, size_t errSize) {
    int flags = 0;
    if (G.ignoreCase) flags |= RE_ICASE;
    if (G.extended)   flags |= RE_EXTENDED;
    if (G.fixed)      flags |= RE_LITERAL;
    if (G.word && !G.wholeLine) flags |= RE_WORD;

    // A pattern containing newlines is several patterns, like real grep.
    const char *p = text;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);

        if (G.nre >= MAX_PATTERNS) {
            snprintf(err, errSize, "too many patterns (max %d)", MAX_PATTERNS);
            return 0;
        }
        char *one = (char *)malloc(n + 1);
        if (!one) { snprintf(err, errSize, "out of memory"); return 0; }
        memcpy(one, p, n);
        one[n] = '\0';

        char why[128];
        Regex *re = reCompile(one, flags, why, sizeof(why));
        if (!re) {
            snprintf(err, errSize, "invalid pattern '%s': %s", one, why);
            free(one);
            return 0;
        }
        free(one);
        G.re[G.nre++] = re;

        if (!nl) break;
        p = nl + 1;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Reading lines. Lines are handed out as pointers straight into a 64 KB read
// block whenever they fit inside one; only a line that straddles two blocks
// gets copied into a growing side buffer. No line length limit.
// ---------------------------------------------------------------------------

static struct {
    FILE *f;
    char blk[BLOCK_SIZE];
    size_t pos, end;
    int eof, first, binary, failed;
    char *acc;
    size_t accLen, accCap;
} LR;

static int accAppend(const char *p, size_t n) {
    if (!LR.acc || LR.accLen + n > LR.accCap) {
        size_t cap = LR.accCap ? LR.accCap : 1024;
        while (cap < LR.accLen + n) cap *= 2;
        char *grown = (char *)realloc(LR.acc, cap);
        if (!grown) return 0;
        LR.acc = grown;
        LR.accCap = cap;
    }
    memcpy(LR.acc + LR.accLen, p, n);
    LR.accLen += n;
    return 1;
}

static void lrOpen(FILE *f) {
    LR.f = f;
    LR.pos = LR.end = 0;
    LR.eof = LR.binary = LR.failed = 0;
    LR.first = 1;
    LR.accLen = 0;
}

static int lrFill(void) {
    size_t n = fread(LR.blk, 1, sizeof(LR.blk), LR.f);
    if (n == 0) {
        LR.eof = 1;
        if (ferror(LR.f)) LR.failed = 1;
        return 0;
    }
    LR.pos = 0;
    LR.end = n;
    if (LR.first) {
        LR.first = 0;
        // A NUL byte in the first block means "this isn't a text file"
        if (!G.asText && memchr(LR.blk, 0, n)) LR.binary = 1;
    }
    return 1;
}

// Returns 1 and the next line (without its '\n'), or 0 at the end.
static int lrNext(const char **line, size_t *len) {
    int spanning = 0;
    LR.accLen = 0;

    for (;;) {
        if (LR.pos >= LR.end) {
            if (LR.eof || !lrFill()) {
                if (spanning && LR.accLen > 0) { // last line had no newline
                    *line = LR.acc;
                    *len = LR.accLen;
                    return 1;
                }
                return 0;
            }
        }

        const char *start = LR.blk + LR.pos;
        const char *nl = (const char *)memchr(start, '\n', LR.end - LR.pos);
        if (nl) {
            size_t chunk = (size_t)(nl - start);
            LR.pos += chunk + 1;
            if (!spanning) { *line = start; *len = chunk; return 1; }
            if (!accAppend(start, chunk)) { LR.failed = 1; return 0; }
            *line = LR.acc;
            *len = LR.accLen;
            return 1;
        }

        if (!accAppend(start, LR.end - LR.pos)) { LR.failed = 1; return 0; }
        spanning = 1;
        LR.pos = LR.end;
    }
}

// ---------------------------------------------------------------------------
// Printing a selected line
// ---------------------------------------------------------------------------

static void outPrefix(const char *name, int withName, long long lineNo) {
    if (withName) {
        outColor("35", name, strlen(name));
        outColor("36", ":", 1);
    }
    if (G.number) {
        char num[32];
        int n = snprintf(num, sizeof(num), "%lld", lineNo);
        outColor("32", num, (size_t)n);
        outColor("36", ":", 1);
    }
}

// `len` is the whole line; `mlen` is the part matching looks at (same, minus
// a trailing '\r' on Windows-style files, so "foo$" still works on them).
static void printSelected(const char *name, int withName, long long lineNo,
                          const char *line, size_t len, size_t mlen) {
    size_t from = 0, ms, me;

    if (G.only) {
        if (G.invert) return; // like real grep: -o -v prints nothing
        while (from <= mlen && findMatch(line, mlen, from, &ms, &me)) {
            if (me > ms) {
                outPrefix(name, withName, lineNo);
                outColor("1;31", line + ms, me - ms);
                outWrite("\n", 1);
            }
            from = me > ms ? me : me + 1;
        }
        return;
    }

    outPrefix(name, withName, lineNo);
    if (G.color && !G.invert) {
        size_t pos = 0;
        while (from <= mlen && findMatch(line, mlen, from, &ms, &me)) {
            if (me > ms) {
                outWrite(line + pos, ms - pos);
                outColor("1;31", line + ms, me - ms);
                pos = me;
            }
            from = me > ms ? me : me + 1;
        }
        outWrite(line + pos, len - pos);
    } else {
        outWrite(line, len);
    }
    outWrite("\n", 1);
}

// ---------------------------------------------------------------------------
// Searching files
// ---------------------------------------------------------------------------

// Returns 1 when the whole run should stop (-q found something, or stdout
// went away), 0 to carry on with the next file.
static int grepStream(FILE *in, const char *name, int withName) {
    long long lineNo = 0, count = 0;
    int stop = 0;
    const char *line;
    size_t len;

    lrOpen(in);
    while (lrNext(&line, &len)) {
        lineNo++;

        size_t mlen = len, ms, me;
        if (mlen > 0 && line[mlen - 1] == '\r') mlen--;

        int matched = findMatch(line, mlen, 0, &ms, &me);
        if (matched == G.invert) continue; // not a selected line

        count++;
        G.anySelected = 1;

        if (G.quiet)     { stop = 1; break; }
        if (G.listFiles) break;            // one hit is all -l needs
        if (G.count)     continue;

        if (LR.binary) {
            outStr("Binary file ");
            outStr(name);
            outStr(" matches\n");
            break;
        }

        printSelected(name, withName, lineNo, line, len, mlen);
        if (G.outFailed) { stop = 1; break; }
    }

    if (LR.failed) {
        outFlush();
        fprintf(stderr, "grep: '%s' read error or out of memory\n", name);
        G.hadError = 1;
    }

    if (!stop) {
        if (G.listFiles) {
            if (count > 0) {
                outColor("35", name, strlen(name));
                outWrite("\n", 1);
            }
        } else if (G.count) {
            if (withName) {
                outColor("35", name, strlen(name));
                outColor("36", ":", 1);
            }
            char num[32];
            int n = snprintf(num, sizeof(num), "%lld\n", count);
            outWrite(num, (size_t)n);
        }
    }
    return stop || G.outFailed;
}

static int grepFile(const char *fsPath, const char *shown, int withName) {
    outFlush(); // keep earlier output ahead of any error message below
    FILE *f = cmdOpenRead("grep", fsPath);
    if (!f) { G.hadError = 1; return 0; }

    int stop = grepStream(f, shown, withName);
    fclose(f);
    return stop;
}

static int endsWithSep(const char *s) {
    size_t n = strlen(s);
    return n > 0 && (s[n - 1] == '\\' || s[n - 1] == '/');
}

// dir + name, with exactly one separator between; a NULL/empty dir gives just name.
static int joinPath(char *out, size_t outSize, const char *dir, const char *name) {
    int n;
    if (!dir || !dir[0])        n = snprintf(out, outSize, "%s", name);
    else if (endsWithSep(dir))  n = snprintf(out, outSize, "%s%s", dir, name);
    else                        n = snprintf(out, outSize, "%s\\%s", dir, name);
    return n >= 0 && (size_t)n < outSize;
}

// Recursive search of a directory. `shown` is the name to print for it
// (empty for the implicit "." of a bare "grep -r pattern").
static int grepWalk(const char *dir, const char *shown, int withName) {
    char search[MAX_PATH];
    if (!joinPath(search, sizeof(search), dir, "*")) {
        outFlush();
        fprintf(stderr, "grep: path too long: '%s'\n", dir);
        G.hadError = 1;
        return 0;
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) { // that one just means "empty"
            outFlush();
            fprintf(stderr, "grep: '%s' cannot be read\n", dir);
            G.hadError = 1;
        }
        return 0;
    }

    int stop = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char fsChild[MAX_PATH], shownChild[MAX_PATH];
        if (!joinPath(fsChild, sizeof(fsChild), dir, fd.cFileName) ||
            !joinPath(shownChild, sizeof(shownChild), shown, fd.cFileName)) {
            outFlush();
            fprintf(stderr, "grep: path too long: '%s'\n", fd.cFileName);
            G.hadError = 1;
            continue;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Don't follow junctions and symlinks into other directories -
            // they can loop back on themselves (the old "Application Data"
            // junctions in a user profile do exactly that).
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            stop = grepWalk(fsChild, shownChild, withName);
        } else {
            stop = grepFile(fsChild, shownChild, withName);
        }
    } while (!stop && FindNextFileA(h, &fd));

    FindClose(h);
    return stop;
}

static int grepOperand(const char *path, int withName) {
    if (strcmp(path, "-") == 0) {
        if (cmdStdinIsConsole()) {
            outFlush();
            fprintf(stderr, "grep: nothing to read - give it a file, or pipe/redirect something into it\n");
            G.hadError = 1;
            return 0;
        }
        return grepStream(stdin, "(standard input)", withName);
    }

    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!G.recursive) {
            outFlush();
            fprintf(stderr, "grep: '%s' is a directory (use -r to search inside it)\n", path);
            G.hadError = 1;
            return 0;
        }
        return grepWalk(path, path, withName);
    }
    return grepFile(path, path, withName);
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

static void grepUsage(FILE *to) {
    fprintf(to,
        "grep: usage [grep [options] <pattern> [file...]]\n"
        "  -e <pattern>  add a pattern (repeatable)     -E  extended regex (a|b, x+, (..), {n,m})\n"
        "  -F            plain text, not a regex        -i  ignore case\n"
        "  -w            match whole words only         -x  match whole lines only\n"
        "  -v            show non-matching lines        -o  show only the matching part\n"
        "  -n            show line numbers              -c  show a count per file\n"
        "  -l            show only names of files that match\n"
        "  -q            no output, just the exit status\n"
        "  -r            search directories recursively (no file = current directory)\n"
        "  -H / -h       always / never show the file name\n"
        "  -a            treat binary files as text     --color[=always|never|auto]\n"
        "  Patterns are basic regex by default: \\( \\) \\| \\+ \\? \\{ \\} are the operators.\n"
        "  Also understood: \\w \\s \\d \\b \\< \\>  and [[:alpha:]]-style classes.\n"
        "  Exit status: 0 if something matched, 1 if nothing did, 2 on an error.\n");
}

static int applyFlag(char c) {
    switch (c) {
    case 'i': G.ignoreCase = 1; break;
    case 'v': G.invert = 1; break;
    case 'n': G.number = 1; break;
    case 'c': G.count = 1; break;
    case 'l': G.listFiles = 1; break;
    case 'q': G.quiet = 1; break;
    case 'r': case 'R': G.recursive = 1; break;
    case 'E': G.extended = 1; G.fixed = 0; break;
    case 'F': G.fixed = 1; G.extended = 0; break;
    case 'G': G.extended = 0; G.fixed = 0; break;
    case 'w': G.word = 1; break;
    case 'x': G.wholeLine = 1; break;
    case 'o': G.only = 1; break;
    case 'H': G.withName = 1; break;
    case 'h': G.withName = 0; break;
    case 'a': G.asText = 1; break;
    default: return 0;
    }
    return 1;
}

static const struct { const char *name; char flag; } LONG_FLAGS[] = {
    { "ignore-case", 'i' },     { "invert-match", 'v' },  { "line-number", 'n' },
    { "count", 'c' },           { "files-with-matches", 'l' },
    { "quiet", 'q' },           { "silent", 'q' },
    { "recursive", 'r' },       { "dereference-recursive", 'r' },
    { "extended-regexp", 'E' }, { "fixed-strings", 'F' }, { "basic-regexp", 'G' },
    { "word-regexp", 'w' },     { "line-regexp", 'x' },   { "only-matching", 'o' },
    { "with-filename", 'H' },   { "no-filename", 'h' },   { "text", 'a' },
};

static int grepRun(int argc, char *argv[]) {
    const char **patterns = (const char **)calloc((size_t)argc + 1, sizeof(char *));
    const char **operands = (const char **)calloc((size_t)argc + 1, sizeof(char *));
    if (!patterns || !operands) {
        free(patterns);
        free(operands);
        fprintf(stderr, "grep: out of memory\n");
        return 2;
    }
    int npat = 0, nops = 0, colorMode = 0; // 0 = never, 1 = always, 2 = auto
    int rc = 2;

    G.withName = -1;

    // Options may come before, between, or after the operands - like real grep.
    int optionsDone = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (optionsDone || a[0] != '-' || a[1] == '\0') {
            operands[nops++] = a;
            continue;
        }
        if (strcmp(a, "--") == 0) { optionsDone = 1; continue; }

        if (a[1] == '-') {
            const char *opt = a + 2;
            const char *eq = strchr(opt, '=');
            size_t nameLen = eq ? (size_t)(eq - opt) : strlen(opt);

            if (strcmp(opt, "help") == 0) {
                grepUsage(stdout);
                rc = 0;
                goto done;
            }
            if (nameLen == 6 && strncmp(opt, "regexp", 6) == 0) {
                const char *v = eq ? eq + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v) { fprintf(stderr, "grep: option '--regexp' requires an argument\n"); goto done; }
                patterns[npat++] = v;
                continue;
            }
            if ((nameLen == 5 && strncmp(opt, "color", 5) == 0) ||
                (nameLen == 6 && strncmp(opt, "colour", 6) == 0)) {
                const char *v = eq ? eq + 1 : "auto";
                if (strcmp(v, "always") == 0 || strcmp(v, "yes") == 0 || strcmp(v, "force") == 0) colorMode = 1;
                else if (strcmp(v, "never") == 0 || strcmp(v, "no") == 0 || strcmp(v, "none") == 0) colorMode = 0;
                else if (strcmp(v, "auto") == 0 || strcmp(v, "tty") == 0 || strcmp(v, "if-tty") == 0) colorMode = 2;
                else { fprintf(stderr, "grep: invalid argument '%s' for '--color'\n", v); goto done; }
                continue;
            }

            int known = 0;
            for (size_t k = 0; k < sizeof(LONG_FLAGS) / sizeof(LONG_FLAGS[0]); k++) {
                if (!eq && strcmp(opt, LONG_FLAGS[k].name) == 0) {
                    applyFlag(LONG_FLAGS[k].flag);
                    known = 1;
                    break;
                }
            }
            if (!known) {
                fprintf(stderr, "grep: unrecognized option '%s'\n", a);
                grepUsage(stderr);
                goto done;
            }
            continue;
        }

        for (const char *f = a + 1; *f; f++) {
            if (*f == 'e') {
                const char *v = f[1] ? f + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v) {
                    fprintf(stderr, "grep: option requires an argument -- 'e'\n");
                    goto done;
                }
                patterns[npat++] = v;
                break; // the rest of this argument was the pattern
            }
            if (!applyFlag(*f)) {
                fprintf(stderr, "grep: invalid option -- '%c'\n", *f);
                grepUsage(stderr);
                goto done;
            }
        }
    }

    // With no -e, the first operand is the pattern.
    int firstFile = 0;
    if (npat == 0) {
        if (nops == 0) { grepUsage(stderr); goto done; }
        patterns[npat++] = operands[0];
        firstFile = 1;
    }

    {
        char err[256];
        for (int k = 0; k < npat; k++) {
            if (!addPatterns(patterns[k], err, sizeof(err))) {
                fprintf(stderr, "grep: %s\n", err);
                goto done;
            }
        }
    }

    G.color = colorMode == 1 || (colorMode == 2 && cmdStdoutIsConsole());

    {
        int nfiles = nops - firstFile;
        int implicitDot = nfiles == 0 && G.recursive;
        int withName = G.withName != -1 ? G.withName
                                        : (nfiles > 1 || G.recursive) ? 1 : 0;
        int stop = 0;

        if (implicitDot) {
            stop = grepWalk(".", "", withName);
        } else if (nfiles == 0) {
            stop = grepOperand("-", withName);
        } else {
            for (int k = firstFile; k < nops && !stop; k++) {
                stop = grepOperand(operands[k], withName);
            }
        }
        outFlush();

        // 0 = something matched, 1 = nothing did, 2 = error - except that a
        // -q hit counts as success even if some other file couldn't be read.
        if (G.hadError && !(G.quiet && G.anySelected)) rc = 2;
        else rc = G.anySelected ? 0 : 1;
    }

done:
    for (int k = 0; k < G.nre; k++) reFree(G.re[k]);
    G.nre = 0;
    free(LR.acc);
    LR.acc = NULL;
    LR.accLen = LR.accCap = 0;
    free(patterns);
    free(operands);
    return rc;
}

int cmd_grep(int argc, char *argv[]) {
    memset(&G, 0, sizeof(G));
    outLen = 0;

    CmdBinaryMode bm;
    cmdBinaryBegin(&bm);
    int rc = grepRun(argc, argv);
    outFlush();
    cmdBinaryEnd(&bm);
    return rc;
}
