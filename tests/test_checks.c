#include "okupa_checks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint32_t now_us(void)
{
    return (uint32_t)((uint64_t)clock() * 1000000u / CLOCKS_PER_SEC);
}

int main(void)
{
    char lines[14][33];
    int i;
    memset(lines, 0, sizeof lines);
    assert(ok_checks_run(lines, 14, now_us) == 0);
    for (i = 0; i < 8; i++) printf("  %s\n", lines[i]);
    assert(!strcmp(lines[7], "all checks passed"));
    assert(ok_checks_run(lines, 3, now_us) == 0 && lines[2][0]); /* fewer lines: no overflow */
    puts("checks: the Dev screen's known answers pass on the host");
    return 0;
}
