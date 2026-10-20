#include "ui/font.h"
#include "ui/font_roles.h"
#include "support/test_check.h"
#include <math.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static _Thread_local bool watch_heap;
static unsigned heap_calls;
static int __cdecl allocation_hook(int type, void *data, size_t size, int block,
    long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block; (void)request; (void)file; (void)line;
    if (watch_heap) { ++heap_calls; } return 1;
}
#endif

static FontCache cache;
static UiRenderFrame frame;
static uint32_t wakes;
static void wake(void *context) { CHECK(context == &wakes); ++wakes; }
static Clay_StringSlice text(const char *s) { return (Clay_StringSlice){(int32_t)strlen(s),s,s}; }
static FontConfig config(void) { return (FontConfig){.size=16,.dpi=96,.weight=400}; }
static FontHandle prepare(const char *s, FontConfig key)
{
    FontHandle h; FontResult result = font_prepare_text(&cache,text(s),&key,&h);
    if (result != FONT_OK) { fprintf(stderr,"prepare '%s': %d\n",s,result); }
    CHECK(result == FONT_OK); return h;
}
static FontMetrics measure(const char *s, FontConfig key)
{
    FontMetrics m; CHECK(font_metrics(&cache,prepare(s,key),&m) == FONT_OK); return m;
}
static void unicode(void)
{
    uint16_t utf16[8]; uint32_t n;
    CHECK(font_utf8_to_utf16(text("\xf0\x9f\x98\x80"),utf16,8,&n) == FONT_OK && n == 2);
    CHECK(utf16[0] == 0xd83d && utf16[1] == 0xde00);
    CHECK(font_utf8_to_utf16(text("\xf0\x9f\x98"),utf16,8,&n) == FONT_INVALID && n == 0);
    CHECK(font_utf8_to_utf16(text("\xc0\x80"),utf16,8,&n) == FONT_INVALID);
    CHECK(font_utf8_to_utf16(text("\xed\xa0\x80"),utf16,8,&n) == FONT_INVALID);
    CHECK(font_utf8_to_utf16(text("\xf4\x90\x80\x80"),utf16,8,&n) == FONT_INVALID);
    CHECK(font_utf8_to_utf16(text("\xf0\x9f\x98\x80"),utf16,1,&n) == FONT_FULL);
    FontConfig k = config();
    FontMetrics empty = measure("",k); CHECK(empty.width == 0 && empty.height > 0 && !empty.glyphs);
    FontMetrics ascii = measure("Hello",k); CHECK(fabsf(ascii.width - 36.8515625f) < 1 && ascii.glyphs == 5);
    CHECK(measure("MMMM",k).width > measure("iiii",k).width * 2);
    CHECK(fabsf(measure("e\xcc\x81",k).width - measure("\xc3\xa9",k).width) < 0.1f);
    FontMetrics arabic = measure("\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9",k);
    CHECK(arabic.width > 0 && arabic.rtl_runs > 0 && arabic.visual_runs > 0);
    FontMetrics emoji = measure("\xf0\x9f\x98\x80",k); CHECK(emoji.width > 0 && emoji.utf16_units == 2);
    FontMetrics ligature = measure("office",k); CHECK(ligature.glyphs <= 6 && ligature.width > 0);
    /* Fonts may represent joined forms with multiple positioned glyphs. Test
     * contextual advances and a precomposed ligature without assuming slots. */
    CHECK(measure("\xd9\x84\xd8\xa7",k).width < measure("\xd9\x84",k).width + measure("\xd8\xa7",k).width);
    CHECK(measure("\xef\xac\x81",k).glyphs == 1); /* U+FB01 fi */
    FontMetrics cjk = measure("\xe4\xb8\xad\xe6\x96\x87",k); CHECK(cjk.utf16_units == 2 && cjk.width > 0);
    CHECK(measure("\xf4\x8f\xbf\xbf",k).missing_glyphs > 0);
    k.letter_spacing = 2; CHECK(measure("Hello",k).width > ascii.width + 8);
    k = config(); k.dpi = 192; CHECK(fabsf(measure("Hello",k).width - ascii.width) < 0.1f);
    k = config(); k.face = 1; CHECK(fabsf(measure("MMMM",k).width - measure("iiii",k).width) < 0.1f);
    k = config(); k.line_height = 30; CHECK(fabsf(measure("Hello",k).height - 30) < 0.1f);
    k = config(); k.max_width = 80; k.ellipsis = true;
    FontMetrics trimmed = measure("This is a long name that must end with a shaped ellipsis",k);
    CHECK(trimmed.width <= 80 && trimmed.trimmed);
    char long_name[1901]; memset(long_name,'W',1900); long_name[1900] = 0;
    FontMetrics long_metrics = measure(long_name,k);
    CHECK(long_metrics.trimmed && long_metrics.width <= 80 && long_metrics.glyphs < 100);
    CHECK(font_stats(&cache).com_objects == 1);
    k = config(); k.size = 96; k.dpi = 384; FontHandle full;
    CHECK(font_prepare_text(&cache,text("WW"),&k,&full) == FONT_OK); /* Tiled within existing budgets. */
    CHECK(font_prepare_text(&cache,text(long_name),&k,&full) == FONT_FULL);
    CHECK(font_stats(&cache).com_objects == 1);
    uint32_t old_wakes = wakes;
    for (unsigned i = 0; i < 1000; ++i) { CHECK(font_request_text(&cache,text(long_name),&k) == FONT_FULL); }
    CHECK(wakes == old_wakes && font_stats(&cache).pending == 0);
    printf("advances: Hello=%.3f Arabic=%.3f emoji=%.3f ellipsis=%.3f\n",
        (double)ascii.width,(double)arabic.width,(double)emoji.width,(double)trimmed.width);
}
static void misses(void)
{
    CHECK(font_cache_reset(&cache) == FONT_OK); FontConfig k = config();
    FontMeasureContext ctx = {&cache,k,FONT_OK}; Clay_TextElementConfig clay = {0}; clay.fontSize = 16;
    uint32_t before = wakes;
    for (unsigned i = 0; i < 1000; ++i) { (void)font_measure(text("cached"),&clay,&ctx); }
    CHECK(wakes == before + 1 && font_stats(&cache).pending == 1);
    CHECK(font_prepare_pending(&cache) == FONT_OK);
    CHECK(font_atlas_commit(&cache,257) == FONT_OK);
    Clay_Dimensions dims = font_measure(text("cached"),&clay,&ctx); CHECK(dims.width > 0 && ctx.last_result == FONT_OK);
    FontHandle h; CHECK(font_prepare_text(&cache,text("cached"),&k,&h) == FONT_OK);
    CHECK(font_atlas_commit(&cache,257) == FONT_OK);
    const float white[4] = {1,1,1,1}; uint64_t prepared = font_stats(&cache).prepared;
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(allocation_hook); heap_calls = 0; watch_heap = true;
#endif
    for (unsigned i = 0; i < 1000; ++i) {
        CHECK(ui_frame_begin(&frame,800,600,1) == UI_RENDER_OK);
        CHECK(font_measure(text("cached"),&clay,&ctx).width == dims.width);
        CHECK(font_render_text(&cache,h,&frame,10,10,white) == FONT_OK && frame.quad_count > 0);
    }
#if defined(_MSC_VER) && defined(_DEBUG)
    watch_heap = false; CHECK(heap_calls == 0); _CrtSetAllocHook(NULL);
#endif
    CHECK(font_stats(&cache).prepared == prepared && wakes == before + 1);
    CHECK(font_cache_reset(&cache) == FONT_OK); FontMetrics metrics;
    CHECK(font_metrics(&cache,h,&metrics) == FONT_STALE);
    k.text_generation = 42; h = prepare("new generation",k);
    k.text_generation = 43; FontHandle next = prepare("new generation",k);
    CHECK(h.generation != next.generation);
    for (unsigned i = 0; i < FONT_PENDING; ++i) {
        char s[32]; (void)snprintf(s,sizeof(s),"pending-%u",i);
        CHECK(font_request_text(&cache,text(s),&k) == FONT_PENDING_WORK);
    }
    CHECK(font_request_text(&cache,text("overflow"),&k) == FONT_FULL);
    CHECK(font_prepare_pending(&cache) == FONT_OK);
}
static void typography_roles(void)
{
    CHECK(font_cache_reset(&cache) == FONT_OK);
    FontConfig base = config(); base.weight = 900; base.rtl = true;
    base.dpi = 120; base.text_generation = 41; base.max_width = 220; base.ellipsis = true;
    FontConfig legacy = font_config_for_clay(base,1,14,2,24);
    CHECK(legacy.face == 1 && legacy.weight == 900 && legacy.rtl);
    CHECK(legacy.size == 14 && legacy.letter_spacing == 2 && legacy.line_height == 24);
    FontMeasureContext notice_context = {&cache,base,FONT_OK};
    FontConfig notice = font_clay_config(&notice_context,FONT_FACE_NOTICE,14,0,20);
    CHECK(notice.face == 0 && notice.weight == base.weight && notice.rtl);
    CHECK(notice.max_width == 0 && !notice.ellipsis && !notice.measure_only);
    CHECK(notice.size == 14 && notice.line_height == 20 && notice.dpi == base.dpi);
    const uint16_t ids[] = {FONT_ROLE_BODY,FONT_ROLE_BOLD,FONT_ROLE_TITLE,
        FONT_ROLE_HEADLINE,FONT_ROLE_BODY | FONT_ROLE_RTL};
    const uint16_t weights[] = {400,700,700,800,400};
    const char label[] = "office \xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9 123";
    FontMeasureContext ctx = {&cache,base,FONT_OK};
    Clay_TextElementConfig styles[5] = {0};
    for (unsigned i = 0; i < 5; ++i) {
        styles[i] = (Clay_TextElementConfig){.fontId=ids[i],.fontSize=16,.lineHeight=24};
        FontConfig key = font_config_for_clay(base,ids[i],16,0,24);
        CHECK(key.face == 0 && key.weight == weights[i] && key.rtl == (i == 4));
        CHECK(key.dpi == 120 && key.text_generation == 41 && key.max_width == 220 && key.ellipsis);
        FontMetrics metrics = measure(label,key);
        CHECK(metrics.width > 0 && metrics.height == 24 && metrics.rtl_runs > 0);
        key.measure_only = true;
        FontMetrics dimensions = measure(label,key);
        CHECK(dimensions.width == metrics.width && dimensions.height == metrics.height);
        CHECK(dimensions.glyphs == 0 && dimensions.visual_runs == 0);
    }
    FontConfig invalid = font_config_for_clay(base,255,16,0,0); FontHandle handle;
    CHECK(font_prepare_text(&cache,text(label),&invalid,&handle) == FONT_INVALID);
    invalid = font_config_for_clay(base,1 | FONT_ROLE_RTL,16,0,0);
    CHECK(font_prepare_text(&cache,text(label),&invalid,&handle) == FONT_INVALID);
    CHECK(font_atlas_commit(&cache,257) == FONT_OK);
    uint64_t prepared = font_stats(&cache).prepared; uint32_t before = wakes;
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(allocation_hook); heap_calls = 0; watch_heap = true;
#endif
    for (unsigned n = 0; n < 1000; ++n) {
        CHECK(ui_frame_begin(&frame,1000,750,1.25f) == UI_RENDER_OK);
        for (unsigned i = 0; i < 5; ++i) {
            Clay_Dimensions dims = font_measure(text(label),&styles[i],&ctx);
            CHECK(ctx.last_result == FONT_OK && dims.width > 0);
            Clay_RenderCommand command = {.commandType=CLAY_RENDER_COMMAND_TYPE_TEXT,
                .boundingBox={0,(float)(i * 24),dims.width,dims.height}};
            command.renderData.text = (Clay_TextRenderData){.stringContents=text(label),
                .textColor={255,255,255,255},.fontId=ids[i],.fontSize=16,.lineHeight=24};
            CHECK(font_render_clay(&ctx,&frame,&command) == UI_RENDER_OK && ctx.last_result == FONT_OK);
        }
    }
#if defined(_MSC_VER) && defined(_DEBUG)
    watch_heap = false; CHECK(heap_calls == 0); _CrtSetAllocHook(NULL);
#endif
    CHECK(font_stats(&cache).pending == 0 && font_stats(&cache).prepared == prepared && wakes == before);
}
static void eviction(void)
{
    CHECK(font_cache_reset(&cache) == FONT_OK); FontConfig k = config(); k.size = 40; k.dpi = 192;
    FontHandle first = prepare("First old line",k);
    for (unsigned i = 0; i < 24; ++i) {
        char s[64]; (void)snprintf(s,sizeof(s),"Shaped cache line %u",i); (void)prepare(s,k);
    }
    CHECK(cache.runs[first.slot].generation == first.generation); /* run slot still owned */
    FontMetrics old; CHECK(font_metrics(&cache,first,&old) == FONT_STALE); /* atlas UV eviction */
    for (unsigned i = 0; i < 100; ++i) {
        char s[64]; (void)snprintf(s,sizeof(s),"Shaped cache line %u",i); (void)prepare(s,k);
    }
    CHECK(font_stats(&cache).evictions > 0 && font_stats(&cache).atlas_entries <= FONT_ATLAS_ENTRIES);
    FontMetrics metrics; CHECK(font_metrics(&cache,first,&metrics) == FONT_STALE);
    CHECK(font_stats(&cache).com_objects == 1);
}
static void clay_binding(void)
{
    static unsigned char arena[1024 * 1024]; static FontMeasureContext ctx;
    CHECK(font_cache_reset(&cache) == FONT_OK);
    Clay_SetMaxElementCount(256); Clay_SetMaxMeasureTextCacheWordCount(512);
    CHECK(Clay_MinMemorySize() <= sizeof(arena));
    CHECK(Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(sizeof(arena),arena),
        (Clay_Dimensions){800,600},(Clay_ErrorHandler){0}) != NULL);
    ctx = (FontMeasureContext){&cache,config(),FONT_OK}; font_bind_clay(&ctx);
    Clay_BeginLayout();
    CLAY_TEXT(CLAY_STRING("cached"),CLAY_TEXT_CONFIG({.fontSize=16,.textColor={255,255,255,255},.wrapMode=CLAY_TEXT_WRAP_NONE}));
    (void)Clay_EndLayout(0);
    CHECK(font_stats(&cache).pending > 0);
    CHECK(font_prepare_pending(&cache) == FONT_OK); CHECK(font_atlas_commit(&cache,257) == FONT_OK);
    FontConfig drawing=config(); FontHandle glyphs;
    CHECK(font_prepare_text(&cache,text("cached"),&drawing,&glyphs)==FONT_OK);
    CHECK(font_atlas_commit(&cache,257)==FONT_OK);
    UiClayBridge bridge = {.custom=font_render_clay,.context=&ctx};
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(allocation_hook); heap_calls = 0; watch_heap = true;
#endif
    for (unsigned i = 0; i < 1000; ++i) {
        Clay_BeginLayout();
        CLAY_TEXT(CLAY_STRING("cached"),CLAY_TEXT_CONFIG({.fontSize=16,.textColor={255,255,255,255},.wrapMode=CLAY_TEXT_WRAP_NONE}));
        Clay_RenderCommandArray commands = Clay_EndLayout(0); CHECK(commands.length > 0);
        CHECK(commands.internalArray[0].boundingBox.width > 0);
        CHECK(ui_frame_begin(&frame,800,600,1) == UI_RENDER_OK);
        CHECK(ui_frame_clay(&frame,commands,&bridge) == UI_RENDER_OK);
        CHECK(frame.quad_count > 0 && font_stats(&cache).pending == 0);
    }
#if defined(_MSC_VER) && defined(_DEBUG)
    watch_heap = false; CHECK(heap_calls == 0); _CrtSetAllocHook(NULL);
#endif
    puts("1000 warm Clay layout/render frames: 0 CRT allocations, 0 new preparation requests");
}
int main(void)
{
    CHECK(font_cache_create(&cache,wake,&wakes) == FONT_OK);
    unicode(); misses(); typography_roles(); eviction(); clay_binding();
    font_cache_destroy(&cache); CHECK(font_stats(&cache).com_objects == 0);
    puts("ui_font: Unicode, DirectWrite metrics, pending coalescing, warm cache, LRU and lifetime passed");
    return 0;
}
