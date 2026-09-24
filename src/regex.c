#include "regex.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Hard limits so a silly pattern like "(a{1000}){1000}" fails cleanly with
// an error instead of eating all the memory.
#define MAX_PROG   20000  // compiled instructions
#define MAX_REPEAT 1000   // largest number allowed inside {n,m}
#define MAX_DEPTH  200    // nesting of ( )

// ---------------------------------------------------------------------------
// Compiled program
// ---------------------------------------------------------------------------

enum {
    OP_CHAR,    // x = byte to match
    OP_CLASS,   // x = index into the class table
    OP_SPLIT,   // continue at both x and y
    OP_JMP,     // continue at x
    OP_MATCH,
    OP_BOL,     // ^
    OP_EOL,     // $
    OP_WORDB,   // \b
    OP_NWORDB,  // \B
    OP_WBEG,    // \<
    OP_WEND,    // \>
    OP_NPW,     // previous character is not a word character  (grep -w)
    OP_NNW      // next character is not a word character      (grep -w)
};

typedef struct { unsigned char bits[32]; } ClassSet;
typedef struct { int op, x, y; } Inst;

struct Regex {
    Inst *code;
    int ncode, capCode;
    ClassSet *classes;
    int nclasses, capClasses;
    int firstByte; // if every match must start with this byte, else -1

    // scratch space for the matcher, allocated once per Regex
    int *clist, *nlist, *mark, *stack;
    size_t *cstart, *nstart;   // where each thread in clist/nlist began matching
    int gen;
};

static void setBit(ClassSet *s, int b)      { s->bits[b >> 3] |= (unsigned char)(1u << (b & 7)); }
static void clearBit(ClassSet *s, int b)    { s->bits[b >> 3] &= (unsigned char)~(1u << (b & 7)); }
static int  hasBit(const ClassSet *s, int b){ return (s->bits[b >> 3] >> (b & 7)) & 1; }

static int isWordByte(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// ---------------------------------------------------------------------------
// Syntax tree
// ---------------------------------------------------------------------------

enum { N_EMPTY, N_CHAR, N_CLASS, N_CAT, N_ALT, N_REP, N_ASSERT };

typedef struct Node {
    int type;
    int val;                 // N_CHAR: byte, N_CLASS: class index, N_ASSERT: OP_*
    int min, max;            // N_REP (max == -1 means "no upper limit")
    struct Node *a, *b;      // children
    struct Node *nextAlloc;  // so the whole tree can be freed in one sweep
} Node;

typedef struct {
    const char *src;
    size_t len, pos;
    int ere, icase;
    Regex *re;
    Node *allocs;
    int depth;
    int contCls;             // cached class index of "UTF-8 continuation byte"
    char *err;
    size_t errSize;
    int failed;
} Parser;

static void setErr(Parser *ps, const char *msg) {
    if (ps->failed) return;
    ps->failed = 1;
    if (ps->err && ps->errSize) snprintf(ps->err, ps->errSize, "%s", msg);
}

static Node *mkNode(Parser *ps, int type, Node *a, Node *b) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) { setErr(ps, "out of memory"); return NULL; }
    n->type = type;
    n->a = a;
    n->b = b;
    n->nextAlloc = ps->allocs;
    ps->allocs = n;
    return n;
}

static Node *mkChar(Parser *ps, int c) {
    Node *n = mkNode(ps, N_CHAR, NULL, NULL);
    if (n) n->val = c;
    return n;
}

static Node *mkAssert(Parser *ps, int op) {
    Node *n = mkNode(ps, N_ASSERT, NULL, NULL);
    if (n) n->val = op;
    return n;
}

static Node *mkRep(Parser *ps, Node *child, int min, int max) {
    Node *n = mkNode(ps, N_REP, child, NULL);
    if (n) { n->min = min; n->max = max; }
    return n;
}

