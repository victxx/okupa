#ifndef OKUPA_CHECKS_H
#define OKUPA_CHECKS_H

#include <stdint.h>

/* Known-answer checks of the core, timed, for the Dev screen: proof that the
 * console computes exactly what the host tests expect, and how long it takes.
 * now_us is the platform's microsecond clock. Returns the number of failures. */
int ok_checks_run(char lines[][33], int max, uint32_t (*now_us)(void));

#endif
