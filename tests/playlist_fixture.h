#ifndef PLAYLIST_FIXTURE_H
#define PLAYLIST_FIXTURE_H
#include "ui/track_disk.h"
#include "support/test_check.h"
void playlist_fixture_record(uint32_t ordinal, TrackRecord *);
void playlist_fixture_disk(TrackDisk *, wchar_t path[MAX_PATH], uint32_t count);
struct App;
int playlist_visual(struct App *);
#endif
