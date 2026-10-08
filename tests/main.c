#include "test.h"

int test_failures;

int main(void) {
    static const struct { const char *name; void (*run)(void); } tests[] = {
        { "twitch", test_twitch },
    };
    for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
        int before = test_failures;
        tests[i].run();
        printf("%-12s %s\n", tests[i].name, test_failures == before ? "ok" : "FAILED");
    }
    return test_failures != 0;
}