// Adds a class to the table (reusing an identical one if it's there) and
// returns its index, or -1 on failure.
static int addClass(Parser *ps, const ClassSet *set) {
    Regex *re = ps->re;
    for (int i = 0; i < re->nclasses; i++) {
        if (memcmp(&re->classes[i], set, sizeof(ClassSet)) == 0) return i;
    }
    if (re->nclasses == re->capClasses) {
        int cap = re->capClasses ? re->capClasses * 2 : 16;
        ClassSet *grown = (ClassSet *)realloc(re->classes, (size_t)cap * sizeof(ClassSet));
        if (!grown) { setErr(ps, "out of memory"); return -1; }
        re->classes = grown;
        re->capClasses = cap;
    }
    re->classes[re->nclasses] = *set;
    return re->nclasses++;
}

static Node *mkClassNode(Parser *ps, const ClassSet *set) {
    int idx = addClass(ps, set);
    if (idx < 0) return NULL;
    Node *n = mkNode(ps, N_CLASS, NULL, NULL);
    if (n) n->val = idx;
    return n;
}

// "Any number of UTF-8 continuation bytes" - glued on after a class that
// can match a lead byte, so "." and [^x] swallow a whole multi-byte
// character instead of splitting it.
static Node *mkContRun(Parser *ps) {
    if (ps->contCls < 0) {
        ClassSet cont;
        memset(&cont, 0, sizeof cont);
        for (int b = 0x80; b <= 0xBF; b++) setBit(&cont, b);
        ps->contCls = addClass(ps, &cont);
        if (ps->contCls < 0) return NULL;
    }
    Node *c = mkNode(ps, N_CLASS, NULL, NULL);
    if (!c) return NULL;
    c->val = ps->contCls;
    return mkRep(ps, c, 0, -1);
}

static void foldCase(ClassSet *s) {
    for (int c = 'a'; c <= 'z'; c++) {
        int u = c - 'a' + 'A';
        if (hasBit(s, c) || hasBit(s, u)) { setBit(s, c); setBit(s, u); }
    }
}

// Builds the node for a finished class. `negate` flips it, and because a
// negated class can now match the lead byte of a multi-byte character, the
// continuation bytes are appended (and excluded from the class itself).
static Node *classNode(Parser *ps, ClassSet *set, int negate) {
    if (ps->icase) foldCase(set);
    if (!negate) return mkClassNode(ps, set);

    for (int i = 0; i < 32; i++) set->bits[i] = (unsigned char)~set->bits[i];
    for (int b = 0x80; b <= 0xBF; b++) clearBit(set, b);
    Node *cls = mkClassNode(ps, set);
    if (!cls) return NULL;
    Node *run = mkContRun(ps);
    if (!run) return NULL;
    return mkNode(ps, N_CAT, cls, run);
}

static Node *mkAny(Parser *ps) {
    ClassSet none;
    memset(&none, 0, sizeof none);
    setBit(&none, '\n');
    return classNode(ps, &none, 1); // everything except a newline, plus the multi-byte trick
}

static int at(const Parser *ps, size_t off) {
    size_t p = ps->pos + off;
    return p < ps->len ? (unsigned char)ps->src[p] : -1;
}

static int atAlt(const Parser *ps) {
    return ps->ere ? at(ps, 0) == '|' : (at(ps, 0) == '\\' && at(ps, 1) == '|');
}

static int atRpar(const Parser *ps) {
    return ps->ere ? at(ps, 0) == ')' : (at(ps, 0) == '\\' && at(ps, 1) == ')');
}

static Node *parseAlt(Parser *ps);

static Node *parseGroup(Parser *ps) {
    ps->pos += ps->ere ? 1 : 2;
    if (++ps->depth > MAX_DEPTH) { setErr(ps, "too many nested groups"); return NULL; }
    Node *inner = parseAlt(ps);
    if (!inner) return NULL;
    if (!atRpar(ps)) { setErr(ps, "Unmatched ( or \\("); return NULL; }
    ps->pos += ps->ere ? 1 : 2;
    ps->depth--;
    return inner;
}

