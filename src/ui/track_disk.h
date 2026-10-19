#ifndef SPOTIC_TRACK_DISK_H
#define SPOTIC_TRACK_DISK_H
#include "track_filter.h"
/* Private local index v1: fixed records, no pointers or JSON. Windows LE only.
 * Writer/read handle belongs to a blocking service owner. Close only after all
 * source readers acknowledge cancellation. Fresh file per immutable revision. */
typedef struct TrackDisk {
    HANDLE file;
    TrackSource source;
    bool writing;
} TrackDisk;
TrackResult track_disk_create(TrackDisk *, const wchar_t *, const char *uri, const char *revision, bool complete);
TrackResult track_disk_append(TrackDisk *, const TrackRecord *, uint32_t);
TrackResult track_disk_finish(TrackDisk *);
TrackResult track_disk_open(TrackDisk *, const wchar_t *);
bool track_disk_close(TrackDisk *);
#endif
