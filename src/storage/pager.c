/*
 * pager.c — 디스크 I/O 및 페이지 캐시 관리자
 *
 * 역할:
 *   데이터베이스 파일을 고정 크기 페이지 단위로 읽고 쓰는 저수준 계층이다.
 *   메모리에 MAX_FRAMES(256)개의 프레임 버퍼를 유지하며, LRU 정책으로
 *   페이지를 교체한다. 모든 상위 모듈(heap, B+ tree)은 pager를 통해서만
 *   디스크에 접근한다.
 *
 * 페이지 레이아웃(파일 전체):
 *   [page 0: DB 헤더] [page 1: 첫 번째 힙 페이지] [page 2: B+ tree 루트] ...
 *
 * 핀/언핀(pin/unpin):
 *   pager_get_page()는 프레임의 pin_count를 증가시켜 교체 대상에서 제외한다.
 *   사용이 끝나면 반드시 pager_unpin()을 호출해야 한다.
 *
 * 빈 페이지 재활용:
 *   삭제로 비워진 페이지는 free page 연결 리스트에 추가되고,
 *   pager_alloc_page()에서 새 페이지 할당 전에 먼저 재활용된다.
 */

#include "storage/pager.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── 내부 헬퍼 함수 ── */

/* 디스크에서 page_id 위치의 페이지를 buf로 직접 읽는다 (캐시 우회) */
static ssize_t pager_raw_read(pager_t *p, uint32_t page_id, uint8_t *buf)
{
    off_t off = (off_t)page_id * p->page_size;
    return pread(p->fd, buf, p->page_size, off);
}

/* buf의 내용을 디스크의 page_id 위치에 직접 쓴다 (캐시 우회) */
static ssize_t pager_raw_write(pager_t *p, uint32_t page_id, const uint8_t *buf)
{
    off_t off = (off_t)page_id * p->page_size;
    return pwrite(p->fd, buf, p->page_size, off);
}

/*
 * 프레임 배열에서 page_id에 해당하는 프레임 인덱스를 찾는다.
 * 캐시 히트 시 인덱스를 반환하고, 캐시 미스 시 -1을 반환한다.
 */
static int find_frame(pager_t *p, uint32_t page_id)
{
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (p->frames[i].is_valid && p->frames[i].page_id == page_id) {
            return i;
        }
    }
    return -1;
}

/*
 * 교체할 프레임을 선택한다.
 *
 * 1단계: 비어 있는(is_valid == false) 프레임이 있으면 바로 반환
 * 2단계: pin_count == 0인 프레임 중 used_tick이 가장 작은 것을 선택 (LRU)
 * 3단계: 선택된 프레임이 dirty이면 디스크에 플러시한 뒤 반환
 *
 * 모든 프레임이 고정(pin) 상태이면 -1을 반환한다.
 */
static int evict_frame(pager_t *p)
{
    /* 빈 프레임 탐색 */
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (!p->frames[i].is_valid) {
            return i;
        }
    }
    /* LRU: 고정되지 않은 프레임 중 가장 오래된 것 선택 */
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
    /* dirty 프레임은 디스크에 기록 후 교체 */
    if (p->frames[best].is_dirty) {
        pager_raw_write(p, p->frames[best].page_id, p->frames[best].data);
        p->frames[best].is_dirty = false;
    }
    p->frames[best].is_valid = false;
    return best;
}

/* ── 생명주기(open / close) ── */

/*
 * 데이터베이스 파일을 열거나 새로 생성한다.
 *
 * create == true:
 *   - 페이지 크기를 OS의 sysconf(_SC_PAGESIZE)로 결정
 *   - DB 헤더(page 0), 빈 힙 페이지(page 1), 빈 B+ tree 루트(page 2) 기록
 *
 * create == false:
 *   - 기존 파일의 헤더를 읽어 page_size와 스키마 정보를 복원
 *   - 매직 넘버 검증 실패 시 -1 반환
 *
 * 프레임 버퍼는 page_size에 맞춰 할당된다. 기존 DB의 page_size가
 * OS page_size와 다를 경우 프레임을 재할당한다.
 */
