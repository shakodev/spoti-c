#include "ui/app.h"
void app_album(App *a)
{
    app_catalog_header(a);
    app_label(CLAY_STRING("Album tracks"),14,(Clay_Color){180,180,180,255});
    app_catalog_status(a);
    if (a->model.logged_in) { app_catalog_rows(a); }
}
