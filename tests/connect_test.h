#ifndef SPOTIC_CONNECT_TEST_H
#define SPOTIC_CONNECT_TEST_H
#include "protocol/dealer.h"
#include "player_test.h"

static size_t connect_fixture(const char *name, void *out, size_t cap)
{
    char path[1024]; CHECK(sprintf_s(path, sizeof(path), "%s/%s", CONNECT_FIXTURE_DIR, name) > 0);
    FILE *file = NULL; CHECK(!fopen_s(&file, path, "rb") && file);
    size_t n = fread(out, 1, cap, file); CHECK(fgetc(file) == EOF && !fclose(file)); return n;
}
#endif