int pager_open(pager_t *pager, const char *path, bool create)
{
    memset(pager, 0, sizeof(*pager));

    /* OS 페이지 크기를 기본값으로 사용 */
    uint32_t ps = (uint32_t)sysconf(_SC_PAGESIZE);
    if (ps == 0 || ps == (uint32_t)-1) {
        ps = 4096;
    }

    int flags = O_RDWR;
    if (create) {
        flags |= O_CREAT | O_TRUNC;
    }
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        return -1;
    }

    pager->fd = fd;

    /* 프레임 버퍼 할당 (초기에는 OS 페이지 크기로) */
    for (int i = 0; i < MAX_FRAMES; i++) {
        pager->frames[i].data = (uint8_t *)calloc(1, ps);
        if (pager->frames[i].data == NULL) {
            close(fd);
            return -1;
        }
    }

    if (create) {
        pager->page_size = ps;

        /* DB 헤더 초기화 */
        db_header_t *h = &pager->header;
        memcpy(h->magic, DB_MAGIC, 8);
        h->version = DB_VERSION;
        h->page_size = ps;
        h->first_heap_page_id = 1;   /* 첫 번째 힙 페이지 */
        h->root_index_page_id = 2;   /* B+ tree 루트 페이지 */
        h->next_page_id = 3;         /* 다음에 할당할 페이지 ID */
        h->free_page_head = 0;       /* 빈 페이지 없음 */
        h->next_id = 1;              /* 자동 증가 ID 시작값 */
        h->row_count = 0;
        h->column_count = 0;
        h->row_size = 0;

        /* page 0: DB 헤더 기록 */
        uint8_t *buf = (uint8_t *)calloc(1, ps);
        memcpy(buf, h, sizeof(*h));
        pager_raw_write(pager, 0, buf);

        /* page 1: 빈 힙 페이지 기록 */
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

        /* page 2: 빈 B+ tree 리프 루트 기록 */
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
        /*
         * 기존 DB 열기: 헤더를 읽어 page_size를 복원한다.
         * 헤더의 page_size가 OS 페이지 크기와 다를 수 있으므로
         * 필요시 프레임 버퍼를 재할당한다.
         */
        uint32_t initial_ps = ps;
        uint8_t tmp[4096];
        pread(fd, tmp, sizeof(tmp), 0);
        db_header_t *th = (db_header_t *)tmp;
        pager->page_size = th->page_size;
        ps = pager->page_size;

        /* DB의 page_size가 초기 할당 크기와 다르면 프레임 재할당 */
        if (ps != initial_ps) {
            for (int i = 0; i < MAX_FRAMES; i++) {
                free(pager->frames[i].data);
                pager->frames[i].data = (uint8_t *)calloc(1, ps);
            }
        }

        /* 전체 헤더 페이지 읽기 */
        uint8_t *hbuf = (uint8_t *)calloc(1, ps);
        pread(fd, hbuf, ps, 0);
        memcpy(&pager->header, hbuf, sizeof(db_header_t));
        free(hbuf);

        /* 매직 넘버 검증 */
        if (memcmp(pager->header.magic, DB_MAGIC, 7) != 0) {
            fprintf(stderr, "pager: 유효하지 않은 매직 넘버입니다\n");
            close(fd);
            return -1;
        }
    }
    return 0;
}

/*
 * 데이터베이스를 닫는다.
 * 모든 dirty 프레임을 디스크에 플러시하고, 프레임 메모리를 해제한다.
 */
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

/* ── 캐시 기반 페이지 접근 ── */