static int addNamedClass(ClassSet *set, const char *name, size_t n) {
    for (int c = 0; c < 128; c++) {
        int in = 0;
        if      (n == 5 && strncmp(name, "alpha", 5) == 0) in = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        else if (n == 5 && strncmp(name, "digit", 5) == 0) in = c >= '0' && c <= '9';
        else if (n == 5 && strncmp(name, "alnum", 5) == 0) in = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        else if (n == 5 && strncmp(name, "upper", 5) == 0) in = c >= 'A' && c <= 'Z';
        else if (n == 5 && strncmp(name, "lower", 5) == 0) in = c >= 'a' && c <= 'z';
        else if (n == 5 && strncmp(name, "space", 5) == 0) in = c == ' ' || (c >= 9 && c <= 13);
        else if (n == 5 && strncmp(name, "blank", 5) == 0) in = c == ' ' || c == '\t';
        else if (n == 5 && strncmp(name, "punct", 5) == 0) in = c > 32 && c < 127 && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        else if (n == 5 && strncmp(name, "print", 5) == 0) in = c >= 32 && c < 127;
        else if (n == 5 && strncmp(name, "graph", 5) == 0) in = c > 32 && c < 127;
        else if (n == 5 && strncmp(name, "cntrl", 5) == 0) in = c < 32 || c == 127;
        else if (n == 6 && strncmp(name, "xdigit", 6) == 0) in = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        else return 0;
        if (in) setBit(set, c);
    }
    return 1;
}

static Node *parseBracket(Parser *ps) {
    ps->pos++; // the '['
    int negate = 0;
    if (at(ps, 0) == '^') { negate = 1; ps->pos++; }

    ClassSet set;
    memset(&set, 0, sizeof set);
    int firstItem = 1;

    for (;;) {
        int c = at(ps, 0);
        if (c < 0) { setErr(ps, "Unmatched [, [^, [:, [., or [="); return NULL; }
        if (c == ']' && !firstItem) { ps->pos++; break; }
        firstItem = 0;

        if (c == '[' && at(ps, 1) == ':') {
            size_t p = ps->pos + 2, q = p;
            while (q < ps->len && ps->src[q] != ':') q++;
            if (q + 1 < ps->len && ps->src[q + 1] == ']') {
                if (!addNamedClass(&set, ps->src + p, q - p)) {
                    setErr(ps, "Invalid character class name");
                    return NULL;
                }
                ps->pos = q + 2;
                continue;
            }
            // no closing ":]" - the '[' is just a character, handled below
        }
        if (c == '[' && (at(ps, 1) == '.' || at(ps, 1) == '=')) {
            setErr(ps, "Collating symbols and equivalence classes are not supported");
            return NULL;
        }
        if (c >= 0x80) {
            setErr(ps, "Non-ASCII characters inside [ ] are not supported");
            return NULL;
        }

        ps->pos++;
        int lo = c;
        if (at(ps, 0) == '-' && at(ps, 1) >= 0 && at(ps, 1) != ']') {
            int hi = at(ps, 1);
            if (hi >= 0x80) {
                setErr(ps, "Non-ASCII characters inside [ ] are not supported");
                return NULL;
            }
            if (hi == '[' && (at(ps, 2) == ':' || at(ps, 2) == '.' || at(ps, 2) == '=')) {
                setErr(ps, "Invalid range end");
                return NULL;
            }
            ps->pos += 2;
            if (hi < lo) { setErr(ps, "Invalid range end"); return NULL; }
            for (int x = lo; x <= hi; x++) setBit(&set, x);
        } else {
            setBit(&set, lo);
        }
    }
    return classNode(ps, &set, negate);
}

static Node *shorthandClass(Parser *ps, int letter) {
    ClassSet set;
    memset(&set, 0, sizeof set);
    int negate = (letter >= 'A' && letter <= 'Z');
    switch (letter | 0x20) {
    case 'w':
        for (int c = 0; c < 128; c++) if (isWordByte(c)) setBit(&set, c);
        break;
    case 's':
        setBit(&set, ' ');
        for (int c = 9; c <= 13; c++) setBit(&set, c);
        break;
    case 'd':
        for (int c = '0'; c <= '9'; c++) setBit(&set, c);
        break;
    }
    return classNode(ps, &set, negate);
}

