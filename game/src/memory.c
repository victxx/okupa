#include "okupa_memory.h"
#include <string.h>

void ok_memory_init(ok_memory *m) { memset(m, 0, sizeof *m); }

/* The letters and digits of a fact, lower case: what makes two the same. */
static void key(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[o++] = c;
    }
    out[o] = 0;
}

int ok_memory_add(ok_memory *m, const char *fact)
{
    char k[OK_MIND_NOTE + 1], other[OK_MIND_NOTE + 1];
    size_t i, o = 0;
    int j;
    if (!m || !fact) return 0;
    key(fact, k, sizeof k);
    if (!k[0]) return 0;
    for (j = 0; j < m->count; j++) {
        key(m->fact[j], other, sizeof other);
        if (!strcmp(k, other)) return 0;
    }
    if (m->count == OK_MIND_KNOWN) { /* full: the oldest goes */
        memmove(m->fact[0], m->fact[1], sizeof m->fact[0] * (OK_MIND_KNOWN - 1));
        m->count--;
    }
    for (i = 0; fact[i] && o < OK_MIND_NOTE; i++)
        if ((unsigned char)fact[i] >= 0x20 && (unsigned char)fact[i] < 0x7f) m->fact[m->count][o++] = fact[i];
    m->fact[m->count][o] = 0;
    m->count++;
    return 1;
}

void ok_memory_load(ok_memory *m, const char *text, size_t n)
{
    char line[OK_MIND_NOTE + 1];
    size_t i, o = 0;
    ok_memory_init(m);
    for (i = 0; i <= n; i++) {
        if (i == n || text[i] == '\n' || text[i] == '\r') {
            line[o] = 0;
            ok_memory_add(m, line);
            o = 0;
        } else if (o < OK_MIND_NOTE) {
            line[o++] = text[i];
        }
    }
}

ok_result ok_memory_save(const ok_memory *m, char *out, size_t cap, size_t *len)
{
    size_t o = 0, k;
    int j;
    for (j = 0; j < m->count; j++) {
        k = strlen(m->fact[j]);
        if (o + k + 2 > cap) return OK_RANGE;
        memcpy(out + o, m->fact[j], k);
        o += k;
        out[o++] = '\n';
    }
    if (o >= cap) return OK_RANGE;
    out[o] = 0;
    *len = o;
    return OK_OK;
}

void ok_memory_tell(const ok_memory *m, ok_mind_obs *obs)
{
    int j;
    for (j = 0; j < m->count; j++) obs->known[j] = m->fact[j];
    obs->known_count = m->count;
}