/*
 * page_id에 해당하는 페이지를 반환한다.
 *
 * 캐시 히트: 기존 프레임의 pin_count를 증가시키고 포인터 반환
 * 캐시 미스: LRU 프레임을 교체하고, 디스크에서 읽어온 뒤 반환
 *
 * 반환된 포인터를 사용한 뒤에는 반드시 pager_unpin()을 호출해야 한다.
 */
uint8_t *pager_get_page(pager_t *pager, uint32_t page_id)
{
    int idx = find_frame(pager, page_id);
    if (idx >= 0) {
        /* 캐시 히트 */
        pager->frames[idx].pin_count++;
        pager->frames[idx].used_tick = ++pager->tick;
        return pager->frames[idx].data;
    }
    /* 캐시 미스: 프레임 교체 후 디스크에서 읽기 */
    idx = evict_frame(pager);
    if (idx < 0) {
        return NULL;
    }

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

/* 해당 페이지를 dirty로 표시하여, flush 시 디스크에 기록되도록 한다 */
void pager_mark_dirty(pager_t *pager, uint32_t page_id)
{
    int idx = find_frame(pager, page_id);
    if (idx >= 0) {
        pager->frames[idx].is_dirty = true;
    }
    pager->header_dirty = true;
}

/* 페이지의 pin_count를 감소시킨다. 0이 되면 LRU 교체 대상이 된다 */
void pager_unpin(pager_t *pager, uint32_t page_id)
{
    int idx = find_frame(pager, page_id);
    if (idx >= 0 && pager->frames[idx].pin_count > 0) {
        pager->frames[idx].pin_count--;
    }
}

/* ── 페이지 할당 / 해제 ── */

/*
 * 새 페이지를 할당한다.
 *
 * 1. free page 리스트에 재활용 가능한 페이지가 있으면 그것을 사용
 * 2. 없으면 next_page_id를 증가시켜 파일 끝에 새 페이지를 추가
 *
 * 반환값: 할당된 페이지 ID
 */
uint32_t pager_alloc_page(pager_t *pager)
{
    /* 빈 페이지 재활용 */
    if (pager->header.free_page_head != 0) {
        uint32_t pid = pager->header.free_page_head;
        uint8_t *page = pager_get_page(pager, pid);
        free_page_header_t fph;
        memcpy(&fph, page, sizeof(fph));
        pager->header.free_page_head = fph.next_free_page;
        /* 페이지 초기화 */
        memset(page, 0, pager->page_size);
        pager_mark_dirty(pager, pid);
        pager_unpin(pager, pid);
        return pid;
    }
    /* 새 페이지 할당: 파일 끝에 추가 */
    uint32_t pid = pager->header.next_page_id++;
    uint8_t *page = pager_get_page(pager, pid);
    memset(page, 0, pager->page_size);
    pager_mark_dirty(pager, pid);
    pager_unpin(pager, pid);
    return pid;
}

/*
 * 페이지를 해제하여 free page 리스트에 추가한다.
 * 해제된 페이지는 PAGE_TYPE_FREE로 표시되고,
 * free_page_head → 이 페이지 → 기존 리스트 순으로 연결된다.
 */
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

/* ── 플러시 ── */

/*
 * 모든 dirty 프레임을 디스크에 기록하고, 헤더 페이지도 갱신한다.
 * fsync()로 디스크 동기화를 보장한다.
 */
void pager_flush_all(pager_t *pager)
{
    /* dirty 프레임을 디스크에 기록 */
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (pager->frames[i].is_valid && pager->frames[i].is_dirty) {
            pager_raw_write(pager, pager->frames[i].page_id, pager->frames[i].data);
            pager->frames[i].is_dirty = false;
        }
    }
    /* DB 헤더(page 0) 기록 */
    uint8_t *hbuf = (uint8_t *)calloc(1, pager->page_size);
    memcpy(hbuf, &pager->header, sizeof(db_header_t));
    pager_raw_write(pager, 0, hbuf);
    free(hbuf);
    fsync(pager->fd);
    pager->header_dirty = false;
}
