#ifndef COMMAND_H
#define COMMAND_H

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define _CRT_SECURE_NO_WARNINGS

#define MAX_ENTRIES 1024

// Every built-in returns 0 on success and a non-zero status otherwise -
// this is what makes "&&" and "||" chaining mean anything for built-ins
// instead of just external programs. The exact non-zero value isn't
// meaningful beyond "not zero" (this shell doesn't expose $?), so they
// all just use 1 for "failed".

// Basic command.
int cmd_echo(int argc, char *argv[]);
int cmd_date(int argc, char *argv[]);
int cmd_clear(int argc, char *argv[]);
int cmd_help(int argc, char *argv[]);

// File and directory.
int cmd_cd(int argc, char *argv[]);
int cmd_ls(int argc, char *argv[]);
int cmd_blank(int argc, char *argv[]);
int cmd_mkdir(int argc, char *argv[]);
int cmd_rm(int argc, char *argv[]);
int cmd_rmdir(int argc, char *argv[]);
int cmd_pcd(int argc, char *argv[]);
int cmd_mv(int argc, char *argv[]);
int cmd_cp(int argc, char *argv[]);

#endif // COMMAND_H
