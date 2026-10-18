#include "headless_support.h"
#include <wchar.h>
void hf_open(HeadlessFixture *f)
{
    *f = (HeadlessFixture){0};
    CHECK(headless_server_open(&f->server, L"" HEADLESS_FIXTURE_DIR L"/synthetic.enc"));
    CHECK(swprintf_s(f->cache, 240, L"%ls/headless-%lu-%llu", L"" HEADLESS_OUTPUT_DIR,
        GetCurrentProcessId(), (unsigned long long)GetTickCount64()) > 0);
    CHECK(CreateDirectoryW(f->cache, NULL));
    CHECK(sprintf_s(f->url, sizeof(f->url), "http://127.0.0.1:%u/audio", (unsigned)f->server.port) > 0);
}
void hf_close(HeadlessFixture *f)
{
    CHECK(headless_server_close(&f->server));
    wchar_t file[260]; CHECK(swprintf_s(file, 260, L"%ls/encrypted-v1.bin", f->cache) > 0);
    /* Exact owned fixture file/directory only, no recursive removal. */
    if (!DeleteFileW(file)) { CHECK(GetLastError() == ERROR_FILE_NOT_FOUND); }
    CHECK(RemoveDirectoryW(f->cache));
}
void hf_load(Headless *h, bool play)
{
    PlayerCommand c = {.type = PLAYER_LOAD, .play = play}; c.track_id[15] = 1;
    CHECK(headless_submit(h, &c) == PLAYER_OK);
}
void hf_wait(Headless *h, PlayerState state)
{
    uint64_t until = GetTickCount64() + 20000;
    for (;;) {
        PlayerSnapshot s = headless_snapshot(h);
        if (s.state == state) { return; }
        if (GetTickCount64() >= until || s.state == PLAYER_ERROR) {
            fprintf(stderr, "headless state=%u error=%u expected=%u\n", (unsigned)s.state,(unsigned)s.error,(unsigned)state); CHECK(false);
        }
        Sleep(1);
    }
}
