#include "storage/pager.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── helpers ── */

static ssize_t pager_raw_read(pager_t *p, uint32_t page_id, uint8_t *buf)
{
    off_t off = (off_t)page_id * p->page_size;
    return pread(p->fd, buf, p->page_size, off);
}

static ssize_t pager_raw_write(pager_t *p, uint32_t page_id, const uint8_t *buf)
{
    off_t off = (off_t)page_id * p->page_size;
    return pwrite(p->fd, buf, p->page_size, off);
}

static int find_frame(pager_t *p, uint32_t page_id)
{
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (p->frames[i].is_valid && p->frames[i].page_id == page_id) {
            return i;
        }
    }
    return -1;
}

static int evict_frame(pager_t *p)
{
    /* find free frame first */
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (!p->frames[i].is_valid) {
            return i;
        }
    }
    /* LRU among unpinned */
    int best = -1;
    uint64_t min_tick = UINT64_MAX;
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (p->frames[i].pin_count == 0 && p->frames[i].used_tick < min_tick) {
            min_tick = p->frames[i].used_tick;
            best = i;
        }
    }
    if (best < 0) {
        fprintf(stderr, "pager: 모든 프레임이 고정되어 교체할 수 없습니다\n");
        return -1;
    }
    /* flush if dirty */
    if (p->frames[best].is_dirty) {
        pager_raw_write(p, p->frames[best].page_id, p->frames[best].data);
        p->frames[best].is_dirty = false;
    }
    p->frames[best].is_valid = false;
    return best;
}

/* ── lifecycle ── */

int pager_open(pager_t *pager, const char *path, bool create)
{
    memset(pager, 0, sizeof(*pager));

    uint32_t ps = (uint32_t)sysconf(_SC_PAGESIZE);
    if (ps == 0 || ps == (uint32_t)-1) ps = 4096;

    int flags = O_RDWR;
    if (create) {
        flags |= O_CREAT | O_TRUNC;
    }
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        return -1;
    }

    pager->fd = fd;

    /* allocate frame buffers */
    for (int i = 0; i < MAX_FRAMES; i++) {
        pager->frames[i].data = (uint8_t *)calloc(1, ps);
        if (pager->frames[i].data == NULL) {
            close(fd);
            return -1;
        }
    }

    if (create) {
        pager->page_size = ps;
        /* init header */
        db_header_t *h = &pager->header;
        memcpy(h->magic, DB_MAGIC, 8);
        h->version = DB_VERSION;
        h->page_size = ps;
        h->first_heap_page_id = 1;
        h->root_index_page_id = 2;
        h->next_page_id = 3;
        h->free_page_head = 0;
        h->next_id = 1;
        h->row_count = 0;
        h->column_count = 0;
        h->row_size = 0;

        /* write header page */
        uint8_t *buf = (uint8_t *)calloc(1, ps);
        memcpy(buf, h, sizeof(*h));
        pager_raw_write(pager, 0, buf);

        /* write empty first heap page */
        memset(buf, 0, ps);
        heap_page_header_t hph = {
            .page_type = PAGE_TYPE_HEAP,
            .next_heap_page_id = 0,
            .slot_count = 0,
            .free_slot_head = SLOT_NONE,
            .free_space_offset = 0,
            .reserved = 0
        };
        memcpy(buf, &hph, sizeof(hph));
        pager_raw_write(pager, 1, buf);

        /* write empty leaf root page */
        memset(buf, 0, ps);
        leaf_page_header_t lph = {
            .page_type = PAGE_TYPE_LEAF,
            .parent_page_id = 0,
            .key_count = 0,
            .next_leaf_page_id = 0,
            .prev_leaf_page_id = 0
        };
        memcpy(buf, &lph, sizeof(lph));
        pager_raw_write(pager, 2, buf);

        free(buf);
        fsync(fd);
    } else {
        /* read existing header */
        /* first read with a temp buffer to get page_size */
        uint32_t initial_ps = ps;
        uint8_t tmp[4096];
        pread(fd, tmp, sizeof(tmp), 0);
        db_header_t *th = (db_header_t *)tmp;
        pager->page_size = th->page_size;
        ps = pager->page_size;

        /* re-alloc frames if page_size differs from initial allocation */
        if (ps != initial_ps) {
            for (int i = 0; i < MAX_FRAMES; i++) {
                free(pager->frames[i].data);
                pager->frames[i].data = (uint8_t *)calloc(1, ps);
            }
        }

        uint8_t *hbuf = (uint8_t *)calloc(1, ps);
        pread(fd, hbuf, ps, 0);
        memcpy(&pager->header, hbuf, sizeof(db_header_t));
        free(hbuf);

        if (memcmp(pager->header.magic, DB_MAGIC, 7) != 0) {
            fprintf(stderr, "pager: 유효하지 않은 매직 넘버입니다\n");
            close(fd);
            return -1;
        }
    }
    return 0;
}

