/*
 * page_format.h — 디스크 페이지 형식 및 핵심 구조체 정의
 *
 * 모든 페이지의 공통 구조와 타입 코드, DB 헤더, 힙 페이지,
 * B+ tree 리프/내부 노드, 빈 페이지 등의 온디스크 레이아웃을 정의한다.
 * packed 속성으로 구조체 패딩을 제거하여 디스크 직렬화 시 정확한 바이트 배치를 보장한다.
 */

#ifndef PAGE_FORMAT_H
#define PAGE_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

/* ── 페이지 유형 코드 ──
 * 모든 페이지의 첫 4바이트(page_type)로 페이지 종류를 식별한다.
 */
typedef enum {
    PAGE_TYPE_HEADER   = 0x01,  /* DB 헤더 페이지 (page 0) */
    PAGE_TYPE_HEAP     = 0x02,  /* 힙 데이터 페이지 (행 저장) */
    PAGE_TYPE_LEAF     = 0x03,  /* B+ tree 리프 노드 */
    PAGE_TYPE_INTERNAL = 0x04,  /* B+ tree 내부 노드 */
    PAGE_TYPE_FREE     = 0x05   /* 빈 페이지 (재활용 대기) */
} page_type_t;

/* ── DB 헤더 (page 0) ── */
#define DB_MAGIC    "MINIDB\0"  /* 매직 넘버: 파일이 minidb인지 식별 */
#define DB_VERSION  1           /* DB 파일 형식 버전 */
#define MAX_COLUMNS 16          /* 테이블당 최대 컬럼 수 */

/* 컬럼 데이터 타입 */
typedef enum {
    COL_TYPE_INT     = 1,   /* 4바이트 정수 (int32_t) */
    COL_TYPE_BIGINT  = 2,   /* 8바이트 정수 (int64_t) */
    COL_TYPE_VARCHAR = 3    /* N바이트 가변 문자열 */
} column_type_t;

/*
 * 컬럼 메타데이터.
 * DB 헤더의 columns[] 배열에 저장되어 테이블 스키마를 구성한다.
 */
typedef struct {
    char          name[32];   /* 컬럼 이름 */
    uint8_t       type;       /* 컬럼 타입 (column_type_t) */
    uint16_t      size;       /* 바이트 크기 (INT=4, BIGINT=8, VARCHAR=N) */
    uint16_t      offset;     /* 행 내 바이트 오프셋 (schema_compute_layout이 계산) */
    uint8_t       is_system;  /* 시스템 컬럼 여부 (1 = id 컬럼) */
} __attribute__((packed)) column_meta_t;

/*
 * DB 헤더 (page 0에 저장).
 * 데이터베이스의 전역 메타데이터를 관리한다.
 */
typedef struct {
    char     magic[8];              /* 매직 넘버 ("MINIDB\0") */
    uint32_t version;               /* DB 파일 형식 버전 */
    uint32_t page_size;             /* 페이지 크기 (OS sysconf에서 결정) */
    uint32_t root_index_page_id;    /* B+ tree 루트 페이지 ID */
    uint32_t first_heap_page_id;    /* 첫 번째 힙 페이지 ID */
    uint32_t next_page_id;          /* 다음에 할당할 페이지 ID */
    uint32_t free_page_head;        /* 빈 페이지 연결 리스트의 헤드 (0 = 없음) */
    uint64_t next_id;               /* 다음 자동 증가 ID */
    uint64_t row_count;             /* 살아 있는 행 수 */
    uint16_t column_count;          /* 등록된 컬럼 수 */
    uint16_t row_size;              /* 한 행의 직렬화 크기 (바이트) */
    column_meta_t columns[MAX_COLUMNS]; /* 컬럼 메타데이터 배열 */
} __attribute__((packed)) db_header_t;

/* ── 행 참조 ──
 * 힙 페이지 내 특정 행의 위치를 가리킨다.
 * B+ tree의 값(value)으로 사용된다: key(id) → row_ref_t
 */
typedef struct {
    uint32_t page_id;   /* 행이 저장된 힙 페이지 ID */
    uint16_t slot_id;   /* 페이지 내 슬롯 번호 */
} __attribute__((packed)) row_ref_t;

/* ── 빈 페이지 헤더 ──
 * 해제된 페이지의 첫 부분에 기록되며, 빈 페이지 연결 리스트를 형성한다.
 */
typedef struct {
    uint32_t page_type;       /* PAGE_TYPE_FREE */
    uint32_t next_free_page;  /* 다음 빈 페이지 ID (0 = 리스트 끝) */
} free_page_header_t;

