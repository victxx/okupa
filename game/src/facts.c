/* The record behind the technical screens; see okupa_facts.h. */
#include "okupa_facts.h"
#include <stdarg.h>
#include <stdio.h>

ok_facts ok_facts_now;

void ok_facts_log(uint32_t now, char src, const char *fmt, ...)
{
    ok_facts *f = &ok_facts_now;
    unsigned i = f->log_count % OK_FACTS_LOG;
    va_list ap;
    f->log[i].t = now;
    f->log[i].src = src;
    va_start(ap, fmt);
    vsnprintf(f->log[i].text, sizeof f->log[i].text, fmt, ap);
    va_end(ap);
    f->log_count++;
}
