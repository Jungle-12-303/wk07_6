/*
 * pager.h — 디스크 I/O 및 페이지 캐시 관리자 인터페이스
 *
 * pager는 DB 파일을 고정 크기 페이지 단위로 관리하며,
 * MAX_FRAMES(256)개의 프레임 버퍼에 LRU 캐시를 유지한다.
 */

#ifndef PAGER_H
#define PAGER_H

#include "page_format.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_FRAMES 256  /* 메모리에 유지할 최대 페이지 프레임 수 */

/*
 * 프레임: 메모리에 캐시된 하나의 페이지를 나타낸다.
 * pin_count > 0이면 교체 대상에서 제외되며,
 * used_tick이 작을수록 LRU 교체 우선 대상이 된다.
 */
typedef struct {
    uint32_t page_id;    /* 이 프레임에 적재된 페이지 ID */
    bool     is_valid;   /* 프레임에 유효한 데이터가 있는지 여부 */
    bool     is_dirty;   /* 수정되어 디스크에 기록이 필요한지 여부 */
    uint32_t pin_count;  /* 현재 사용 중인 참조 수 (0이면 교체 가능) */
    uint64_t used_tick;  /* 마지막 접근 시점의 틱 (LRU 판별용) */
    uint8_t *data;       /* 페이지 데이터 버퍼 (page_size 바이트) */
} frame_t;

/*
 * 페이저: DB 파일 핸들과 페이지 캐시를 관리하는 최상위 구조체.
 * 모든 상위 모듈(heap, B+ tree)은 pager_t를 통해 디스크에 접근한다.
 */
typedef struct {
    int         fd;                  /* DB 파일 디스크립터 */
    uint32_t    page_size;           /* 페이지 크기 (바이트) */
    db_header_t header;              /* DB 헤더 (page 0의 인메모리 사본) */
    frame_t     frames[MAX_FRAMES];  /* 페이지 프레임 배열 */
    uint64_t    tick;                /* 전역 틱 카운터 (LRU 추적용) */
    bool        header_dirty;        /* 헤더가 수정되었는지 여부 */
} pager_t;

/* 생명주기 */
int  pager_open(pager_t *pager, const char *path, bool create);  /* DB 열기/생성 */
void pager_close(pager_t *pager);  /* DB 닫기 (flush 후 리소스 해제) */

/* 캐시 기반 페이지 접근 */
uint8_t *pager_get_page(pager_t *pager, uint32_t page_id);     /* 페이지 로드 (pin++) */
void     pager_mark_dirty(pager_t *pager, uint32_t page_id);   /* dirty 표시 */
void     pager_unpin(pager_t *pager, uint32_t page_id);        /* pin 해제 (pin--) */

/* 페이지 할당/해제 */
uint32_t pager_alloc_page(pager_t *pager);                     /* 새 페이지 할당 */
void     pager_free_page(pager_t *pager, uint32_t page_id);    /* 페이지 해제 (free 리스트에 추가) */

/* 플러시 */
void pager_flush_all(pager_t *pager);  /* 모든 dirty 프레임을 디스크에 기록 */

#endif /* PAGER_H */
