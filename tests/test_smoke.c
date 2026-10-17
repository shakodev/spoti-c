#include <stdio.h>

_Static_assert(__STDC_VERSION__ >= 201710L, "The project requires C17 or later.");

int main(void)
{
    return puts("spoti-c C17 smoke test passed") == EOF ? 1 : 0;
}