static Node *parseEscape(Parser *ps) {
    int n = at(ps, 1);
    if (n < 0) { setErr(ps, "Trailing backslash"); return NULL; }

    if (!ps->ere) {
        if (n == '(') return parseGroup(ps);
        if (n == ')') { setErr(ps, "Unmatched ) or \\)"); return NULL; }
        if (n == '{') { setErr(ps, "Invalid preceding regular expression"); return NULL; }
    }
    ps->pos += 2;
    switch (n) {
    case 'w': case 'W': case 's': case 'S': case 'd': case 'D':
        return shorthandClass(ps, n);
    case 'b': return mkAssert(ps, OP_WORDB);
    case 'B': return mkAssert(ps, OP_NWORDB);
    case '<': return mkAssert(ps, OP_WBEG);
    case '>': return mkAssert(ps, OP_WEND);
    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
        setErr(ps, "Back-references are not supported");
        return NULL;
    default:
        return mkChar(ps, n); // an escaped literal: \. \* \\ \[ ...
    }
}

// True when the '$' at ps->pos is at the end of a branch (BRE only treats
// '$' as an anchor there; anywhere else it's an ordinary character).
static int dollarEndsBranch(const Parser *ps) {
    size_t p = ps->pos + 1;
    if (p >= ps->len) return 1;
    return ps->src[p] == '\\' && p + 1 < ps->len &&
           (ps->src[p + 1] == ')' || ps->src[p + 1] == '|');
}

static Node *parseAtom(Parser *ps, int first) {
    int c = at(ps, 0);
    if (c == '\\') return parseEscape(ps);
    if (c == '.')  { ps->pos++; return mkAny(ps); }
    if (c == '[')  return parseBracket(ps);

    if (ps->ere) {
        if (c == '(') return parseGroup(ps);
        if (c == ')') { setErr(ps, "Unmatched ) or \\)"); return NULL; }
        if (c == '^') { ps->pos++; return mkAssert(ps, OP_BOL); }
        if (c == '$') { ps->pos++; return mkAssert(ps, OP_EOL); }
    } else {
        if (c == '^' && first)            { ps->pos++; return mkAssert(ps, OP_BOL); }
        if (c == '$' && dollarEndsBranch(ps)) { ps->pos++; return mkAssert(ps, OP_EOL); }
    }

    // Everything else is a literal - including "*", "+", "?" and "{" when
    // there's nothing before them to repeat.
    ps->pos++;
    return mkChar(ps, c);
}

// Tries to read an interval ({n}, {n,}, {n,m}) at the current position.
// Returns 1 and advances if it found one. Returns 0 if there isn't one
// (for ERE a "{" that doesn't start a valid interval is just a literal).
// For BRE a malformed \{ ... \} is an error, signalled by ps->failed.
static int tryInterval(Parser *ps, int *min, int *max) {
    size_t p = ps->pos;
    if (ps->ere) {
        if (at(ps, 0) != '{') return 0;
        p += 1;
    } else {
        if (!(at(ps, 0) == '\\' && at(ps, 1) == '{')) return 0;
        p += 2;
    }

    long lo = 0, hi = -1;
    int haveLo = 0, bad = 0;
    while (p < ps->len && ps->src[p] >= '0' && ps->src[p] <= '9') {
        lo = lo * 10 + (ps->src[p] - '0');
        if (lo > MAX_REPEAT) { setErr(ps, "Regular expression too big"); return 0; }
        haveLo = 1;
        p++;
    }
    if (p < ps->len && ps->src[p] == ',') {
        p++;
        int haveHi = 0;
        hi = 0;
        while (p < ps->len && ps->src[p] >= '0' && ps->src[p] <= '9') {
            hi = hi * 10 + (ps->src[p] - '0');
            if (hi > MAX_REPEAT) { setErr(ps, "Regular expression too big"); return 0; }
            haveHi = 1;
            p++;
        }
        if (!haveHi) hi = -1;      // {n,}
    } else {
        if (!haveLo) bad = 1;      // "{}" or "{x"
        hi = lo;                   // {n}
    }

    if (ps->ere) {
        if (p < ps->len && ps->src[p] == '}') p += 1; else bad = 1;
    } else {
        if (p + 1 < ps->len && ps->src[p] == '\\' && ps->src[p + 1] == '}') p += 2; else bad = 1;
    }
    if (!bad && hi != -1 && hi < lo) {
        // Well-formed, but nonsense ({2,1}) - that's an error in both flavours.
        setErr(ps, "Invalid content of \\{\\}");
        return 0;
    }

    if (bad) {
        if (!ps->ere) setErr(ps, "Invalid content of \\{\\}");
        return 0;
    }
    ps->pos = p;
    *min = (int)lo;
    *max = (int)hi;
    return 1;
}

