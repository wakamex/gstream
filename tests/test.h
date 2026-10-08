// streamit's tests: one executable (zig build test).
#pragma once
#include <stdio.h>

extern int test_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond), test_failures++; \
    } while (0)

void test_twitch(void);
