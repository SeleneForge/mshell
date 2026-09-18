#ifndef MSHELL_H
#define MSHELL_H

// Tries to run argv[0] as a built-in command. Returns -1 if argv[0]
// doesn't match any built-in (the caller should then try PATH).
// Otherwise runs it and returns its exit status (0 = success, matching
// the convention external processes use) - this is what makes "&&" and
// "||" chaining mean anything for built-ins, not just external
// programs. Shared between mshell.c's plain single-command path and
// pipeline.c, so the two never end up with two different ideas of which
// names are built in.
int runBuiltin(int argc, char *argv[]);

#endif // MSHELL_H