static Node *parseRepeat(Parser *ps, int first) {
    size_t start = ps->pos;
    Node *atom = parseAtom(ps, first);
    if (!atom) return NULL;

    // In BRE, a '*' right after a leading '^' is a literal star, not a
    // repeat of the anchor - so don't look for repeat operators here.
    if (!ps->ere && ps->src[start] == '^' && atom->type == N_ASSERT) return atom;

    for (;;) {
        int min, max;
        if (at(ps, 0) == '*') {
            ps->pos += 1; min = 0; max = -1;
        } else if (ps->ere && at(ps, 0) == '+') {
            ps->pos += 1; min = 1; max = -1;
        } else if (ps->ere && at(ps, 0) == '?') {
            ps->pos += 1; min = 0; max = 1;
        } else if (!ps->ere && at(ps, 0) == '\\' && at(ps, 1) == '+') {
            ps->pos += 2; min = 1; max = -1;
        } else if (!ps->ere && at(ps, 0) == '\\' && at(ps, 1) == '?') {
            ps->pos += 2; min = 0; max = 1;
        } else if (tryInterval(ps, &min, &max)) {
            // consumed
        } else {
            if (ps->failed) return NULL;
            break;
        }
        atom = mkRep(ps, atom, min, max);
        if (!atom) return NULL;
    }
    return atom;
}

static Node *parseConcat(Parser *ps) {
    Node *result = NULL;
    int first = 1;
    while (ps->pos < ps->len && !atAlt(ps) && !(ps->depth > 0 && atRpar(ps))) {
        Node *atom = parseRepeat(ps, first);
        if (!atom) return NULL;
        first = 0;
        result = result ? mkNode(ps, N_CAT, result, atom) : atom;
        if (!result) return NULL;
    }
    return result ? result : mkNode(ps, N_EMPTY, NULL, NULL);
}

static Node *parseAlt(Parser *ps) {
    Node *left = parseConcat(ps);
    if (!left) return NULL;
    while (atAlt(ps)) {
        ps->pos += ps->ere ? 1 : 2;
        Node *right = parseConcat(ps);
        if (!right) return NULL;
        left = mkNode(ps, N_ALT, left, right);
        if (!left) return NULL;
    }
    return left;
}

static Node *buildLiteral(Parser *ps) {
    Node *result = NULL;
    for (size_t i = 0; i < ps->len; i++) {
        Node *c = mkChar(ps, (unsigned char)ps->src[i]);
        if (!c) return NULL;
        result = result ? mkNode(ps, N_CAT, result, c) : c;
        if (!result) return NULL;
    }
    return result ? result : mkNode(ps, N_EMPTY, NULL, NULL);
}

// ---------------------------------------------------------------------------
// Code generation
// ---------------------------------------------------------------------------

typedef struct {
    Parser *ps;
    Regex *re;
    int icase;
    int failed;
} Gen;