/* ── 힙 페이지 ──
 * 슬롯 기반 힙 페이지의 헤더와 슬롯 구조를 정의한다.
 *
 * 페이지 레이아웃:
 *   [헤더][slot_0][slot_1]...[slot_N]  ←  앞에서 뒤로 성장
 *                    [빈 공간]
 *   [row_N]...[row_1][row_0]           ←  뒤에서 앞으로 성장
 */

/* 슬롯 상태 코드 */
#define SLOT_ALIVE 0x01   /* 유효한 행이 존재 */
#define SLOT_DEAD  0x02   /* 삭제 예정 (미사용) */
#define SLOT_FREE  0x03   /* 빈 슬롯 (재활용 가능) */
#define SLOT_NONE  0xFFFF /* 빈 슬롯 없음 표시 */

/* 힙 페이지 헤더 */
typedef struct {
    uint32_t page_type;          /* PAGE_TYPE_HEAP */
    uint32_t next_heap_page_id;  /* 다음 힙 페이지 ID (연결 리스트) */
    uint16_t slot_count;         /* 할당된 슬롯 총 수 (빈 슬롯 포함) */
    uint16_t free_slot_head;     /* 빈 슬롯 체인의 헤드 (SLOT_NONE = 없음) */
    uint16_t free_space_offset;  /* 페이지 끝에서 사용된 행 데이터의 총 크기 */
    uint16_t reserved;           /* 예약 (정렬용) */
} __attribute__((packed)) heap_page_header_t;

/* 슬롯 디렉터리 엔트리 */
typedef struct {
    uint16_t offset;    /* 페이지 내 행 데이터의 시작 오프셋 */
    uint16_t status;    /* 슬롯 상태 (SLOT_ALIVE / SLOT_DEAD / SLOT_FREE) */
    uint16_t next_free; /* 빈 슬롯 체인의 다음 슬롯 ID (FREE일 때만 유효) */
    uint16_t reserved;  /* 예약 (정렬용) */
} __attribute__((packed)) slot_t;

/* ── B+ tree 리프 노드 ──
 * 리프 노드는 (key, row_ref_t) 쌍을 저장하며,
 * 이중 연결 리스트(prev/next)로 범위 스캔을 지원한다.
 *
 * 레이아웃: [리프헤더][entry_0][entry_1]...[entry_N]
 */
typedef struct {
    uint32_t page_type;           /* PAGE_TYPE_LEAF */
    uint32_t parent_page_id;      /* 부모 내부 노드의 페이지 ID (루트면 0) */
    uint32_t key_count;           /* 저장된 키 수 */
    uint32_t next_leaf_page_id;   /* 오른쪽 형제 리프 (0 = 없음) */
    uint32_t prev_leaf_page_id;   /* 왼쪽 형제 리프 (0 = 없음) */
} __attribute__((packed)) leaf_page_header_t;

/* 리프 엔트리: key → 힙 행 위치 매핑 */
typedef struct {
    uint64_t  key;      /* 인덱스 키 (id 값) */
    row_ref_t row_ref;  /* 힙 페이지 내 행 위치 */
} __attribute__((packed)) leaf_entry_t;

/* ── B+ tree 내부 노드 ──
 * 내부 노드는 자식 노드로의 라우팅 정보를 저장한다.
 *
 * 자식 탐색 규칙:
 *   key < entries[0].key          → leftmost_child로 이동
 *   entries[i].key ≤ key < entries[i+1].key → entries[i].right_child로 이동
 *   key ≥ entries[last].key       → entries[last].right_child로 이동
 *
 * 레이아웃: [내부헤더][entry_0][entry_1]...[entry_N]
 */
typedef struct {
    uint32_t page_type;               /* PAGE_TYPE_INTERNAL */
    uint32_t parent_page_id;          /* 부모 노드 페이지 ID (루트면 0) */
    uint32_t key_count;               /* 저장된 키 수 */
    uint32_t leftmost_child_page_id;  /* 가장 왼쪽 자식 페이지 ID */
} __attribute__((packed)) internal_page_header_t;

/* 내부 노드 엔트리: 구분 키 + 오른쪽 자식 */
typedef struct {
    uint64_t key;                   /* 구분 키 */
    uint32_t right_child_page_id;   /* 이 키 이상인 값이 있는 자식 페이지 ID */
} __attribute__((packed)) internal_entry_t;

#endif /* PAGE_FORMAT_H */
