#ifndef TOKENIZE_H
#define TOKENIZE_H

#include <stddef.h>

#define MAX_ARGS 64

// inputCap is the *total capacity* of the input buffer (e.g. sizeof(input)
// at the call site), not just its current string length. tokenize() may
// need to shift bytes rightward in place to give pipe/redirection
// operators (|, <, >, >>) their own token even when the user types them
// glued to other text (ls>out.txt), so it needs to know how much slack
// room is left in the buffer before it can safely do that.
int tokenize(char *input, size_t inputCap, char *argv[MAX_ARGS]);

#endif // TOKENIZE_H
