#include "storage/table.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Heap page layout (growing towards each other):
 *   [header][slot_0][slot_1]...[slot_N]  ←  front
 *                      [free space]
 *   [row_N]...[row_1][row_0]             ←  back (page_size boundary)
 *
 * free_space_offset tracks the end of the last row payload from the page end.
 * i.e., next row starts at: page_size - free_space_offset - row_size
 */

static uint16_t slots_end(uint16_t slot_count)
{
    return (uint16_t)(sizeof(heap_page_header_t) + slot_count * sizeof(slot_t));
}

static uint16_t available_space(pager_t *pager, heap_page_header_t *hph)
{
    uint16_t front = slots_end(hph->slot_count);
    uint16_t back  = (uint16_t)(pager->page_size - hph->free_space_offset);
    if (back <= front)
    {
        return 0;
    }
    return back - front;
}

/* Find a heap page with a free slot or enough space. Returns page_id. */
static uint32_t find_heap_page(pager_t *pager, uint16_t row_size)
{
    uint32_t pid = pager->header.first_heap_page_id;
    while (pid != 0) {
        uint8_t *page = pager_get_page(pager, pid);
        heap_page_header_t hph;
        memcpy(&hph, page, sizeof(hph));
        pager_unpin(pager, pid);

        /* has free slot? */
        if (hph.free_slot_head != SLOT_NONE)
        {
            return pid;
        }

        /* enough space for new slot + row? */
        uint16_t need = (uint16_t)(sizeof(slot_t) + row_size);
        if (available_space(pager, &hph) >= need)
        {
            return pid;
        }

        pid = hph.next_heap_page_id;
    }
    return 0; /* none found */
}

row_ref_t heap_insert(pager_t *pager, const uint8_t *row_data, uint16_t row_size)
{
    row_ref_t ref = {0, 0};
    uint32_t pid = find_heap_page(pager, row_size);

    if (pid == 0) {
        /* allocate new heap page */
        pid = pager_alloc_page(pager);
        uint8_t *page = pager_get_page(pager, pid);

        heap_page_header_t hph = {
            .page_type = PAGE_TYPE_HEAP,
            .next_heap_page_id = 0,
            .slot_count = 0,
            .free_slot_head = SLOT_NONE,
            .free_space_offset = 0,
            .reserved = 0
        };
        memcpy(page, &hph, sizeof(hph));
        pager_mark_dirty(pager, pid);
        pager_unpin(pager, pid);

        /* link to end of heap chain */
        uint32_t cur = pager->header.first_heap_page_id;
        uint32_t prev_pid = 0;
        while (cur != 0) {
            uint8_t *cp = pager_get_page(pager, cur);
            heap_page_header_t ch;
            memcpy(&ch, cp, sizeof(ch));
            pager_unpin(pager, cur);
            if (ch.next_heap_page_id == 0) { prev_pid = cur; break; }
            cur = ch.next_heap_page_id;
        }
        if (prev_pid != 0) {
            uint8_t *pp = pager_get_page(pager, prev_pid);
            heap_page_header_t ph;
            memcpy(&ph, pp, sizeof(ph));
            ph.next_heap_page_id = pid;
            memcpy(pp, &ph, sizeof(ph));
            pager_mark_dirty(pager, prev_pid);
            pager_unpin(pager, prev_pid);
        }
    }

    /* insert into page */
    uint8_t *page = pager_get_page(pager, pid);
    heap_page_header_t hph;
    memcpy(&hph, page, sizeof(hph));

    uint16_t slot_id;
    slot_t   slot;

    if (hph.free_slot_head != SLOT_NONE) {
        /* reuse free slot */
        slot_id = hph.free_slot_head;
        size_t slot_off = sizeof(heap_page_header_t) + slot_id * sizeof(slot_t);
        memcpy(&slot, page + slot_off, sizeof(slot));
        hph.free_slot_head = slot.next_free;

        /* row payload already has space at slot.offset */
        memcpy(page + slot.offset, row_data, row_size);
        slot.status = SLOT_ALIVE;
        slot.next_free = SLOT_NONE;
        memcpy(page + slot_off, &slot, sizeof(slot));
    } else {
        /* new slot */
        slot_id = hph.slot_count;
        uint16_t row_offset = (uint16_t)(pager->page_size - hph.free_space_offset - row_size);

        slot.offset = row_offset;
        slot.status = SLOT_ALIVE;
        slot.next_free = SLOT_NONE;
        slot.reserved = 0;

        size_t slot_off = sizeof(heap_page_header_t) + slot_id * sizeof(slot_t);
        memcpy(page + slot_off, &slot, sizeof(slot));
        memcpy(page + row_offset, row_data, row_size);

        hph.slot_count++;
        hph.free_space_offset += row_size;
    }

    memcpy(page, &hph, sizeof(hph));
    pager_mark_dirty(pager, pid);
    pager_unpin(pager, pid);

    ref.page_id = pid;
    ref.slot_id = slot_id;
    return ref;
}

const uint8_t *heap_fetch(pager_t *pager, row_ref_t ref, uint16_t row_size)
{
    (void)row_size;
    uint8_t *page = pager_get_page(pager, ref.page_id);
    if (page == NULL)
    {
        return NULL;
    }

    slot_t slot;
    size_t slot_off = sizeof(heap_page_header_t) + ref.slot_id * sizeof(slot_t);
    memcpy(&slot, page + slot_off, sizeof(slot));

    if (slot.status != SLOT_ALIVE) {
        pager_unpin(pager, ref.page_id);
        return NULL;
    }

    /* NOTE: caller must call pager_unpin(pager, ref.page_id) after use */
    return page + slot.offset;
}

int heap_delete(pager_t *pager, row_ref_t ref)
{
    uint8_t *page = pager_get_page(pager, ref.page_id);
    if (page == NULL)
    {
        return -1;
    }

    heap_page_header_t hph;
    memcpy(&hph, page, sizeof(hph));

    slot_t slot;
    size_t slot_off = sizeof(heap_page_header_t) + ref.slot_id * sizeof(slot_t);
    memcpy(&slot, page + slot_off, sizeof(slot));

    if (slot.status != SLOT_ALIVE) {
        pager_unpin(pager, ref.page_id);
        return -1;
    }

    slot.status = SLOT_FREE;
    slot.next_free = hph.free_slot_head;
    hph.free_slot_head = ref.slot_id;

    memcpy(page + slot_off, &slot, sizeof(slot));
    memcpy(page, &hph, sizeof(hph));
    pager_mark_dirty(pager, ref.page_id);
    pager_unpin(pager, ref.page_id);
    return 0;
}

void heap_scan(pager_t *pager, uint16_t row_size, scan_cb cb, void *ctx)
{
    (void)row_size;
    uint32_t pid = pager->header.first_heap_page_id;
    while (pid != 0) {
        uint8_t *page = pager_get_page(pager, pid);
        heap_page_header_t hph;
        memcpy(&hph, page, sizeof(hph));

        for (uint16_t i = 0; i < hph.slot_count; i++) {
            slot_t slot;
            size_t slot_off = sizeof(heap_page_header_t) + i * sizeof(slot_t);
            memcpy(&slot, page + slot_off, sizeof(slot));
            if (slot.status == SLOT_ALIVE) {
                row_ref_t ref = { .page_id = pid, .slot_id = i };
                if (!cb(page + slot.offset, ref, ctx)) {
                    pager_unpin(pager, pid);
                    return;
                }
            }
        }

        uint32_t next = hph.next_heap_page_id;
        pager_unpin(pager, pid);
        pid = next;
    }
}
