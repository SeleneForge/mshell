// tail - print the last lines of files (or stdin), optionally following one

#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include "cmdio.h"

#include <stdlib.h>

static void tailUsage(void) {
    fprintf(stderr, "tail: usage [tail [-f] [-n <count> | -n +<line> | -<count>] [<file>...]]\n");
}

// The last byte tail wrote, so a -f session that stops mid-line can still
// leave the prompt on a line of its own. -1 = nothing written yet.
static int lastByte = -1;

// Returns 0, or -1 if the write failed (the pipe/file on the other end is gone).
static int put(const char *p, size_t n) {
    if (n == 0) return 0;
    if (fwrite(p, 1, n, stdout) != n) return -1;
    lastByte = (unsigned char)p[n - 1];
    return 0;
}

// Copies whatever is left of `in`. Returns 0, -1 (write failed) or -2 (read failed).
static int copyRest(FILE *in) {
    static char buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (put(buf, got) != 0) return -1;
    }
    return ferror(in) ? -2 : 0;
}

// "tail -n +N": everything from line N onward.
static int tailFromLine(FILE *in, long long startLine) {
    static char buf[65536];
    long long skip = startLine > 1 ? startLine - 1 : 0;
    size_t got;

    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        size_t off = 0;
        while (skip > 0 && off < got) {
            const char *nl = (const char *)memchr(buf + off, '\n', got - off);
            if (!nl) { off = got; break; }
            off = (size_t)(nl - buf) + 1;
            skip--;
        }
        if (off < got && put(buf + off, got - off) != 0) return -1;
    }
    return ferror(in) ? -2 : 0;
}

// The last `n` lines of something that can't be seeked (a pipe, or a
// redirected stdin): there's no way to know where they start without
// reading everything, so read it all and then look backwards.
static int tailStreamLast(FILE *in, long long n) {
    size_t cap = 65536, len = 0;
    char *data = (char *)malloc(cap);
    if (!data) return -2;

    size_t got;
    while ((got = fread(data + len, 1, cap - len, in)) > 0) {
        len += got;
        if (len == cap) {
            char *bigger = (char *)realloc(data, cap * 2);
            if (!bigger) { free(data); return -2; }
            data = bigger;
            cap *= 2;
        }
    }
    if (ferror(in)) { free(data); return -2; }

    size_t start = len;
    if (n > 0) {
        // One trailing newline is just the end of the last line, not an
        // extra empty line - don't count it.
        size_t end = (len > 0 && data[len - 1] == '\n') ? len - 1 : len;
        long long found = 0;
        start = 0;
        for (size_t i = end; i > 0; i--) {
            if (data[i - 1] == '\n' && ++found == n) { start = i; break; }
        }
    }

    int rc = put(data + start, len - start) != 0 ? -1 : 0;
    free(data);
    return rc;
}

// The last `n` lines of a real file. Works backwards from the end in
// blocks, so a multi-gigabyte log costs a few reads instead of a full
// scan. Leaves the file positioned at the end. Same return codes as above.
static int tailFileLast(FILE *f, long long n) {
    if (_fseeki64(f, 0, SEEK_END) != 0) return tailStreamLast(f, n); // not seekable after all
    long long size = _ftelli64(f);
    if (size < 0) return -2;

    long long start = size;
    if (n > 0) {
        static char buf[65536];
        long long end = size;

        // Same rule as above: a final newline doesn't start a new line.
        if (size > 0) {
            char last;
            if (_fseeki64(f, size - 1, SEEK_SET) != 0 || fread(&last, 1, 1, f) != 1) return -2;
            if (last == '\n') end = size - 1;
        }

        long long pos = end, found = 0;
        start = 0;
        while (pos > 0) {
            size_t want = pos > (long long)sizeof(buf) ? sizeof(buf) : (size_t)pos;
            pos -= (long long)want;
            if (_fseeki64(f, pos, SEEK_SET) != 0) return -2;
            if (fread(buf, 1, want, f) != want) return -2;

            int done = 0;
            for (size_t i = want; i > 0; i--) {
                if (buf[i - 1] == '\n' && ++found == n) {
                    start = pos + (long long)i;
                    done = 1;
                    break;
                }
            }
            if (done) break;
        }
    }

    if (_fseeki64(f, start, SEEK_SET) != 0) return -2;
    return copyRest(f);
}