void pager_close(pager_t *pager)
{
    pager_flush_all(pager);
    for (int i = 0; i < MAX_FRAMES; i++) {
        free(pager->frames[i].data);
        pager->frames[i].data = NULL;
    }
    if (pager->fd >= 0) {
        close(pager->fd);
        pager->fd = -1;
    }
}

/* ── cached page access ── */

uint8_t *pager_get_page(pager_t *pager, uint32_t page_id)
{
    int idx = find_frame(pager, page_id);
    if (idx >= 0) {
        pager->frames[idx].pin_count++;
        pager->frames[idx].used_tick = ++pager->tick;
        return pager->frames[idx].data;
    }
    idx = evict_frame(pager);
    if (idx < 0) return NULL;

    frame_t *f = &pager->frames[idx];
    f->page_id = page_id;
    f->is_valid = true;
    f->is_dirty = false;
    f->pin_count = 1;
    f->used_tick = ++pager->tick;
    memset(f->data, 0, pager->page_size);
    pager_raw_read(pager, page_id, f->data);
    return f->data;
}

void pager_mark_dirty(pager_t *pager, uint32_t page_id)
{
    int idx = find_frame(pager, page_id);
    if (idx >= 0) {
        pager->frames[idx].is_dirty = true;
    }
    pager->header_dirty = true;
}

void pager_unpin(pager_t *pager, uint32_t page_id)
{
    int idx = find_frame(pager, page_id);
    if (idx >= 0 && pager->frames[idx].pin_count > 0) {
        pager->frames[idx].pin_count--;
    }
}

/* ── allocation ── */

uint32_t pager_alloc_page(pager_t *pager)
{
    if (pager->header.free_page_head != 0) {
        uint32_t pid = pager->header.free_page_head;
        uint8_t *page = pager_get_page(pager, pid);
        free_page_header_t fph;
        memcpy(&fph, page, sizeof(fph));
        pager->header.free_page_head = fph.next_free_page;
        /* clear page */
        memset(page, 0, pager->page_size);
        pager_mark_dirty(pager, pid);
        pager_unpin(pager, pid);
        return pid;
    }
    uint32_t pid = pager->header.next_page_id++;
    /* touch the page so file extends */
    uint8_t *page = pager_get_page(pager, pid);
    memset(page, 0, pager->page_size);
    pager_mark_dirty(pager, pid);
    pager_unpin(pager, pid);
    return pid;
}

void pager_free_page(pager_t *pager, uint32_t page_id)
{
    uint8_t *page = pager_get_page(pager, page_id);
    free_page_header_t fph = {
        .page_type = PAGE_TYPE_FREE,
        .next_free_page = pager->header.free_page_head
    };
    memset(page, 0, pager->page_size);
    memcpy(page, &fph, sizeof(fph));
    pager_mark_dirty(pager, page_id);
    pager_unpin(pager, page_id);
    pager->header.free_page_head = page_id;
}

/* ── flush ── */

void pager_flush_all(pager_t *pager)
{
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (pager->frames[i].is_valid && pager->frames[i].is_dirty) {
            pager_raw_write(pager, pager->frames[i].page_id, pager->frames[i].data);
            pager->frames[i].is_dirty = false;
        }
    }
    /* write header */
    uint8_t *hbuf = (uint8_t *)calloc(1, pager->page_size);
    memcpy(hbuf, &pager->header, sizeof(db_header_t));
    pager_raw_write(pager, 0, hbuf);
    free(hbuf);
    fsync(pager->fd);
    pager->header_dirty = false;
}