static int emit(Gen *g, int op, int x, int y) {
    Regex *re = g->re;
    if (g->failed) return 0;
    if (re->ncode >= MAX_PROG) {
        g->failed = 1;
        setErr(g->ps, "Regular expression too big");
        return 0;
    }
    if (re->ncode == re->capCode) {
        int cap = re->capCode ? re->capCode * 2 : 64;
        Inst *grown = (Inst *)realloc(re->code, (size_t)cap * sizeof(Inst));
        if (!grown) { g->failed = 1; setErr(g->ps, "out of memory"); return 0; }
        re->code = grown;
        re->capCode = cap;
    }
    re->code[re->ncode].op = op;
    re->code[re->ncode].x = x;
    re->code[re->ncode].y = y;
    return re->ncode++;
}

static void gen(Gen *g, const Node *n);

static void genRep(Gen *g, const Node *n) {
    for (int i = 0; i < n->min; i++) gen(g, n->a);

    if (n->max == -1) {
        int split = emit(g, OP_SPLIT, 0, 0);
        int body = g->re->ncode;
        gen(g, n->a);
        emit(g, OP_JMP, split, 0);
        if (g->failed) return;
        g->re->code[split].x = body;
        g->re->code[split].y = g->re->ncode;
    } else {
        // (max - min) optional copies, all of which jump to the same place
        // when skipped. The pending SPLITs are chained through their own y
        // field until that place is known, so this needs no array.
        int pending = -1;
        for (int i = n->min; i < n->max; i++) {
            int split = emit(g, OP_SPLIT, 0, 0);
            if (g->failed) return;
            g->re->code[split].x = g->re->ncode;
            g->re->code[split].y = pending;
            pending = split;
            gen(g, n->a);
        }
        if (g->failed) return;
        while (pending != -1) {
            int next = g->re->code[pending].y;
            g->re->code[pending].y = g->re->ncode;
            pending = next;
        }
    }
}

static void gen(Gen *g, const Node *n) {
    if (g->failed) return;
    switch (n->type) {
    case N_EMPTY:
        break;
    case N_CHAR:
        if (g->icase && ((n->val >= 'a' && n->val <= 'z') || (n->val >= 'A' && n->val <= 'Z'))) {
            ClassSet set;
            memset(&set, 0, sizeof set);
            setBit(&set, n->val);
            foldCase(&set);
            int idx = addClass(g->ps, &set);
            if (idx < 0) { g->failed = 1; return; }
            emit(g, OP_CLASS, idx, 0);
        } else {
            emit(g, OP_CHAR, n->val, 0);
        }
        break;
    case N_CLASS:
        emit(g, OP_CLASS, n->val, 0);
        break;
    case N_ASSERT:
        emit(g, n->val, 0, 0);
        break;
    case N_CAT:
        gen(g, n->a);
        gen(g, n->b);
        break;
    case N_ALT: {
        int split = emit(g, OP_SPLIT, 0, 0);
        if (g->failed) return;
        g->re->code[split].x = g->re->ncode;
        gen(g, n->a);
        int jmp = emit(g, OP_JMP, 0, 0);
        if (g->failed) return;
        g->re->code[split].y = g->re->ncode;
        gen(g, n->b);
        if (g->failed) return;
        g->re->code[jmp].x = g->re->ncode;
        break;
    }
    case N_REP:
        genRep(g, n);
        break;
    }
}

static void freeNodes(Node *n) {
    while (n) {
        Node *next = n->nextAlloc;
        free(n);
        n = next;
    }
}

void reFree(Regex *re) {
    if (!re) return;
    free(re->code);
    free(re->classes);
    free(re->clist);
    free(re->nlist);
    free(re->mark);
    free(re->stack);
    free(re->cstart);
    free(re->nstart);
    free(re);
}

