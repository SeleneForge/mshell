// head - print the first lines of files (or stdin)

#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include "cmdio.h"

static void headUsage(void) {
    fprintf(stderr, "head: usage [head [-n <count> | -<count>] [<file>...]]\n");
}

// Prints the first `n` lines of `in`. Returns 0 on success, -1 if writing
// failed, -2 if reading failed.
static int headStream(FILE *in, long long n) {
    static char buf[65536];
    size_t got;

    if (n <= 0) return 0;

    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        size_t off = 0;
        while (off < got) {
            const char *nl = (const char *)memchr(buf + off, '\n', got - off);
            size_t upto = nl ? (size_t)(nl - buf) + 1 : got;
            if (fwrite(buf + off, 1, upto - off, stdout) != upto - off) return -1;
            off = upto;
            if (nl && --n == 0) return 0;
        }
    }
    return ferror(in) ? -2 : 0;
}

static int headRun(int argc, char *argv[]) {
    long long n = 10;
    int i = 1;

    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (a[0] != '-' || a[1] == '\0') break;

        if (a[1] >= '0' && a[1] <= '9') {           // -5
            if (!cmdParseCount(a + 1, &n)) {
                fprintf(stderr, "head: invalid number of lines: '%s'\n", a + 1);
                return 1;
            }
        } else if (a[1] == 'n') {                    // -n 5   or   -n5
            const char *v = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
            if (!v) {
                fprintf(stderr, "head: option requires an argument -- 'n'\n");
                headUsage();
                return 1;
            }
            if (!cmdParseCount(v, &n)) {
                fprintf(stderr, "head: invalid number of lines: '%s'\n", v);
                return 1;
            }
        } else {
            fprintf(stderr, "head: invalid option -- '%c'\n", a[1]);
            headUsage();
            return 1;
        }
    }

    int nfiles = argc - i;
    int showNames = nfiles > 1;
    int status = 0;

    for (int k = 0; k < (nfiles > 0 ? nfiles : 1); k++) {
        const char *name = nfiles > 0 ? argv[i + k] : "-";
        int isStdin = strcmp(name, "-") == 0;
        FILE *in;

        if (isStdin) {
            if (cmdStdinIsConsole()) {
                fprintf(stderr, "head: nothing to read - give it a file, or pipe/redirect something into it\n");
                return 1;
            }
            in = stdin;
        } else {
            in = cmdOpenRead("head", name);
            if (!in) { status = 1; continue; }
        }

        if (showNames) {
            if (fprintf(stdout, "%s==> %s <==\n", k > 0 ? "\n" : "",
                        isStdin ? "standard input" : name) < 0) {
                if (!isStdin) fclose(in);
                return 1;
            }
        }

        int rc = headStream(in, n);
        if (!isStdin) fclose(in);

        if (rc == -1) return 1;
        if (rc == -2) {
            fprintf(stderr, "head: '%s' read error\n", isStdin ? "standard input" : name);
            status = 1;
        }
    }
    return status;
}

int cmd_head(int argc, char *argv[]) {
    CmdBinaryMode bm;
    cmdBinaryBegin(&bm);
    int rc = headRun(argc, argv);
    cmdBinaryEnd(&bm);
    return rc;
}
