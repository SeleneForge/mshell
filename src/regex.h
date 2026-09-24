#ifndef REGEX_H
#define REGEX_H

#include <stddef.h>

// A small, self-contained regular expression engine (no POSIX <regex.h> on
// native Windows, and no dependencies on anything else in this shell).
//
// It's a Thompson-style NFA simulated in lockstep (a "Pike VM"), so matching
// time is linear in the text - there's no catastrophic backtracking, no
// matter what pattern gets typed in.
//
// Supported syntax
//   .            any character
//   [abc] [a-z]  bracket expression, with [^...] negation and [:alpha:]-style
//                classes (alpha digit alnum upper lower space blank punct
//                print graph cntrl xdigit)
//   * + ? {n,m}  repetition
//   ^ $          start / end of line
//   ( ) |        grouping / alternation (no capture groups)
//   \w \W \s \S \d \D   word / whitespace / digit shorthands
//   \b \B \< \>  word-boundary assertions
//
// Two flavours, like grep: the default is "basic" (BRE), where ( ) | + ? { }
// are plain characters and \( \) \| \+ \? \{ \} are the operators; with
// RE_EXTENDED (ERE) it's the other way around.
//
// Not supported: back-references (\1), and collating symbols ([.x.], [=x=]).
// It works on bytes, with one nicety: "." and negated classes consume a whole
// UTF-8 character rather than half of one. Non-ASCII characters *inside*
// [...] are rejected with an error instead of silently misbehaving.

typedef struct Regex Regex;

#define RE_ICASE    0x01  // ignore ASCII case
#define RE_EXTENDED 0x02  // ERE syntax instead of BRE
#define RE_LITERAL  0x04  // pattern is a plain string, nothing is special
#define RE_WORD     0x08  // match must not touch a word character on either side

// Compiles `pattern`. On failure returns NULL and, if err is non-NULL,
// writes a short message into it.
Regex *reCompile(const char *pattern, int flags, char *err, size_t errSize);

void reFree(Regex *re);

// Finds the leftmost-longest match in s[0..len) starting the search at
// offset `from`. Returns 1 and fills *matchStart/*matchEnd on success.
// Assertions like ^ and \b always look at the whole of s, not just s+from.
int reSearch(Regex *re, const char *s, size_t len, size_t from,
             size_t *matchStart, size_t *matchEnd);

// Like reSearch, but the match must begin exactly at `at`. Returns 1 and
// fills *matchEnd with the end of the longest such match.
int reMatchAt(Regex *re, const char *s, size_t len, size_t at, size_t *matchEnd);

#endif // REGEX_H
