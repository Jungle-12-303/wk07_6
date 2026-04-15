/*
 * table.c — 슬롯 기반 힙 테이블 구현
 *
 * 역할:
 *   행(row) 데이터를 힙 페이지에 저장, 조회, 삭제, 스캔하는 테이블 계층이다.
 *   각 힙 페이지는 슬롯 디렉터리와 행 데이터 영역으로 구성된다.
 *
 * 힙 페이지 레이아웃 (슬롯과 행이 양방향으로 자라는 구조):
 *
 *   [헤더][slot_0][slot_1]...[slot_N]  ←  앞쪽(front)에서 뒤로 성장
 *                     [빈 공간]
 *   [row_N]...[row_1][row_0]           ←  뒤쪽(back)에서 앞으로 성장
 *
 *   free_space_offset: 페이지 끝에서부터 사용된 행 데이터의 총 크기
 *   즉, 다음 행은 page_size - free_space_offset - row_size 위치에 저장된다.
 *
 * 삭제 처리:
 *   행 삭제 시 slot.status를 SLOT_FREE로 변경하고 free_slot_head 체인에 연결한다.
 *   이후 INSERT 시 빈 슬롯을 먼저 재활용하여 공간 낭비를 줄인다.
 *
 * 힙 체인:
 *   힙 페이지들은 next_heap_page_id로 연결 리스트를 형성한다.
 *   모든 페이지가 가득 차면 새 페이지를 할당하여 체인 끝에 연결한다.
 */

#include "storage/table.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* 슬롯 디렉터리의 끝 오프셋을 계산한다 (헤더 + 슬롯 배열 크기) */
static uint16_t slots_end(uint16_t slot_count)
{
    return (uint16_t)(sizeof(heap_page_header_t) + slot_count * sizeof(slot_t));
}

/*
 * 페이지의 사용 가능한 공간을 계산한다.
 *
 *   front: 슬롯 디렉터리가 차지하는 영역의 끝
 *   back:  행 데이터가 차지하는 영역의 시작 (page_size - free_space_offset)
 *   사용 가능 = back - front
 */
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

/*
 * 행을 삽입할 수 있는 힙 페이지를 찾는다.
 *
 * 탐색 순서:
 *   1. free_slot_head가 있는 페이지 (삭제된 슬롯 재활용 가능)
 *   2. 새 슬롯 + 행을 위한 충분한 공간이 있는 페이지
 *   3. 없으면 0을 반환 → 호출자가 새 페이지를 할당해야 함
 */
static uint32_t find_heap_page(pager_t *pager, uint16_t row_size)
{
    uint32_t pid = pager->header.first_heap_page_id;
    while (pid != 0) {
        uint8_t *page = pager_get_page(pager, pid);
        heap_page_header_t hph;
        memcpy(&hph, page, sizeof(hph));
        pager_unpin(pager, pid);

        /* 재활용 가능한 빈 슬롯이 있는지 확인 */
        if (hph.free_slot_head != SLOT_NONE)
        {
            return pid;
        }

        /* 새 슬롯 + 행 데이터를 위한 공간이 충분한지 확인 */
        uint16_t need = (uint16_t)(sizeof(slot_t) + row_size);
        if (available_space(pager, &hph) >= need)
        {
            return pid;
        }

        pid = hph.next_heap_page_id;
    }
    return 0; /* 적합한 페이지 없음 */
}

/*
 * 힙에 행을 삽입하고, 삽입된 위치(page_id, slot_id)를 반환한다.
 *
 * 과정:
 *   1. find_heap_page()로 삽입 가능한 페이지 탐색
 *   2. 없으면 새 힙 페이지를 할당하고 체인 끝에 연결
 *   3. 빈 슬롯이 있으면 재활용, 없으면 새 슬롯 생성
 *   4. 행 데이터를 해당 위치에 복사
 */
row_ref_t heap_insert(pager_t *pager, const uint8_t *row_data, uint16_t row_size)
{
    row_ref_t ref = {0, 0};
    uint32_t pid = find_heap_page(pager, row_size);

    if (pid == 0) {
        /* 새 힙 페이지 할당 */
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

        /* 힙 체인의 마지막 페이지를 찾아 새 페이지를 연결 */
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

    /* 선택된 페이지에 행 삽입 */
    uint8_t *page = pager_get_page(pager, pid);
    heap_page_header_t hph;
    memcpy(&hph, page, sizeof(hph));

    uint16_t slot_id;
    slot_t   slot;

    if (hph.free_slot_head != SLOT_NONE) {
        /*
         * 빈 슬롯 재활용: free_slot_head에서 슬롯을 꺼낸다.
         * 행 데이터는 이전 삭제된 행의 위치(slot.offset)에 덮어쓴다.
         */
        slot_id = hph.free_slot_head;
        size_t slot_off = sizeof(heap_page_header_t) + slot_id * sizeof(slot_t);
        memcpy(&slot, page + slot_off, sizeof(slot));
        hph.free_slot_head = slot.next_free;

        memcpy(page + slot.offset, row_data, row_size);
        slot.status = SLOT_ALIVE;
        slot.next_free = SLOT_NONE;
        memcpy(page + slot_off, &slot, sizeof(slot));
    } else {
        /*
         * 새 슬롯 생성: 슬롯 디렉터리 끝에 추가하고,
         * 행 데이터는 페이지 뒤쪽에서 앞으로 자라도록 기록한다.
         */
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

/*
 * row_ref_t로 지정된 행을 조회한다.
 *
 * 슬롯이 SLOT_ALIVE가 아니면 NULL을 반환한다.
 * 반환된 포인터는 페이지 버퍼 내부를 가리키므로,
 * 호출자가 사용 후 반드시 pager_unpin(pager, ref.page_id)을 호출해야 한다.
 */
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

    /* 주의: 호출자가 사용 후 pager_unpin(pager, ref.page_id)을 호출해야 한다 */
    return page + slot.offset;
}

/*
 * row_ref_t로 지정된 행을 삭제한다.
 *
 * 실제 데이터는 지우지 않고 slot.status를 SLOT_FREE로 변경한 뒤,
 * free_slot_head 체인에 연결한다 (톰스톤 삭제 방식).
 * 이후 INSERT 시 해당 슬롯을 재활용한다.
 */
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

    /* 톰스톤 처리: 슬롯을 FREE로 표시하고 빈 슬롯 체인에 연결 */
    slot.status = SLOT_FREE;
    slot.next_free = hph.free_slot_head;
    hph.free_slot_head = ref.slot_id;

    memcpy(page + slot_off, &slot, sizeof(slot));
    memcpy(page, &hph, sizeof(hph));
    pager_mark_dirty(pager, ref.page_id);
    pager_unpin(pager, ref.page_id);
    return 0;
}

/*
 * 모든 힙 페이지를 순회하며, 살아 있는 행마다 콜백을 호출한다.
 *
 * 힙 체인(first_heap_page → next → next → ...)을 따라가며
 * 각 페이지의 슬롯을 검사하고, SLOT_ALIVE인 행에 대해 cb를 호출한다.
 * 콜백이 false를 반환하면 스캔을 즉시 중단한다.
 */
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
