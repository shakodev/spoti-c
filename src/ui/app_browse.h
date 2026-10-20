#ifndef SPOTIC_UI_APP_BROWSE_H
#define SPOTIC_UI_APP_BROWSE_H
#include "api/collection_api.h"
#include "components.h"

#define APP_BROWSE_VISIBLE 14u /* Reserves renderer slots for font atlas and persistent player cover. */
typedef struct AppVisibleImage { uint32_t shelf, index, size; } AppVisibleImage;
typedef struct AppBrowse {
    uint16_t order[COLLECTION_ROW_CAP];
    AppVisibleImage visible[APP_BROWSE_VISIBLE], previous[APP_BROWSE_VISIBLE];
    uint32_t count, visible_count, previous_count;
    uint64_t generation;
    uint16_t surrogate;
    bool collapsed;
} AppBrowse;
struct App;
void app_browse_project(struct App *);
uint32_t app_browse_columns(const struct App *);
void app_browse_cover(struct App *, const CollectionItem *, uint32_t shelf, uint32_t index, float edge);
void app_browse_card(struct App *, Clay_ElementId, const CollectionItem *, uint32_t shelf,
    uint32_t index, float width, float edge, bool list);
void app_browse_query(struct App *);
void app_browse_begin(struct App *);
void app_browse_end(struct App *);
void app_browse_prepare(struct App *);
void app_browse_restore(struct App *);
bool app_shell_button(struct App *, Clay_ElementId, Clay_String, UiIcon, UiComponentKind,
    float width, float height, int action, bool selected, bool disabled);
void app_header(struct App *);
#endif
