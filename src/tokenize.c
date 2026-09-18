#include <string.h>
#include "tokenize.h"

// Makes sure every pipe/redirection/chaining operator (|, <, >, >>, &&,
// ||, ;, and bare &) is surrounded by spaces, so the plain
// space-delimited scan below always sees them as their own token - even
// when typed with no spaces at all, like "ls>out.txt" or
// "mkdir foo&&cd foo". Text inside "quotes" is left alone, so
// `echo "a|b"` still keeps its literal pipe character.
//
// This shifts bytes within the buffer (there's no separator character to
// sacrifice the way there is between two space-separated words), so it
// needs the buffer's real capacity, not just its current length, to know
// how much room it has to grow into. If a line is packed so tightly
// there's truly no room left to insert a padding space, it just stops
// padding early rather than overflow - that line's operators would only
// get misparsed if they were *also* glued to their neighbors with zero
// spaces anywhere to spare, which is an extreme edge case for a 1024-byte
// input line.
static void padOperators(char *buf, size_t cap) {
    size_t len = strlen(buf);
    int inQuotes = 0;

    for (size_t i = 0; i < len; ) {
        char c = buf[i];

        if (c == '"') {
            inQuotes = !inQuotes;
            i++;
            continue;
        }
        if (inQuotes || (c != '|' && c != '<' && c != '>' && c != '&' && c != ';')) {
            i++;
            continue;
        }

        // ">>", "||" and "&&" are each one two-character operator; every
        // other operator (including a lone "&", which isn't meaningful
        // here - there's no background-job support - but still needs to
        // land on its own token so the parser can reject it clearly) is
        // one character.
        int opLen = 1;
        if ((c == '>' || c == '|' || c == '&') && i + 1 < len && buf[i + 1] == c) {
            opLen = 2;
        }

        // Pad the trailing edge first so the leading-edge insertion below
        // (if it happens) doesn't shift this index out from under us.
        size_t after = i + (size_t)opLen;
        if (after < len && buf[after] != ' ') {
            if (len + 2 > cap) break; // no room left - leave the rest as-is
            memmove(buf + after + 1, buf + after, len - after + 1); // +1 carries the NUL along
            buf[after] = ' ';
            len++;
        }

        if (i > 0 && buf[i - 1] != ' ') {
            if (len + 2 > cap) break;
            memmove(buf + i + 1, buf + i, len - i + 1);
            buf[i] = ' ';
            len++;
            i++; // re-sync: the operator itself just moved one byte to the right
        }

        i += (size_t)opLen; // move past the operator
    }
}

int tokenize(char *input, size_t inputCap, char *argv[MAX_ARGS]) {
    padOperators(input, inputCap);

    int argc = 0;
    char *p = input;

    while (*p != '\0' && argc < MAX_ARGS) {
        // skip spaces between tokens
        while (*p == ' ') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            // quoted token: "folder with space" -> one token, quotes stripped
            p++; // skip opening quote
            argv[argc++] = p;

            char *end = strchr(p, '"');
            if (end == NULL) {
                // unterminated quote - just take the rest of the string
                break;
            }

            *end = '\0';
            p = end + 1;
        } else {
            // plain token: ends at the next space (padOperators above
            // guarantees |, <, >, >> always land on their own this way too)
            argv[argc++] = p;

            while (*p != ' ' && *p != '\0') p++;
            if (*p == ' ') {
                *p = '\0';
                p++;
            }
        }
    }

    return argc;
}