// Keeps printing whatever gets appended to `f` until Ctrl+C / Esc.
static void tailFollow(FILE *f, const char *name) {
    static char buf[65536];
    CmdKeyWatch *watch = cmdKeyWatchOpen();

    fflush(stdout);
    for (;;) {
        if (cmdKeyWatchPressed(watch)) break;

        size_t got = fread(buf, 1, sizeof(buf), f);
        if (got > 0) {
            if (put(buf, got) != 0) break;
            fflush(stdout);
            continue;
        }

        // Nothing new. If the file has shrunk under us it was truncated
        // (rotated in place), so start over from the top.
        clearerr(f);
        long long size = cmdFileSize(f), pos = _ftelli64(f);
        if (size >= 0 && pos >= 0 && size < pos) {
            fprintf(stderr, "tail: '%s' was truncated\n", name);
            _fseeki64(f, 0, SEEK_SET);
            continue;
        }

        cmdSleepMs(150);
    }
    cmdKeyWatchClose(watch);
}

static int tailRun(int argc, char *argv[]) {
    long long n = 10;
    int fromStart = 0, follow = 0;
    int i = 1;

    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (a[0] != '-' || a[1] == '\0') break;

        if (a[1] >= '0' && a[1] <= '9') {                 // -5
            if (!cmdParseCount(a + 1, &n)) {
                fprintf(stderr, "tail: invalid number of lines: '%s'\n", a + 1);
                return 1;
            }
            fromStart = 0;
            continue;
        }

        for (const char *f = a + 1; *f; f++) {            // -f  -n 5  -n5  -n +5  -fn5
            if (*f == 'f') {
                follow = 1;
            } else if (*f == 'n') {
                const char *v = f[1] ? f + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v) {
                    fprintf(stderr, "tail: option requires an argument -- 'n'\n");
                    tailUsage();
                    return 1;
                }
                fromStart = (v[0] == '+');
                if (!cmdParseCount(v, &n)) {
                    fprintf(stderr, "tail: invalid number of lines: '%s'\n", v);
                    return 1;
                }
                break; // the rest of this argument was the number
            } else {
                fprintf(stderr, "tail: invalid option -- '%c'\n", *f);
                tailUsage();
                return 1;
            }
        }
    }

    int nfiles = argc - i;

    if (follow) {
        if (nfiles != 1 || strcmp(argv[i], "-") == 0) {
            fprintf(stderr, "tail: -f needs exactly one file\n");
            return 1;
        }
        // Built-ins run one at a time in this shell, so a pipe's other end
        // wouldn't even start until tail finished - which, following, it never does.
        if (cmdStdoutIsPipe()) {
            fprintf(stderr, "tail: -f can't feed a pipe (built-ins run one at a time here) - use > file, or run it on its own\n");
            return 1;
        }
    }

    int showNames = nfiles > 1;
    int status = 0;

    for (int k = 0; k < (nfiles > 0 ? nfiles : 1); k++) {
        const char *name = nfiles > 0 ? argv[i + k] : "-";
        int isStdin = strcmp(name, "-") == 0;
        FILE *in;

        if (isStdin) {
            if (cmdStdinIsConsole()) {
                fprintf(stderr, "tail: nothing to read - give it a file, or pipe/redirect something into it\n");
                return 1;
            }
            in = stdin;
        } else {
            in = cmdOpenRead("tail", name);
            if (!in) { status = 1; continue; }
        }

        if (showNames) {
            if (fprintf(stdout, "%s==> %s <==\n", k > 0 ? "\n" : "",
                        isStdin ? "standard input" : name) < 0) {
                if (!isStdin) fclose(in);
                return 1;
            }
        }

        int rc;
        if (fromStart) {
            rc = tailFromLine(in, n);
        } else if (isStdin) {
            rc = tailStreamLast(in, n);
        } else {
            rc = tailFileLast(in, n);
        }

        if (rc == 0 && follow) {
            tailFollow(in, name);
            if (lastByte != -1 && lastByte != '\n') fputc('\n', stdout);
        }
        if (!isStdin) fclose(in);

        if (rc == -1) return 1;
        if (rc == -2) {
            fprintf(stderr, "tail: '%s' read error\n", isStdin ? "standard input" : name);
            status = 1;
        }
    }
    return status;
}

int cmd_tail(int argc, char *argv[]) {
    CmdBinaryMode bm;
    cmdBinaryBegin(&bm);
    lastByte = -1;
    int rc = tailRun(argc, argv);
    cmdBinaryEnd(&bm);
    return rc;
}
