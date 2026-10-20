#include "live_internal.h"
#include <string.h>

/* An application-owned Home shelf built from real rootlist data. It is not a
 * claim that the native rootlist is a Spotify personalized Home response. */
bool live_saved_home_body(AppLive *h, CollectionRequest *r, bool section, size_t *bytes)
{
    yyjson_alc allocator; yyjson_alc_pool_init(&allocator,h->json_work,sizeof(h->json_work));
    yyjson_mut_doc *doc=yyjson_mut_doc_new(&allocator); if (!doc) { return false; }
    yyjson_mut_val *root=yyjson_mut_obj(doc), *data=yyjson_mut_obj(doc), *home=yyjson_mut_obj(doc),
        *shelf=yyjson_mut_obj(doc), *items=yyjson_mut_arr(doc), *section_items=yyjson_mut_obj(doc), *sections=yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc,root);
    bool ok=root && data && home && shelf && items && section_items && sections &&
        yyjson_mut_obj_add_val(doc,root,"data",data) && yyjson_mut_obj_add_str(doc,shelf,"__typename","HomeSection") &&
        yyjson_mut_obj_add_str(doc,shelf,"uri","spoti-c:playlists") &&
        yyjson_mut_obj_add_val(doc,shelf,"sectionItems",section_items) && yyjson_mut_obj_add_val(doc,section_items,"items",items) &&
        yyjson_mut_obj_add_uint(doc,section_items,"totalCount",h->list.total) && yyjson_mut_arr_append(sections,shelf);
    for (uint32_t i=0;ok && i<h->list.count;++i) {
        LiveRow *row=&h->list.rows[i];
        if (strncmp(row->uri,"spotify:playlist:",17)) { continue; }
        yyjson_mut_val *item=yyjson_mut_obj(doc), *content=yyjson_mut_obj(doc), *playlist=yyjson_mut_obj(doc);
        ok=item && content && playlist && yyjson_mut_obj_add_str(doc,item,"uri",row->uri) &&
            yyjson_mut_obj_add_val(doc,item,"content",content) && yyjson_mut_obj_add_val(doc,content,"data",playlist) &&
            yyjson_mut_obj_add_str(doc,playlist,"__typename","Playlist") && yyjson_mut_obj_add_str(doc,playlist,"uri",row->uri) &&
            yyjson_mut_obj_add_str(doc,playlist,"name",row->name[0] ? row->name : row->uri) && yyjson_mut_arr_append(items,item);
        if (ok && row->image_uri[0]) {
            yyjson_mut_val *images=yyjson_mut_obj(doc), *variants=yyjson_mut_arr(doc), *variant=yyjson_mut_obj(doc),
                *sources=yyjson_mut_arr(doc), *source=yyjson_mut_obj(doc);
            ok=images && variants && variant && sources && source &&
                yyjson_mut_obj_add_str(doc,source,"url",row->image_uri) && yyjson_mut_arr_append(sources,source) &&
                yyjson_mut_obj_add_val(doc,variant,"sources",sources) && yyjson_mut_arr_append(variants,variant) &&
                yyjson_mut_obj_add_val(doc,images,"items",variants) && yyjson_mut_obj_add_val(doc,playlist,"images",images);
        }
    }
    if (section) {
        yyjson_mut_val *paging=yyjson_mut_obj(doc);
        ok=ok && paging && yyjson_mut_obj_add_val(doc,data,"homeSections",home) &&
            yyjson_mut_obj_add_str(doc,home,"__typename","HomeSectionCollection") && yyjson_mut_obj_add_val(doc,home,"sections",sections) &&
            yyjson_mut_obj_add_val(doc,section_items,"pagingInfo",paging);
        uint32_t next=r->cursor+h->list.count;
        ok=ok && (next<h->list.total && h->list.count ? yyjson_mut_obj_add_uint(doc,paging,"nextOffset",next) :
            yyjson_mut_obj_add_null(doc,paging,"nextOffset"));
    } else {
        yyjson_mut_val *greeting=yyjson_mut_obj(doc), *container=yyjson_mut_obj(doc), *group=yyjson_mut_obj(doc),
            *section_data=yyjson_mut_obj(doc), *title=yyjson_mut_obj(doc);
        ok=ok && greeting && container && group && section_data && title &&
            yyjson_mut_obj_add_val(doc,data,"home",home) && yyjson_mut_obj_add_str(doc,home,"__typename","HomeResponsePayload") &&
            yyjson_mut_obj_add_val(doc,home,"greeting",greeting) && yyjson_mut_obj_add_str(doc,greeting,"transformedLabel","Your music") &&
            yyjson_mut_obj_add_val(doc,home,"sectionContainer",container) && yyjson_mut_obj_add_val(doc,container,"sections",group) &&
            yyjson_mut_obj_add_uint(doc,group,"totalCount",1) && yyjson_mut_obj_add_val(doc,group,"items",sections) &&
            yyjson_mut_obj_add_val(doc,shelf,"data",section_data) && yyjson_mut_obj_add_str(doc,section_data,"__typename","HomeGenericSectionData") &&
            yyjson_mut_obj_add_val(doc,section_data,"title",title) && yyjson_mut_obj_add_str(doc,title,"transformedLabel","Your playlists");
    }
    yyjson_write_err error={0}; char *text=ok ? yyjson_mut_write_opts(doc,0,&allocator,bytes,&error) : NULL;
    ok=text && *bytes<r->capacity;
    if (ok) { memcpy(r->body,text,*bytes); r->body[*bytes]=0; }
    yyjson_mut_doc_free(doc); return ok;
}
