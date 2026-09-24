// cat - print files (or stdin) to stdout, byte for byte

#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include "cmdio.h"

typedef struct {
    int number;         // -n
    long long line;     // last line number printed
    int atLineStart;
} CatState;

// Copies `in` to stdout. Returns 0 on success, -1 if writing failed (the
// reader on the other end of a pipe went away), -2 if reading failed.
static int catStream(FILE *in, CatState *st) {
    static char buf[65536];
    size_t got;

    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (!st->number) {
            if (fwrite(buf, 1, got, stdout) != got) return -1;
            continue;
        }
        for (size_t i = 0; i < got; i++) {
            if (st->atLineStart) {
                if (fprintf(stdout, "%6lld\t", ++st->line) < 0) return -1;
                st->atLineStart = 0;
            }
            if (putc(buf[i], stdout) == EOF) return -1;
            if (buf[i] == '\n') st->atLineStart = 1;
        }
    }
    return ferror(in) ? -2 : 0;
}

static int catRun(int argc, char *argv[]) {
    CatState st = { 0, 0, 1 };
    int i = 1;

    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (a[0] != '-' || a[1] == '\0') break; // first file (or "-")
        for (const char *f = a + 1; *f; f++) {
            if (*f == 'n') {
                st.number = 1;
            } else {
                fprintf(stderr, "cat: invalid option -- '%c'\n", *f);
                fprintf(stderr, "cat: usage [cat [-n] <file>...]  (\"-\" reads standard input)\n");
                return 1;
            }
        }
    }

    int status = 0;
    int nfiles = argc - i;

    for (int k = 0; k < (nfiles > 0 ? nfiles : 1); k++) {
        const char *name = nfiles > 0 ? argv[i + k] : "-";
        FILE *in;
        int isStdin = strcmp(name, "-") == 0;

        if (isStdin) {
            if (cmdStdinIsConsole()) {
                fprintf(stderr, "cat: nothing to read - give it a file, or pipe/redirect something into it\n");
                return 1;
            }
            in = stdin;
        } else {
            in = cmdOpenRead("cat", name);
            if (!in) { status = 1; continue; }
        }

        int rc = catStream(in, &st);
        if (!isStdin) fclose(in);

        if (rc == -1) return 1; // output is gone - stop quietly
        if (rc == -2) {
            fprintf(stderr, "cat: '%s' read error\n", isStdin ? "standard input" : name);
            status = 1;
        }
    }
    return status;
}

int cmd_cat(int argc, char *argv[]) {
    CmdBinaryMode bm;
    cmdBinaryBegin(&bm);
    int rc = catRun(argc, argv);
    cmdBinaryEnd(&bm);
    return rc;
}
