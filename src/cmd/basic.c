#define _CRT_SECURE_NO_WARNINGS
#include "command.h"
#include <time.h>

// echo
int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s%s", argv[i], (i < argc - 1) ? " " : "");
    }
    printf("\n");
    return 0;
}

// date/time

int cmd_date(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    time_t raw_time = time(NULL);
    struct tm *info = localtime(&raw_time);

    // 1. Add this array of day names
    const char *days[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };

    int year = info->tm_year + 1900;
    int month = info->tm_mon + 1;
    int day = info->tm_mday;

    int hour = info->tm_hour;
    int minute = info->tm_min;
    int second = info->tm_sec;

    // 2. Print the day name using info->tm_wday as the index
    printf("\nToday is: %s, ", days[info->tm_wday]);
    printf("%02d/%02d/%d, ", day, month, year);
    printf("%02d:%02d:%02d\n\n", hour, minute, second);
    return 0;
}

// clear
int cmd_clear(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // \x1b[H  -> home the cursor
    // \x1b[2J -> clear the visible screen
    // \x1b[3J -> clear the scrollback too (xterm-style), so nothing
    //            resurfaces when the user scrolls up afterward
    printf("\x1b[H\x1b[2J\x1b[3J");
    fflush(stdout);
    return 0;
}