Regex *reCompile(const char *pattern, int flags, char *err, size_t errSize) {
    Regex *re = (Regex *)calloc(1, sizeof(Regex));
    if (!re) {
        if (err && errSize) snprintf(err, errSize, "out of memory");
        return NULL;
    }

    Parser ps;
    memset(&ps, 0, sizeof ps);
    ps.src = pattern;
    ps.len = strlen(pattern);
    ps.ere = (flags & RE_EXTENDED) != 0;
    ps.icase = (flags & RE_ICASE) != 0;
    ps.re = re;
    ps.contCls = -1;
    ps.err = err;
    ps.errSize = errSize;
    if (err && errSize) err[0] = '\0';

    Node *root;
    if (flags & RE_LITERAL) {
        root = buildLiteral(&ps);
    } else {
        root = parseAlt(&ps);
        if (root && ps.pos < ps.len) { setErr(&ps, "Unmatched ) or \\)"); root = NULL; }
    }

    if (root && (flags & RE_WORD)) {
        Node *pre = mkAssert(&ps, OP_NPW);
        Node *post = mkAssert(&ps, OP_NNW);
        Node *mid = (pre && post) ? mkNode(&ps, N_CAT, root, post) : NULL;
        root = mid ? mkNode(&ps, N_CAT, pre, mid) : NULL;
    }

    if (root) {
        Gen g;
        g.ps = &ps;
        g.re = re;
        g.icase = ps.icase;
        g.failed = 0;
        gen(&g, root);
        emit(&g, OP_MATCH, 0, 0);
        if (g.failed) root = NULL;
    }
    freeNodes(ps.allocs);

    if (!root) {
        if (err && errSize && !err[0]) snprintf(err, errSize, "invalid regular expression");
        reFree(re);
        return NULL;
    }

    re->firstByte = (re->ncode > 0 && re->code[0].op == OP_CHAR) ? re->code[0].x : -1;

    size_t n = (size_t)re->ncode;
    re->clist = (int *)malloc(n * sizeof(int));
    re->nlist = (int *)malloc(n * sizeof(int));
    re->mark  = (int *)calloc(n, sizeof(int));
    re->stack = (int *)malloc((2 * n + 2) * sizeof(int));
    re->cstart = (size_t *)malloc(n * sizeof(size_t));
    re->nstart = (size_t *)malloc(n * sizeof(size_t));
    if (!re->clist || !re->nlist || !re->mark || !re->stack || !re->cstart || !re->nstart) {
        if (err && errSize) snprintf(err, errSize, "out of memory");
        reFree(re);
        return NULL;
    }
    re->gen = 0;
    return re;
}

// ---------------------------------------------------------------------------
// Matching (Pike VM)
// ---------------------------------------------------------------------------

static void bumpGen(Regex *re) {
    if (++re->gen == INT_MAX) {
        memset(re->mark, 0, (size_t)re->ncode * sizeof(int));
        re->gen = 1;
    }
}

// Adds pc - and everything reachable from it without consuming input - to
// `list`, evaluating zero-width assertions against position `pos`. Uses an
// explicit stack (sized 2 * ncode + 2, which is enough because each
// instruction is expanded at most once per generation).
static void addThread(Regex *re, int *list, size_t *starts, int *count, int pc0, size_t start,
                      const unsigned char *s, size_t len, size_t pos) {
    int *stack = re->stack;
    int sp = 0;
    stack[sp++] = pc0;

    int prevWord = pos > 0 && isWordByte(s[pos - 1]);
    int nextWord = pos < len && isWordByte(s[pos]);

    while (sp > 0) {
        int pc = stack[--sp];
        if (re->mark[pc] == re->gen) continue;
        re->mark[pc] = re->gen;

        const Inst *in = &re->code[pc];
        switch (in->op) {
        case OP_JMP:    stack[sp++] = in->x; break;
        case OP_SPLIT:  stack[sp++] = in->y; stack[sp++] = in->x; break;
        case OP_BOL:    if (pos == 0)   stack[sp++] = pc + 1; break;
        case OP_EOL:    if (pos == len) stack[sp++] = pc + 1; break;
        case OP_WORDB:  if (prevWord != nextWord)             stack[sp++] = pc + 1; break;
        case OP_NWORDB: if (prevWord == nextWord)             stack[sp++] = pc + 1; break;
        case OP_WBEG:   if (!prevWord && nextWord)            stack[sp++] = pc + 1; break;
        case OP_WEND:   if (prevWord && !nextWord)            stack[sp++] = pc + 1; break;
        case OP_NPW:    if (!prevWord)                        stack[sp++] = pc + 1; break;
        case OP_NNW:    if (!nextWord)                        stack[sp++] = pc + 1; break;
        default:        // CHAR, CLASS, MATCH
            starts[*count] = start;
            list[(*count)++] = pc;
            break;
        }
    }
}

