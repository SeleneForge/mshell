#ifndef CHAIN_H
#define CHAIN_H

#include "pipeline.h"

// A command chain is a sequence of pipelines joined by "&&", "||" or
// ";", e.g. `mkdir foo && cd foo && ls` or `a | b || c ; d`. Each link's
// precedingOp says how it's connected to the link before it (the first
// link's is unused - there's nothing before it to connect to).
#define CHAIN_MAX_LINKS 32

typedef enum {
    CHAIN_OP_NONE = 0, // first link only - nothing precedes it
    CHAIN_OP_AND,      // "&&" - run only if the previous link succeeded
    CHAIN_OP_OR,       // "||" - run only if the previous link failed
    CHAIN_OP_SEQ,      // ";"  - always run, regardless of the previous link
} ChainOp;

typedef struct {
    Pipeline pipeline;
    ChainOp precedingOp;
} ChainLink;

typedef struct {
    ChainLink links[CHAIN_MAX_LINKS];
    int linkCount;
} CommandChain;

// True if any of the given tokens is a pipe, redirection, or chaining
// operator ("|", "<", ">", ">>", "&&", "||", ";", or a bare "&"). mshell.c
// uses this right after tokenize() to decide whether a line needs any of
// this machinery at all, or can take the plain single-command path
// exactly as before.
int chainHasOperators(char *argv[], int argc);

// Splits already-tokenized argv/argc on "&&"/"||"/";" into links, then
// pipeline-parses each link's own tokens (so "|"/"<"/">"/">>" inside one
// link still work exactly as pipelineParse alone would handle them).
// Returns 1 on success. On a syntax error - a stray/duplicated operator,
// a bare "&" (background jobs aren't supported), too many links, or a
// syntax error within one of the links' own pipeline - returns 0 and
// writes a human-readable message into errMsg. Nothing runs until the
// whole line has parsed successfully, same as pipelineParse's contract.
int chainParse(char *argv[], int argc, CommandChain *chain, char *errMsg, size_t errMsgSize);

// Runs each link in order, skipping a link when its preceding operator
// says to (an "&&" link is skipped once something has already failed; an
// "||" link is skipped once something has already succeeded; ";" always
// runs). Returns the exit status of the last link that actually ran.
int chainExecute(CommandChain *chain);

#endif // CHAIN_H
