#ifndef PAGER_H
#define PAGER_H

#include "page_format.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_FRAMES 256

typedef struct {
    uint32_t page_id;
    bool     is_valid;
    bool     is_dirty;
    uint32_t pin_count;
    uint64_t used_tick;
    uint8_t *data;          /* page_size bytes */
} frame_t;

typedef struct {
    int         fd;
    uint32_t    page_size;
    db_header_t header;
    frame_t     frames[MAX_FRAMES];
    uint64_t    tick;
    bool        header_dirty;
} pager_t;

/* lifecycle */
int  pager_open(pager_t *pager, const char *path, bool create);
void pager_close(pager_t *pager);

/* page I/O through cache */
uint8_t *pager_get_page(pager_t *pager, uint32_t page_id);
void     pager_mark_dirty(pager_t *pager, uint32_t page_id);
void     pager_unpin(pager_t *pager, uint32_t page_id);

/* allocation */
uint32_t pager_alloc_page(pager_t *pager);
void     pager_free_page(pager_t *pager, uint32_t page_id);

/* flush */
void pager_flush_all(pager_t *pager);

#endif /* PAGER_H */