// One pass over the text finds the leftmost-longest match. Every thread
// remembers where its match began; threads are kept in order of start
// position, and when two threads land on the same instruction the earlier
// one wins (they'd behave identically from there on, so the later one is
// redundant). That makes the whole search linear in the text instead of
// retrying from every starting position.
//
// With `anchored` set, only a match beginning exactly at `from` counts.
static int runSearch(Regex *re, const unsigned char *s, size_t len, size_t from,
                     int anchored, size_t *msOut, size_t *meOut) {
    int *clist = re->clist, *nlist = re->nlist;
    size_t *cst = re->cstart, *nst = re->nstart;
    int cn = 0, found = 0;
    size_t bestS = 0, bestE = 0, pos = from;

    if (from > len) return 0;

    bumpGen(re);
    addThread(re, clist, cst, &cn, 0, from, s, len, from);

    for (;;) {
        if (cn == 0) {
            if (found || anchored || pos >= len) break;
            // Nothing alive and nothing found yet: skip ahead to the next
            // position where a match could even begin.
            pos++;
            if (re->firstByte >= 0) {
                const unsigned char *p = pos < len
                    ? (const unsigned char *)memchr(s + pos, re->firstByte, len - pos)
                    : NULL;
                if (!p) break;
                pos = (size_t)(p - s);
            }
            bumpGen(re);
            addThread(re, clist, cst, &cn, 0, pos, s, len, pos);
            continue;
        }

        bumpGen(re);
        int nn = 0;
        for (int i = 0; i < cn; i++) {
            size_t st = cst[i];
            if (found && st > bestS) continue; // can't beat the match we already have
            const Inst *in = &re->code[clist[i]];
            switch (in->op) {
            case OP_MATCH:
                // Keep going after a match instead of stopping: an earlier
                // start still wins, and for the same start we want the
                // longest end.
                if (!found || st < bestS || (st == bestS && pos > bestE)) {
                    found = 1;
                    bestS = st;
                    bestE = pos;
                }
                break;
            case OP_CHAR:
                if (pos < len && s[pos] == (unsigned)in->x)
                    addThread(re, nlist, nst, &nn, clist[i] + 1, st, s, len, pos + 1);
                break;
            case OP_CLASS:
                if (pos < len && hasBit(&re->classes[in->x], s[pos]))
                    addThread(re, nlist, nst, &nn, clist[i] + 1, st, s, len, pos + 1);
                break;
            }
        }
        if (pos >= len) break; // nothing left to consume

        // A new match may also begin at the next position - added last, so
        // it ranks behind every thread that started earlier.
        if (!found && !anchored)
            addThread(re, nlist, nst, &nn, 0, pos + 1, s, len, pos + 1);

        int *tl = clist; clist = nlist; nlist = tl;
        size_t *ts = cst; cst = nst; nst = ts;
        cn = nn;
        pos++;
    }

    if (found) { *msOut = bestS; *meOut = bestE; }
    return found;
}

int reMatchAt(Regex *re, const char *str, size_t len, size_t at, size_t *matchEnd) {
    size_t ms;
    return runSearch(re, (const unsigned char *)str, len, at, 1, &ms, matchEnd);
}

int reSearch(Regex *re, const char *str, size_t len, size_t from,
             size_t *matchStart, size_t *matchEnd) {
    return runSearch(re, (const unsigned char *)str, len, from, 0, matchStart, matchEnd);
}
