/* Unmodified pinned stb implementation. Only this translation unit compiles it.
 * Arena allocation is mandatory; disable its unreachable heap fallback too. */
#include <stdlib.h>
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PULLDATA_API
#define STB_VORBIS_MAX_CHANNELS 2
#define STB_VORBIS_FAST_HUFFMAN_LENGTH 8
#define malloc(bytes) ((void)(bytes), (void *)0)
#define free(pointer) ((void)(pointer))
#ifdef _MSC_VER
/* Pinned upstream narrows validated bit fields and shadows local loop names.
 * Keep the application at /W4 /WX and contain these four vendor diagnostics. */
#pragma warning(push)
#pragma warning(disable: 4244 4245 4456 4457)
#endif
#include "stb_vorbis.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
