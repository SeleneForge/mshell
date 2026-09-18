#ifndef PIPELINE_H
#define PIPELINE_H

#include "tokenize.h"

// A pipeline is a series of commands chained with "|", where each stage
// may additionally redirect its input from a file ("<") and/or its
// output to a file (">" truncates, ">>" appends).
#define PIPELINE_MAX_STAGES 16

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    const char *inFile;  // NULL if this stage has no "<"
    const char *outFile; // NULL if this stage has no ">" / ">>"
    int append;          // 1 for ">>", 0 for ">" - meaningless if outFile is NULL
} PipelineStage;

typedef struct {
    PipelineStage stages[PIPELINE_MAX_STAGES];
    int stageCount;
} Pipeline;

// Splits already-tokenized argv/argc into pipeline stages, pulling out
// "|", "<", ">" and ">>" tokens (and the filename that follows a
// redirection operator) so each stage's argv/argc is just the plain
// command + its own arguments. Returns 1 on success. On a syntax error
// (a stray/duplicated "|", a redirection with no filename after it, or
// too many stages/arguments) it returns 0 and writes a human-readable
// message into errMsg.
int pipelineParse(char *argv[], int argc, Pipeline *pipeline, char *errMsg, size_t errMsgSize);

// Runs every stage of the pipeline, wiring up files and pipes between
// stages as needed, and waits for it to finish. Builtins run in-process
// (with their stdin/stdout temporarily redirected); anything else is
// looked up on PATH and spawned the same way a plain external command
// would be, just with explicit std handles instead of inheriting the
// console directly.
//
// Returns the exit status of the pipeline's *last* stage (0 = success),
// same convention real shells use for a pipeline's overall status - so
// `a | b` succeeds or fails based on b, regardless of whether a
// succeeded. A stage that can't even be found on PATH counts as status
// 127, and one that's found but fails to actually start counts as 126 -
// both matching what bash uses these codes for.
int pipelineExecute(Pipeline *pipeline);

#endif // PIPELINE_H
