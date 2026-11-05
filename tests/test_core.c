#include "okupa.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t secret[32];
    size_t i;
    memset(secret, 0xa5, sizeof secret);
    ok_wipe(secret, sizeof secret);
    for (i = 0; i < sizeof secret; i++) assert(secret[i] == 0);
    assert(!strcmp(ok_result_name(OK_OK), "ok"));
    assert(!strcmp(ok_result_name(OK_FORMAT), "format"));
    assert(!strcmp(ok_result_name((ok_result)99), "unknown"));
    puts("core: result names and wipe");
    return 0;
}
