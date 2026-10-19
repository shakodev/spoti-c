#include "ui/app.h"
void app_artist(App *a)
{
    app_catalog_header(a);
    CLAY_AUTO_ID({.layout={.childGap=4}}) {
        (void)app_button(a,CLAY_ID("ArtistTracks"),CLAY_STRING("Top tracks"),88,
            (AppAction){.type=APP_CATALOG_SECTION,.value=ARTIST_TOP_TRACKS},a->model.location.selector==ARTIST_TOP_TRACKS);
        (void)app_button(a,CLAY_ID("ArtistAlbums"),CLAY_STRING("Albums"),64,
            (AppAction){.type=APP_CATALOG_SECTION,.value=ARTIST_ALBUMS},a->model.location.selector==ARTIST_ALBUMS);
        (void)app_button(a,CLAY_ID("ArtistSingles"),CLAY_STRING("Singles"),64,
            (AppAction){.type=APP_CATALOG_SECTION,.value=ARTIST_SINGLES},a->model.location.selector==ARTIST_SINGLES);
    }
    app_catalog_status(a);
    if (a->model.logged_in) { app_catalog_rows(a); }
}
