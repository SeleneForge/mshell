#define _CRT_SECURE_NO_WARNINGS
#include "chain.h"
#include <stdio.h>
#include <string.h>

static int isChainToken(const char *tok, ChainOp *outOp) {
    if (strcmp(tok, "&&") == 0) { *outOp = CHAIN_OP_AND; return 1; }
    if (strcmp(tok, "||") == 0) { *outOp = CHAIN_OP_OR; return 1; }
    if (strcmp(tok, ";") == 0) { *outOp = CHAIN_OP_SEQ; return 1; }
    return 0;
}

int chainHasOperators(char *argv[], int argc) {
    for (int i = 0; i < argc; i++) {
        ChainOp op;
        if (isChainToken(argv[i], &op)) return 1;
        if (strcmp(argv[i], "&") == 0) return 1;
        if (strcmp(argv[i], "|") == 0 || strcmp(argv[i], "<") == 0 ||
            strcmp(argv[i], ">") == 0 || strcmp(argv[i], ">>") == 0) {
            return 1;
        }
    }
    return 0;
}

int chainParse(char *argv[], int argc, CommandChain *chain, char *errMsg, size_t errMsgSize) {
    chain->linkCount = 0;

    int segStart = 0;
    ChainOp pendingOp = CHAIN_OP_NONE; // operator that will connect the *next* link to this one

    for (int i = 0; i <= argc; i++) {
        int isEnd = (i == argc);
        ChainOp thisOp = CHAIN_OP_NONE;

        if (!isEnd) {
            if (strcmp(argv[i], "&") == 0) {
                snprintf(errMsg, errMsgSize, "syntax error: background jobs ('&') aren't supported");
                return 0;
            }
            if (!isChainToken(argv[i], &thisOp)) {
                continue; // still part of the current link - handled by pipelineParse below
            }
        }

        if (isEnd || thisOp != CHAIN_OP_NONE) {
            int segLen = i - segStart;
            if (segLen == 0) {
                snprintf(errMsg, errMsgSize, "syntax error: expected a command%s",
                         isEnd ? " after operator" : " before operator");
                return 0;
            }
            if (chain->linkCount >= CHAIN_MAX_LINKS) {
                snprintf(errMsg, errMsgSize, "too many chained commands (max %d)", CHAIN_MAX_LINKS);
                return 0;
            }

            ChainLink *link = &chain->links[chain->linkCount];
            link->precedingOp = pendingOp;
            if (!pipelineParse(&argv[segStart], segLen, &link->pipeline, errMsg, errMsgSize)) {
                return 0;
            }
            chain->linkCount++;

            pendingOp = thisOp;
            segStart = i + 1;
        }
    }

    return 1;
}

int chainExecute(CommandChain *chain) {
    int lastStatus = 0;

    for (int i = 0; i < chain->linkCount; i++) {
        ChainLink *link = &chain->links[i];

        int shouldRun;
        switch (link->precedingOp) {
            case CHAIN_OP_AND: shouldRun = (lastStatus == 0); break;
            case CHAIN_OP_OR:  shouldRun = (lastStatus != 0); break;
            case CHAIN_OP_SEQ:
            case CHAIN_OP_NONE:
            default:           shouldRun = 1; break;
        }

        if (shouldRun) {
            lastStatus = pipelineExecute(&link->pipeline);
        }
        // else: a skipped link leaves lastStatus exactly as it was, so a
        // later operator still sees whatever last *actually ran* -
        // matching how `a && b || c` runs c when a fails even though b
        // (skipped) never touched the status.
    }

    return lastStatus;
}
