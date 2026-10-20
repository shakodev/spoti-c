#include "app_shell_support.h"
#include <math.h>

static const char notice[]="abcdefghijklmnopabcdefghijklmnop abcdefghijkl\nabcdefghijklmno";
static AppControl *control(Clay_ElementId id)
{
    for (uint32_t i=0;i<app.control_count;++i) { if (app.controls[i].id.id==id.id) { return &app.controls[i]; } }
    return NULL;
}
static Clay_BoundingBox bounds(Clay_ElementId id)
{
    Clay_ElementData data=Clay_GetElementData(id); CHECK(data.found); return data.boundingBox;
}
static void inside(Clay_ElementId id)
{
    Clay_BoundingBox box=bounds(id),panel=bounds(CLAY_ID("AppMain"));
    CHECK(box.height>0 && box.y>=panel.y+12-0.1f && box.y+box.height<=panel.y+panel.height-12+0.1f);
}
static void focused(float width,float height,float dpi)
{
    const AppKey keys[]={APP_KEY_END,APP_KEY_HOME,APP_KEY_DOWN,APP_KEY_PAGE_DOWN,APP_KEY_PAGE_UP};
    for (unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
        app.widgets.focus=CLAY_ID("AppList").id; CHECK(app_key(&app,keys[i],false)==APP_OK);
        (void)shell_layout(width,height,dpi);
        Clay_ElementId id=CLAY_IDI("LibraryItem",(uint32_t)app.widgets.lists[0].focus);
        CHECK(control(id)); Clay_BoundingBox row=bounds(id),list=bounds(CLAY_ID("AppList"));
        CHECK(row.y>=list.y-0.1f && row.y+fminf(row.height,list.height)<=list.y+list.height+0.1f);
        inside(CLAY_ID("AppMore"));
    }
}
static void matrix(bool home,unsigned state)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING);
    if (!home) { shell_action(APP_GO_LIBRARY); }
    shell_dense_page(home); const CollectionPage *owned=app.model.page;
    /* Keep the real immutable JSON/page owner alive while varying only the
     * finite projection metadata. No delivery/update/release uses this copy. */
    static CollectionPage finite; finite=*owned; finite.next=home ? 8 : 64;
    if (state==3) { finite.count=0; }
    app.model.page=&finite;
    app.model.status=state==1 || state==4 ? APP_ERROR : state==3 ? APP_EMPTY : APP_CONTENT;
    app.model.pending=state==4;
    if (state==2) { strcpy_s(app.model.location.query,sizeof(app.model.location.query),"no matching item"); app_browse_restore(&app); }
    const unsigned heights[]={560,640,720,900},widths[]={400,1100,1800},dpis[]={96,192};
    for (unsigned message=0;message<3;++message) for (unsigned grid=0;grid<(home ? 1u : 2u);++grid) {
        app.model.location.grid=grid!=0;
        CHECK(app_set_notice(&app,APP_WARNING,message==1 ? (Clay_String){false,sizeof(notice)-1,notice} : CLAY_STRING(""))==APP_OK);
        app.model.last_result=message==2 ? APP_FAILED : APP_OK;
        for (unsigned h=0;h<4;++h) for (unsigned w=0;w<3;++w) for (unsigned d=0;d<2;++d) {
            float scale=(float)dpis[d]/96,width=widths[w]*scale,height=heights[h]*scale,dpi=(float)dpis[d];
            app.widgets.lists[0].scroll=0; (void)shell_layout(width,height,dpi);
            inside(CLAY_ID("AppReadMessages")); inside(CLAY_ID("AppMore"));
            CHECK(control(CLAY_ID("AppMore"))->disabled==(state==4));
            if (state==1 || state==4) {
                inside(CLAY_ID("AppRetry")); CHECK(control(CLAY_ID("AppRetry"))->disabled==(state==4));
                CHECK(strstr(app.messages.text,"Couldn't load this page. Try again."));
            }
            if (message==1) {
                CHECK(app_messages_request(&app)==APP_OK);
                CHECK(strstr(app.messages.details,"abcdefghijklmnopabcdefghijklmnop abcdefghijkl\r\nabcdefghijklmno"));
            }
            if (!home && state!=2 && state!=3) { focused(width,height,dpi); }
            if (state==2 || state==3) { CHECK(!app.browse.count && control(CLAY_ID("AppList"))->disabled); inside(CLAY_ID("AppList")); }
        }
    }
    app.model.page=owned; app.model.pending=false; shell_close();
}
void shell_pagination(void)
{
    for (unsigned state=0;state<5;++state) { matrix(false,state); }
    matrix(true,0); matrix(true,1); matrix(true,4);
}
