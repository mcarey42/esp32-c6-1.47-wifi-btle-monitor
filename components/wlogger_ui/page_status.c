#include "page_internal.h"
ui_page_t page_status_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent); lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_label_set_text(lv_label_create(p), "File / SD (TBD)");
    return (ui_page_t){ .root = p, .update = NULL };
}
