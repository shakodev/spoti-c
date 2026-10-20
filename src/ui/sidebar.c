#include "app.h"
#include "font_roles.h"
#include <math.h>
#include <string.h>

static unsigned char fold(unsigned char c) { return c>='A' && c<='Z' ? (unsigned char)(c+32) : c; }
static bool matches(JsonString text,const UiTextInput *query)
{
    if (!query->length) { return true; }
    if (!text.data || query->length>text.bytes) { return false; }
    for (size_t i=0;i<=text.bytes-query->length;++i) {
        uint32_t n=0;
        while (n<query->length && fold((unsigned char)text.data[i+n])==fold((unsigned char)query->text[n])) { ++n; }
        if (n==query->length) { return true; }
    }
    return false;
}
static uint64_t key_at(void *context,uint32_t row)
{
    App *a=context; return (uint64_t)a->sidebar.order[row]+1;
}
static bool rtl(const char *text)
{
    for (const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if ((*p>='A' && *p<='Z') || (*p>='a' && *p<='z')) { return false; }
        if (*p>=0xd6 && *p<=0xdb) { return true; }
    }
    return false;
}
static void cover(App *a,uint32_t index)
{
    AppSidebar *s=&a->sidebar; AppSidebarCover *image=NULL;
    if (s->visible_count<10) { s->visible[s->visible_count++]=index; }
    for (uint32_t i=0;i<s->cover_count;++i) { if (s->covers[i].index==index) { image=&s->covers[i]; break; } }
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(48),CLAY_SIZING_FIXED(48)},
        .childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},.backgroundColor=image ? (Clay_Color){0} : (Clay_Color){40,40,40,255},
        .cornerRadius=CLAY_CORNER_RADIUS(4),.custom={.customData=image}}) {
        if (!image || image->result!=ART_OK) {
            bool failed=image && image->result!=ART_PENDING && image->result!=ART_FULL;
            ui_icon(&a->widgets,CLAY_IDI("SidebarCover",index),failed ? UI_ICON_WARNING : UI_ICON_LIBRARY,24,
                failed ? (Clay_Color){255,184,77,255} : (Clay_Color){179,179,179,255},false);
        }
    }
}
static void row(void *context,uint32_t row_index,uint64_t key)
{
    App *a=context; uint32_t index=a->sidebar.order[row_index];
    CollectionItem value=app_rootlist_item(a->sidebar.page,index);
    const CollectionItem *item=&value; AppEntity entity={0};
    bool valid=item->available && app_entity(item,&entity)==APP_OK;
    Clay_ElementId id=CLAY_IDI("SidebarItem",(uint32_t)key);
    if (a->control_count==APP_CONTROLS || !ui_widgets_declare(&a->widgets,id)) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return; }
    AppAction action={.type=APP_SIDEBAR_SELECT,.entity=entity,.value=index};
    a->controls[a->control_count++]=(AppControl){id,action,!valid};
    if (ui_widgets_button_event(&a->widgets,id,!valid)) { (void)app_enqueue(a,action); }
    bool focus=a->widgets.focus==id.id || (a->widgets.focus==CLAY_ID("SidebarList").id && a->widgets.lists[1].focus==key);
    UiComponentStyle style=ui_component_style(UI_COMPONENT_ROW,(!valid ? UI_CONTROL_DISABLED : 0) |
        (a->widgets.hover==id.id ? UI_CONTROL_HOVER : 0) | (focus ? UI_CONTROL_FOCUS : 0) |
        (!strcmp(entity.uri,a->model.selected.uri) && entity.uri[0] ? UI_CONTROL_SELECTED : 0));
    bool collapsed=a->geometry.sidebar<=64, direction=rtl(entity.title);
    CLAY(id,{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(64)},.padding={0,0,8,8},.childGap=12,
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}},.backgroundColor=style.background,.cornerRadius=CLAY_CORNER_RADIUS(4),
        .border={.color=style.outline,.width=CLAY_BORDER_ALL(style.border)},.clip={.horizontal=true,.vertical=true}}) {
        cover(a,index);
        if (!collapsed) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIT()},.childGap=4,.layoutDirection=CLAY_TOP_TO_BOTTOM},
                .clip={.horizontal=true,.vertical=true}}) {
                ui_component_label(app_text(a,(Clay_String){false,(int32_t)strlen(entity.title),entity.title}),FONT_ROLE_BODY,14,style.foreground,direction);
                ui_component_label(CLAY_STRING("Playlist"),FONT_ROLE_BODY,12,(Clay_Color){179,179,179,255},direction);
            }
        }
    }
}

