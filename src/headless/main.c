#include "fixture_server.h"
#include "headless.h"
#include "protocol/media_source.h"
#include <string.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    HeadlessConfig config = {.realtime = true};
    const wchar_t *fixture = NULL, *uri_arg = NULL;
    char country[3] = {0}, catalogue[33] = {0}, uri[MEDIA_URI_BYTES] = {0};
    bool automatic = false;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--wasapi")) { config.wasapi = true; continue; }
        if (!wcscmp(argv[i], L"--once")) { automatic = true; continue; }
        if (i + 1 == argc) { goto usage; }
        if (!wcscmp(argv[i], L"--fixture")) { fixture = argv[++i]; }
        else if (!wcscmp(argv[i], L"--cache")) { config.cache_directory = argv[++i]; }
        else if (!wcscmp(argv[i], L"--live")) { uri_arg = argv[++i]; }
        else if (!wcscmp(argv[i], L"--credentials")) { config.credential_path = argv[++i]; }
        else if (!wcscmp(argv[i], L"--country")) {
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[++i], -1, country, sizeof(country), NULL, NULL)) { goto usage; }
            config.country = country;
        } else if (!wcscmp(argv[i], L"--catalogue")) {
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[++i], -1, catalogue, sizeof(catalogue), NULL, NULL)) { goto usage; }
            config.catalogue = catalogue;
        } else { goto usage; }
    }
    if (!config.cache_directory || (fixture != NULL) == (uri_arg != NULL)) { goto usage; }
    HeadlessServer server = {0}; char url[128]; PlayerCommand load = {.type = PLAYER_LOAD, .play = true};
    if (fixture) {
        if (!headless_server_open(&server, fixture)) { fputs("Cannot open fixture HTTP server.\n", stderr); return 1; }
        sprintf_s(url, sizeof(url), "http://127.0.0.1:%u/audio", (unsigned)server.port); config.fixture_url = url;
        load.track_id[15] = 1;
    } else {
        if (!config.credential_path || !config.country || !config.catalogue ||
            !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, uri_arg, -1, uri, sizeof(uri), NULL, NULL) ||
            media_gid_from_uri(uri, load.track_id) != MEDIA_OK) { goto usage; }
        config.wasapi = true; /* Live acceptance must be audible. */
    }
    Headless *h;
    if (!headless_open(&config, &h)) {
        if (fixture) { headless_server_close(&server); } fputs("Headless startup failed.\n", stderr); return 1;
    }
    bool ok = headless_submit(h, &load) == PLAYER_OK;
    if (automatic) {
        uint64_t until = GetTickCount64() + 15 * 60 * 1000;
        while (ok) {
            PlayerSnapshot snapshot = headless_snapshot(h);
            if (snapshot.state == PLAYER_ENDED) { break; }
            if (snapshot.state == PLAYER_ERROR || GetTickCount64() >= until) { ok = false; break; }
            Sleep(50);
        }
    } else {
        fputs("Commands: play, pause, seek <ms>, volume <0..1>, next, stop, metrics, quit\n", stderr);
        char line[256];
        while (ok && fgets(line, sizeof(line), stdin)) {
            size_t n = strcspn(line, "\r\n");
            if (n == sizeof(line) - 1) {
                int ch; while ((ch = getchar()) != '\n' && ch != EOF) { } fputs("Command too long.\n", stderr); continue;
            }
            line[n] = 0;
            if (!strcmp(line, "quit")) { break; }
            if (!strcmp(line, "metrics")) { HeadlessMetrics m; ok = headless_metrics(h, &m) && headless_write_metrics(stdout, &m); }
            else if (!headless_command(h, line)) { fputs("Invalid or unavailable command.\n", stderr); }
            else if (!strcmp(line,"stop")) { break; } /* Terminal CLI stop joins and wipes the session. */
        }
    }
    HeadlessMetrics metrics; ok &= headless_close(h, &metrics); headless_write_metrics(stdout, &metrics);
    if (fixture) { ok &= headless_server_close(&server); }
    return ok ? 0 : 1;
usage:
    fputs("spotic-headless --fixture synthetic.enc --cache <private-directory> [--wasapi] [--once]\n"
        "spotic-headless --live spotify:track:... --credentials <DPAPI-file> --country NL --catalogue premium --cache <directory> [--once]\n", stderr);
    return 2;
}
