#define CLAY_IMPLEMENTATION
/* Keep the pinned upstream bytes unchanged. These diagnostics are confined to
 * its implementation include; project code retains /W4 /WX. */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100 4189 4244 4305 4389)
#endif
#include "clay.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