void app_sidebar(App *a)
{
    float width=a->geometry.sidebar; bool collapsed=width<=64; AppSidebar *s=&a->sidebar;
    s->count=0;
    if (s->page && a->model.logged_in) {
        for (uint32_t i=0;i<s->page->count;++i) {
            CollectionItem value=app_rootlist_item(s->page,i); const CollectionItem *item=&value;
            if ((item->kind==COLLECTION_PLAYLIST_ITEM || item->kind==COLLECTION_LIKED) && matches(item->name,&a->widgets.texts[2])) {
                s->order[s->count++]=(uint16_t)i;
            }
        }
    }
    CLAY(CLAY_ID("AppSidebar"),{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_GROW()},
        .padding=CLAY_PADDING_ALL(8),.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
        .backgroundColor={18,18,18,255},.cornerRadius=CLAY_CORNER_RADIUS(8),.clip={.horizontal=true,.vertical=true}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(48)},.childGap=4}}) {
            (void)app_shell_button(a,CLAY_ID("NavLibrary"),collapsed ? CLAY_STRING("") : CLAY_STRING("Your Library"),
                UI_ICON_LIBRARY,UI_COMPONENT_TERTIARY,collapsed ? 48 : width-68,48,APP_GO_LIBRARY,a->model.location.screen==APP_LIBRARY,false);
            if (!collapsed) {
                (void)app_shell_button(a,CLAY_ID("SidebarCollapse"),CLAY_STRING(""),UI_ICON_CHEVRON_LEFT,
                    UI_COMPONENT_ICON,32,48,APP_SIDEBAR_TOGGLE,false,false);
            }
        }
        if (collapsed && !a->geometry.compact) {
            (void)app_shell_button(a,CLAY_ID("SidebarCollapse"),CLAY_STRING(""),UI_ICON_CHEVRON_RIGHT,
                UI_COMPONENT_TERTIARY,48,28,APP_SIDEBAR_TOGGLE,false,false);
        }
        if (!collapsed) {
            app_label(CLAY_STRING("Playlists | loaded page"),12,(Clay_Color){179,179,179,255});
            ui_text_input(&a->widgets,CLAY_ID("SidebarQuery"),&a->widgets.texts[2],width-16);
            if (a->control_count<APP_CONTROLS) {
                a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("SidebarQuery"),.action={.type=APP_SIDEBAR_FOCUS}};
            }
        }
        if (s->count) {
            UiListRange range;
            (void)ui_virtual_list(&a->widgets,CLAY_ID("SidebarList"),&a->widgets.lists[1],s->count,64,
                fminf(640,fmaxf(64,a->geometry.height-(collapsed ? 264 : 332))),0,key_at,row,a,&range);
            if (a->control_count<APP_CONTROLS) {
                a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("SidebarList"),.action={.type=APP_SIDEBAR_SELECT,.value=-1}};
            }
        } else if (!collapsed) {
            app_label(!a->model.logged_in ? CLAY_STRING("Sign in to see playlists") : s->pending ? CLAY_STRING("Loading playlists...") :
                s->failed ? CLAY_STRING("Couldn't load playlists") : s->page ? CLAY_STRING("No matching playlists") : CLAY_STRING("Open Library to browse"),
                12,(Clay_Color){179,179,179,255});
        }
        if (!collapsed && s->failed) {
            (void)app_shell_button(a,CLAY_ID("SidebarRetry"),CLAY_STRING("Try again"),UI_ICON_NONE,
                UI_COMPONENT_SECONDARY,width-16,28,APP_SIDEBAR_RETRY,false,s->pending);
        }
        if (!collapsed && s->page && s->page->next!=COLLECTION_END) {
            (void)app_shell_button(a,CLAY_ID("SidebarMore"),s->pending ? CLAY_STRING("Loading...") : CLAY_STRING("Next loaded page"),UI_ICON_NONE,
                UI_COMPONENT_SECONDARY,width-16,28,APP_SIDEBAR_MORE,false,s->pending);
        }
    }
}
