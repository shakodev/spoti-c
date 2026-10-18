#include "headless/headless.h"
#include "protocol/media_source.h"
#include "support/test_check.h"
int main(void)
{
    wchar_t credentials[240], cache[240]; char uri[MEDIA_URI_BYTES], country[3], catalogue[33];
    DWORD a = GetEnvironmentVariableW(L"SPOTIC_HEADLESS_CREDENTIALS",credentials,240);
    DWORD b = GetEnvironmentVariableW(L"SPOTIC_HEADLESS_CACHE",cache,240);
    DWORD c = GetEnvironmentVariableA("SPOTIC_MEDIA_TRACK",uri,sizeof(uri));
    DWORD d = GetEnvironmentVariableA("SPOTIC_MEDIA_COUNTRY",country,sizeof(country));
    DWORD e = GetEnvironmentVariableA("SPOTIC_MEDIA_CATALOGUE",catalogue,sizeof(catalogue));
    if (!a || a >= 240 || !b || b >= 240 || !c || c >= sizeof(uri) || !d || d >= sizeof(country) || !e || e >= sizeof(catalogue)) {
        puts("SKIP: supply private DPAPI SPOTIC_HEADLESS_CREDENTIALS, SPOTIC_HEADLESS_CACHE and SPOTIC_MEDIA_TRACK/COUNTRY/CATALOGUE."); return 77;
    }
    HeadlessConfig config = {.credential_path = credentials, .cache_directory = cache, .country = country,
        .catalogue = catalogue, .wasapi = true, .realtime = true};
    PlayerCommand load = {.type = PLAYER_LOAD, .play = true};
    CHECK(media_gid_from_uri(uri,load.track_id) == MEDIA_OK);
    Headless *h; CHECK(headless_open(&config,&h)); CHECK(headless_submit(h,&load) == PLAYER_OK);
    uint64_t until = GetTickCount64() + 60000; bool played = false;
    while (GetTickCount64() < until) {
        PlayerSnapshot s = headless_snapshot(h);
        if (s.state == PLAYER_ERROR) { break; }
        if (s.state == PLAYER_PLAYING && s.position_frames >= s.rate * 5) { played = true; break; }
        Sleep(50);
    }
    HeadlessMetrics m; bool closed = headless_close(h,&m);
    CHECK(closed && m.balanced && played && m.frames >= 48000);
    puts("Account-backed encrypted playback reached WASAPI for at least five seconds; listener confirmation remains manual."); return 0;
}
