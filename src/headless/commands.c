#include "headless.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

bool headless_parse(const char *line, uint32_t rate, PlayerCommand *out)
{
    if (!line || !out) { return false; }
    PlayerCommand c = {0};
    if (!strcmp(line, "play")) { c.type = PLAYER_PLAY; }
    else if (!strcmp(line, "pause")) { c.type = PLAYER_PAUSE; }
    else if (!strcmp(line, "next")) { c.type = PLAYER_NEXT; }
    else if (!strcmp(line, "stop")) { c.type = PLAYER_STOP; }
    else if (!strncmp(line, "seek ", 5)) {
        const char *p = line + 5; uint64_t ms = 0;
        if (!*p || !rate) { return false; }
        for (; *p; ++p) {
            if (*p < '0' || *p > '9' || ms > (UINT64_MAX - (unsigned)(*p - '0')) / 10) { return false; }
            ms = ms * 10 + (unsigned)(*p - '0');
        }
        if (ms > UINT64_MAX / rate) { return false; }
        c.type = PLAYER_SEEK; c.position_frames = ms * rate / 1000;
    } else if (!strncmp(line, "volume ", 7)) {
        const char *p = line + 7; char *end;
        if (!*p || *p == ' ' || *p == '\t') { return false; }
        errno = 0; c.volume = strtof(p, &end);
        if (errno || *end || !isfinite(c.volume) || c.volume < 0 || c.volume > 1) { return false; }
        c.type = PLAYER_VOLUME;
    } else { return false; }
    *out = c; return true;
}
bool headless_command(Headless *h, const char *line)
{
    PlayerCommand c; return h && headless_parse(line, headless_snapshot(h).rate, &c) && headless_submit(h, &c) == PLAYER_OK;
}
