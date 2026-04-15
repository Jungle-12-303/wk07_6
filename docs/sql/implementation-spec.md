# 디스크 기반 SQL 엔진 구현 설계서 (확정판)

> 이 문서는 모든 설계 결정이 확정된 뒤 작성된 단일 구현 명세다.
> 기존 문서(b-plus-tree-sql-plan, system-perspective-guide, why-bptree-and-disk-pages,
> sql-feature-scope-roadmap, week7-bptree-sql-blueprint)의 내용을 흡수하고,
> 확정 결정을 반영하여 **코드 리뷰 가능한 수준의 최소 구현 명세**로 통합했다.

---

## 0. 확정된 설계 결정 요약

| 결정 항목 | 확정 내용 | 근거 |
|-----------|-----------|------|
| I/O 방식 | `pread`/`pwrite` (프로세스 단 직접 관리) | pager 학습 목적, flush 시점 제어, mmap은 OS에 I/O 위임 |
| VACUUM FULL | 구현하지 않음 | tombstone + free slot 우선, free page list 확장 설계 |
| page size | OS page size 동적 로드 (`sysconf(_SC_PAGESIZE)`) | 이식성 확보, 대부분 4096 |
| row schema | 스키마 문법으로 컬럼별 바이트 수 정의 (기본 타입만) | `CREATE TABLE` 시 고정 길이 row layout 생성 |
| flush 시점 | commit/종료 시 dirty page 일괄 flush | statement 단위 flush의 I/O 병목 회피 |
| benchmark | cold start / warm cache 모두 측정 | 공정한 성능 비교 |
| DB file | 단일 `.db` 바이너리 파일 | SQLite 방식, 설명 용이 |
| 공간 재사용 | tombstone + free slot 필수 구현, free page list 확장 설계 | 단계적 확장 |
| 동시성 | DB 전역 RW lock | page-level latch 제외, 복잡도 통제 |

---

## 1. 아키텍처

```text
input (REPL / .sql file)
  -> lexer / parser
  -> statement AST
  -> planner (rule-based)
  -> executor
  -> storage engine
      -> schema layout
      -> pager (page frame cache, pread/pwrite)
      -> slotted heap table
      -> B+ tree (on-disk, page 기반)
      -> page allocator (free page list)
      -> database.db (단일 바이너리 파일)
```

핵심 규칙 6가지:

1. parser는 storage를 직접 호출하지 않는다.
2. planner가 `WHERE id = ?` 여부에 따라 access path를 선택한다.
3. row는 heap page에 저장하고, B+ tree leaf에는 `key + row_ref`만 저장한다.
4. row별 `malloc()`을 피하고 page 중심 메모리 모델을 유지한다.
5. delete는 file compaction이 아니라 tombstone + free slot/page 재사용으로 처리한다.
6. B+ tree의 노드 = 파일의 page. 자식을 포인터가 아니라 page 번호로 가리킨다.

---

## 2. 모듈 구조

```text
include/
  sql/
    lexer.h          # 토큰 정의, tokenize API
    parser.h         # parse API → statement_t
    statement.h      # statement_t, predicate_kind_t
    planner.h        # plan_t, access_path_t
    executor.h       # execute API
  storage/
    pager.h          # page read/write/alloc/free/flush
    db_file.h        # db_open, db_close, db_header_t
    schema.h         # row_layout_t, column_meta_t
    table.h          # heap insert/fetch/delete/scan
    bptree.h         # search/insert/delete/split/merge
    page_format.h    # page type enum, header structs

src/
  sql/
    lexer.c
    parser.c
    planner.c
    executor.c
  storage/
    pager.c
    db_file.c
    schema.c
    table.c
    bptree.c
    page_format.c
  main.c             # REPL loop, .sql file 실행

tests/
  unit/
    test_pager.c
    test_schema.c
    test_table.c
    test_bptree.c
    test_planner.c
  integration/
    test_insert_select.c
    test_persistence.c
    test_delete_reuse.c
  benchmark/
    bench_insert.c
    bench_lookup.c
```

빌드: `Makefile` (`-Wall -Wextra -Werror -fsanitize=address,undefined`)

---

## 3. 바이너리 파일 구조

### 3.1 전체 레이아웃

```text
page 0  : DB header
page 1  : first heap page
page 2  : B+ tree root page
page 3+ : 추가 heap / index / free pages
```

### 3.2 DB header page (page 0)

```c
#define DB_MAGIC "MINIDB\0\0"  // 8 bytes
#define DB_VERSION 1

typedef struct {
    char     magic[8];              // "MINIDB\0\0"
    uint32_t version;               // 1
    uint32_t page_size;             // sysconf(_SC_PAGESIZE) 결과 (보통 4096)
    uint32_t root_index_page_id;    // B+ tree root page (초기값 2)
    uint32_t first_heap_page_id;    // 첫 heap page (초기값 1)
    uint32_t next_page_id;          // 다음 할당할 page id
    uint32_t free_page_head;        // free page list head (0이면 없음)
    uint64_t next_id;               // auto-increment id 다음 값
    uint64_t row_count;             // 현재 live row 수
    // schema metadata (단일 테이블이므로 header에 직접 저장)
    uint16_t column_count;          // 컬럼 수
    uint16_t row_size;              // 계산된 고정 row 크기(바이트)
    // column_meta는 header page 나머지 공간에 직렬화
    // column_meta_t columns[column_count];
} db_header_t;
```

page_size 결정 로직:

```c
// 새 DB 생성 시
uint32_t page_size = (uint32_t)sysconf(_SC_PAGESIZE);
if (page_size == 0 || page_size == (uint32_t)-1) {
    page_size = 4096;  // fallback
}

// 기존 DB 열기 시
// header에서 page_size를 읽어 pager에 세팅
// (다른 OS에서 만든 DB를 열어도 원래 page_size를 존중)
```

### 3.3 page type enum

```c
typedef enum {
    PAGE_TYPE_HEADER   = 0x01,
    PAGE_TYPE_HEAP     = 0x02,
    PAGE_TYPE_LEAF     = 0x03,
    PAGE_TYPE_INTERNAL = 0x04,
    PAGE_TYPE_FREE     = 0x05
} page_type_t;
```

### 3.4 free page 구조

free page list는 단순 singly linked list로 구현한다.

```c
typedef struct {
    uint32_t page_type;       // PAGE_TYPE_FREE
    uint32_t next_free_page;  // 다음 free page id (0이면 끝)
} free_page_header_t;
```

```text
db_header.free_page_head → page X → page Y → page Z → 0 (끝)
```

malloc의 explicit free list와 동일한 구조다.

---

## 4. 스키마와 row layout

### 4.1 지원 타입 (최소 구현)

```c
typedef enum {
    COL_TYPE_INT,       // 4 bytes (int32_t)
    COL_TYPE_BIGINT,    // 8 bytes (int64_t) — id에 사용
    COL_TYPE_VARCHAR    // N bytes 고정 할당 (스키마에서 N 지정)
} column_type_t;
```

### 4.2 컬럼 메타데이터

```c
typedef struct {
    char          name[32];    // 컬럼 이름 (최대 31자 + null)
    column_type_t type;
    uint16_t      size;        // 바이트 크기 (INT=4, BIGINT=8, VARCHAR(N)=N)
    uint16_t      offset;      // row 내 바이트 offset
    bool          is_system;   // true이면 시스템 컬럼 (id)
} column_meta_t;
```

### 4.3 row layout 계산

`CREATE TABLE users (id BIGINT, name VARCHAR(32), age INT)` 에 대해:

```text
id:   offset=0,  size=8   (BIGINT, 시스템 컬럼, 자동 증가)
name: offset=8,  size=32  (VARCHAR(32))
age:  offset=40, size=4   (INT)

row_size = 44 bytes
```

```c
typedef struct {
    column_meta_t columns[MAX_COLUMNS];
    uint16_t      column_count;
    uint16_t      row_size;          // sum of all column sizes
} row_layout_t;
```

### 4.4 스키마 문법

```text
CREATE TABLE <name> (
    <col_name> INT,
    <col_name> BIGINT,
    <col_name> VARCHAR(<N>)
);
```

규칙:
- `id` 컬럼은 `BIGINT`으로, 사용자가 명시하지 않아도 엔진이 자동 추가한다.
- `id` 값은 사용자가 지정하지 않고 엔진이 `next_id`를 부여한다.
- 단일 테이블만 지원한다 (테이블 이름은 저장하되, 다중 테이블 전환은 비범위).

### 4.5 serialize / deserialize

```c
// row를 byte buffer로 직렬화
void row_serialize(const row_layout_t *layout,
                   const row_value_t *values,
                   uint8_t *out_buf);

// byte buffer에서 row 복원
void row_deserialize(const row_layout_t *layout,
                     const uint8_t *buf,
                     row_value_t *out_values);
```

고정 길이이므로 `memcpy` 기반 단순 복사로 구현한다.
VARCHAR는 남는 바이트를 0으로 채운다.

---

## 5. pager

### 5.1 핵심 역할

pager = DB의 메모리 관리자.
malloc이 heap 메모리를 관리하듯, pager는 파일의 page를 관리한다.

### 5.2 I/O: pread/pwrite

```c
// page 읽기
ssize_t pager_read_page(pager_t *pager, uint32_t page_id, uint8_t *buf) {
    off_t offset = (off_t)page_id * pager->page_size;
    return pread(pager->fd, buf, pager->page_size, offset);
}

// page 쓰기
ssize_t pager_write_page(pager_t *pager, uint32_t page_id, const uint8_t *buf) {
    off_t offset = (off_t)page_id * pager->page_size;
    return pwrite(pager->fd, buf, pager->page_size, offset);
}
```

왜 pread/pwrite인가:
- offset 계산과 page read/write 의도가 명시적이다.
- file position 상태를 공유하지 않아 thread-safe하다.
- "우리가 pager를 직접 구현했다"는 설명이 분명하다.
- mmap은 커널이 page fault/eviction을 관리하므로 pager 학습 목적에 맞지 않는다.
- mmap은 flush 시점을 직접 제어하기 어렵다 (msync의 실제 writeback은 OS가 결정).
- mmap은 page fault 시 SIGBUS로 에러가 발생해 방어 코드 작성이 까다롭다.

### 5.3 page frame cache

```c
#define MAX_FRAMES 256  // 조정 가능

typedef struct {
    uint32_t page_id;
    bool     is_valid;
    bool     is_dirty;
    uint32_t pin_count;
    uint64_t used_tick;             // LRU용 (clock 방식도 가능)
    uint8_t  data[/* page_size */]; // 실제로는 동적 할당
} frame_t;

typedef struct {
    int       fd;
    uint32_t  page_size;
    frame_t   frames[MAX_FRAMES];
    uint64_t  tick;                 // 전역 접근 카운터
    // ... DB header 캐시, lock 등
} pager_t;
```

frame cache는 malloc의 free list와 같은 역할이다.
고정 크기 frame 배열을 미리 잡아두고, page를 읽으면 빈 frame에 넣는다.

### 5.4 page 할당과 반환

```c
// 새 page 할당 (malloc에 대응)
uint32_t pager_alloc_page(pager_t *pager) {
    // 1. free page list에서 빈 page 찾기
    if (pager->header.free_page_head != 0) {
        uint32_t page_id = pager->header.free_page_head;
        // free page header를 읽어 next를 따라간다
        free_page_header_t fph;
        pager_read_page(pager, page_id, (uint8_t *)&fph);
        pager->header.free_page_head = fph.next_free_page;
        return page_id;
    }
    // 2. 없으면 파일 끝에 새 page 추가 (sbrk에 대응)
    uint32_t page_id = pager->header.next_page_id;
    pager->header.next_page_id++;
    return page_id;
}

// page 반환 (free에 대응)
void pager_free_page(pager_t *pager, uint32_t page_id) {
    free_page_header_t fph = {
        .page_type = PAGE_TYPE_FREE,
        .next_free_page = pager->header.free_page_head
    };
    pager_write_page(pager, page_id, (uint8_t *)&fph);
    pager->header.free_page_head = page_id;
}
```

malloc 연결:

```text
malloc: sbrk()로 heap 확장 → free list에서 블록 찾기 → 반환
pager:  파일 끝에 page 추가 → free page list에서 page 찾기 → 반환
```

### 5.5 flush 정책

- **flush 시점: commit / 프로그램 종료 시**
- statement 단위 flush는 매 INSERT마다 pwrite + fsync가 호출되어 벤치마크에서 I/O 병목이 됨.
- commit 단위로 dirty page를 모아서 flush하면 batch write 효과를 얻는다.
- WAL이 비범위이므로 비정상 종료 시 데이터 유실 가능 (README에 명시).

```c
void pager_flush_all(pager_t *pager) {
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (pager->frames[i].is_valid && pager->frames[i].is_dirty) {
            pager_write_page(pager, pager->frames[i].page_id,
                             pager->frames[i].data);
            pager->frames[i].is_dirty = false;
        }
    }
    // header page도 flush
    pager_write_header(pager);
    fsync(pager->fd);
}
```

### 5.6 eviction 정책

최소 구현: **LRU (Least Recently Used)**

```text
1. 새 page를 읽으려는데 빈 frame이 없다
2. pin_count == 0 인 frame 중 used_tick이 가장 작은 것을 선택
3. dirty면 먼저 flush
4. 해당 frame에 새 page를 읽어온다
```

pin_count는 현재 해당 page를 사용 중인 호출자 수.
0이 아니면 evict하지 않는다.

---

## 6. slotted heap table

### 6.1 heap page 구조

```c
typedef struct {
    uint32_t page_type;          // PAGE_TYPE_HEAP
    uint32_t next_heap_page_id;  // 다음 heap page (0이면 마지막)
    uint16_t slot_count;         // 할당된 slot 수
    uint16_t free_slot_head;     // free slot chain head (0xFFFF면 없음)
    uint16_t free_space_offset;  // 다음 row payload 시작 위치
    uint16_t reserved;
} heap_page_header_t;
```

### 6.2 slot directory

```c
#define SLOT_ALIVE  0x01
#define SLOT_DEAD   0x02  // tombstone
#define SLOT_FREE   0x03  // free slot (재사용 가능)

typedef struct {
    uint16_t offset;     // page 내 row payload 시작 위치
    uint16_t status;     // SLOT_ALIVE / SLOT_DEAD / SLOT_FREE
    uint16_t next_free;  // free slot chain (SLOT_FREE일 때만 유효)
    uint16_t reserved;
} slot_t;
```

### 6.3 page 내부 배치

```text
[heap_page_header][slot_0][slot_1]...[slot_N]  ← 앞에서 뒤로 성장
                                    [free space]
[row_payload_N]...[row_payload_1][row_payload_0]  ← 뒤에서 앞으로 성장
```

slot directory는 앞에서 뒤로 자라고, row payload는 뒤에서 앞으로 자란다.
둘이 만나면 page가 꽉 찬 것이다.

### 6.4 row_ref

```c
typedef struct {
    uint32_t page_id;
    uint16_t slot_id;
} row_ref_t;
```

row_ref = 디스크 위의 주소.
메모리 포인터가 `(세그먼트, 오프셋)`이듯, row_ref는 `(page_id, slot_id)`로 파일 안의 row 위치를 가리킨다.
프로그램을 껐다 켜도 이 주소는 유효하다.

### 6.5 INSERT 흐름

```text
1. free_slot_head가 유효한 heap page를 찾는다
2. 없으면 free_space가 충분한 heap page를 찾는다
3. 없으면 새 heap page를 pager_alloc_page()로 할당한다
4. slot directory에 새 slot을 추가하거나, free slot을 재사용한다
5. row payload를 page 뒤쪽에 기록한다
6. row_ref(page_id, slot_id)를 반환한다
```

### 6.6 DELETE 흐름 (tombstone + free slot)

```text
1. row_ref로 해당 page의 slot을 찾는다
2. slot.status를 SLOT_DEAD (tombstone)로 변경한다
3. scan에서 SLOT_DEAD는 건너뛴다
4. 이후 INSERT 시: SLOT_DEAD를 SLOT_FREE로 전환하고 free slot chain에 연결
   (또는 DELETE 시점에 바로 SLOT_FREE로 전환)
5. 다음 INSERT가 free slot chain에서 빈 slot을 꺼내 재사용한다
```

최소 구현에서는 DELETE 시 바로 SLOT_FREE로 전환하고 free slot chain에 연결한다.
tombstone과 free slot의 구분이 필요한 이유: MVCC나 트랜잭션을 추가할 때 tombstone 상태가 필요하다.
이번 구현에서는 단순화를 위해 DELETE → 즉시 SLOT_FREE 경로를 취해도 된다.

### 6.7 free slot chain

```text
heap_page_header.free_slot_head → slot[3] → slot[7] → slot[1] → 0xFFFF (끝)
```

각 free slot의 `next_free` 필드가 다음 free slot의 index를 가리킨다.
malloc의 explicit free list와 동일한 구조다.

### 6.8 heap scan

```c
// 전체 heap page를 순회하며 ALIVE인 row를 반환
void heap_scan(pager_t *pager, row_layout_t *layout,
               scan_callback_fn callback, void *ctx) {
    uint32_t page_id = pager->header.first_heap_page_id;
    while (page_id != 0) {
        // page 읽기
        // slot directory 순회
        // status == SLOT_ALIVE인 slot만 callback
        // page_id = header.next_heap_page_id
    }
}
```

---

## 7. on-disk B+ tree

### 7.1 설계 원칙

- B+ tree의 노드 = 파일의 page
- 자식을 포인터가 아니라 page 번호로 가리킨다
- leaf에는 `key + row_ref`만 저장 (row 전체를 넣지 않음)
- leaf는 next_leaf_page_id로 연결 (범위 조회, 순차 순회용)
- `id` unique key 하나만 지원

### 7.2 leaf page

```c
typedef struct {
    uint32_t page_type;           // PAGE_TYPE_LEAF
    uint32_t parent_page_id;      // 부모 page (root면 0)
    uint32_t key_count;
    uint32_t next_leaf_page_id;   // 오른쪽 형제 leaf (0이면 마지막)
    uint32_t prev_leaf_page_id;   // 왼쪽 형제 leaf (borrow/merge용)
} leaf_page_header_t;

typedef struct {
    uint64_t  key;       // id 값
    row_ref_t row_ref;   // heap page 위치
} leaf_entry_t;          // 8 + 4 + 2 = 14 bytes
```

page_size=4096 기준 fan-out 계산:

```text
header = sizeof(leaf_page_header_t) = 20 bytes
entry  = sizeof(leaf_entry_t) = 14 bytes (padding 없이)
max_keys = (4096 - 20) / 14 = 291
```

### 7.3 internal page

```c
typedef struct {
    uint32_t page_type;              // PAGE_TYPE_INTERNAL
    uint32_t parent_page_id;
    uint32_t key_count;
    uint32_t leftmost_child_page_id;
} internal_page_header_t;

typedef struct {
    uint64_t key;                    // separator key
    uint32_t right_child_page_id;
} internal_entry_t;                  // 12 bytes
```

배치:

```text
[internal_header][entry_0][entry_1]...[entry_N]

children: leftmost_child, entry_0.right_child, entry_1.right_child, ...
→ key_count개 key, key_count+1개 child
```

page_size=4096 기준:

```text
header = 16 bytes
entry  = 12 bytes
max_keys = (4096 - 16) / 12 = 340
max_children = 341
```

100만 건 높이 계산:

```text
높이 1 (root만):                 341개 커버
높이 2 (root + leaf):            341 × 291 = 99,231개
높이 3 (root + internal + leaf): 341 × 341 × 291 = 28,876,371개
→ 100만 건은 높이 3이면 충분
→ id lookup = root + internal + leaf + heap = page 4개
```

### 7.4 구현할 연산

| 연산 | 설명 | 난이도 |
|------|------|--------|
| search | root → leaf 탐색, key 이진 검색 | 하 |
| insert | leaf에 entry 추가 | 중 |
| leaf split | leaf 꽉 참 → 새 page 할당, 반분 | 상 |
| internal split | internal 꽉 참 → 새 page 할당, 반분 | 상 |
| root split | root가 꽉 참 → 새 root 생성 | 상 |
| delete | leaf에서 entry 제거 | 중 |
| borrow | 형제 node에서 entry 가져오기 | 상 |
| merge | 형제 node와 합치기, 빈 page 반환 | 상 |
| root shrink | root의 자식이 1개 → 자식을 root로 승격 | 중 |
| leaf sibling 연결 | next/prev leaf page id 유지 | 중 |

### 7.5 split 상세

leaf split 예시:

```text
Before: leaf page A (꽉 참)
  [k1, k2, k3, k4, k5, k6]  (max_keys = 6이라 가정)

After:
  leaf page A: [k1, k2, k3]
  leaf page B: [k4, k5, k6]  (새 page, pager_alloc_page()로 할당)
  parent에 separator key k4 추가, right_child = page B
```

이것은 malloc에서 블록이 부족하면 새 블록을 할당하는 것과 같다.

### 7.6 merge 상세

```text
Before:
  leaf A: [k1]  (underflow)
  leaf B: [k2, k3]
  parent: ... [separator] ...

borrow 시도: B에서 k2를 A로 이동 가능 → borrow 성공

borrow 불가 시 merge:
  leaf A + leaf B → [k1, k2, k3] (한 page로 합침)
  빈 page를 pager_free_page()로 반환
  parent에서 separator 제거
```

merge 후 빈 page가 free page list로 반환된다.
이것은 malloc에서 인접 free 블록을 합치는 coalescing과 같다.

### 7.7 B+ tree minimum key 규칙 (최소 구현)

```text
min_keys = max_keys / 2  (정수 나누기)
- leaf:     291 / 2 = 145
- internal: 340 / 2 = 170

node의 key_count < min_keys → underflow → borrow 또는 merge
root는 예외: key가 0개여도 괜찮다 (빈 트리)
```

---

## 8. SQL 실행 구조

### 8.1 statement

```c
typedef enum {
    STMT_CREATE_TABLE,
    STMT_INSERT,
    STMT_SELECT,
    STMT_DELETE,
    STMT_EXPLAIN
} statement_type_t;

typedef enum {
    PREDICATE_NONE,       // SELECT * (전체)
    PREDICATE_ID_EQ,      // WHERE id = <value>
    PREDICATE_FIELD_EQ    // WHERE <field> = <value>
} predicate_kind_t;

typedef struct {
    statement_type_t  type;
    predicate_kind_t  predicate_kind;
    char              table_name[32];
    // INSERT용
    row_value_t       values[MAX_COLUMNS];
    // WHERE용
    char              pred_field[32];
    char              pred_value[64];
    uint64_t          pred_id;        // PREDICATE_ID_EQ일 때
    // CREATE TABLE용
    column_def_t      col_defs[MAX_COLUMNS];
    uint16_t          col_count;
    // SELECT용
    bool              select_all;     // SELECT * 여부
} statement_t;
```

### 8.2 plan

```c
typedef enum {
    ACCESS_PATH_TABLE_SCAN,
    ACCESS_PATH_INDEX_LOOKUP,
    ACCESS_PATH_INDEX_DELETE,
    ACCESS_PATH_INSERT,
    ACCESS_PATH_CREATE_TABLE
} access_path_t;

typedef struct {
    access_path_t access_path;
} plan_t;
```

### 8.3 planner 규칙 (rule-based)

```text
CREATE TABLE            → ACCESS_PATH_CREATE_TABLE
INSERT                  → ACCESS_PATH_INSERT
SELECT + WHERE id = ?   → ACCESS_PATH_INDEX_LOOKUP
SELECT + WHERE other = ? → ACCESS_PATH_TABLE_SCAN
SELECT + no WHERE       → ACCESS_PATH_TABLE_SCAN
DELETE + WHERE id = ?   → ACCESS_PATH_INDEX_DELETE
DELETE + WHERE other = ? → ACCESS_PATH_TABLE_SCAN (per-row delete)
EXPLAIN                 → plan 생성 후 출력만
```

### 8.4 executor 흐름

```c
result_t execute(db_t *db, statement_t *stmt) {
    plan_t plan = planner_create_plan(db, stmt);

    if (stmt->type == STMT_EXPLAIN) {
        return explain_plan(&plan);
    }

    switch (plan.access_path) {
        case ACCESS_PATH_CREATE_TABLE:
            return exec_create_table(db, stmt);
        case ACCESS_PATH_INSERT:
            return exec_insert(db, stmt);
        case ACCESS_PATH_INDEX_LOOKUP:
            return exec_index_lookup(db, stmt);
        case ACCESS_PATH_TABLE_SCAN:
            return exec_table_scan(db, stmt);
        case ACCESS_PATH_INDEX_DELETE:
            return exec_index_delete(db, stmt);
    }
}
```

### 8.5 INSERT 실행 상세

```text
1. header에서 next_id를 읽는다
2. row에 id = next_id를 채운다
3. heap_insert() 호출 → row_ref 획득
4. bptree_insert(next_id, row_ref) 호출
5. next_id++, row_count++ 갱신
6. (commit/종료 시 dirty page flush)
```

### 8.6 SELECT WHERE id = ? 실행 상세

```text
1. bptree_search(id) 호출 → row_ref 획득
2. heap_fetch(row_ref) 호출 → row bytes
3. row_deserialize() → 사용자에게 출력
```

### 8.7 DELETE WHERE id = ? 실행 상세

```text
1. bptree_search(id) → row_ref 획득
2. heap_delete(row_ref) → slot을 FREE로 전환, free slot chain 갱신
3. bptree_delete(id) → leaf에서 entry 제거
   → 필요시 borrow / merge / root shrink
   → 빈 page는 pager_free_page()로 반환
4. row_count-- 갱신
```

핵심: heap 삭제, index 삭제, 공간 재사용이 함께 움직여야 한다.

---

## 9. 공간 재사용 정책

### 9.1 3단계 확장 설계

```text
Level 1 (필수): tombstone / free slot
  - DELETE 시 slot을 DEAD/FREE로 표시
  - INSERT 시 free slot chain에서 빈 slot 재사용
  - page 내부의 공간 회수

Level 2 (필수): free page list
  - B+ tree merge 후 빈 page를 free page list에 반환
  - pager_alloc_page() 시 free page list에서 우선 재사용
  - page 단위의 공간 회수

Level 3 (비범위): VACUUM FULL
  - 전체 파일을 재배치하여 단편화 해소
  - row 이동 시 모든 row_ref와 index를 갱신해야 함
  - 구현하지 않음 (README에 한계로 명시)
```

### 9.2 공간 재사용 흐름도

```text
DELETE row
  ├── heap: slot → FREE, free_slot_chain에 추가
  └── B+ tree: leaf에서 entry 제거
        ├── key_count >= min_keys → 완료
        └── key_count < min_keys (underflow)
              ├── borrow 가능 → 형제에서 entry 이동, 완료
              └── borrow 불가 → merge
                    ├── 두 leaf 합침
                    ├── 빈 page → pager_free_page() → free page list
                    └── parent에서 separator 제거
                          └── parent underflow → 재귀적 처리

INSERT row
  ├── heap: free_slot_chain에서 빈 slot 재사용
  │         없으면 → free_space에서 새 slot
  │         없으면 → pager_alloc_page() (free page list 우선)
  └── B+ tree: leaf에 entry 추가
        ├── leaf에 공간 있음 → 완료
        └── leaf 꽉 참 → split
              └── 새 page = pager_alloc_page() (free page list 우선)
```

---

## 10. 동시성 (최소 구현)

```c
#include <pthread.h>

typedef struct {
    pthread_rwlock_t lock;
    // ...
} db_t;

// SELECT 시
pthread_rwlock_rdlock(&db->lock);
// ... 조회 ...
pthread_rwlock_unlock(&db->lock);

// INSERT / DELETE 시
pthread_rwlock_wrlock(&db->lock);
// ... 변경 ...
pthread_rwlock_unlock(&db->lock);
```

- DB 전역 RW lock: multi-reader / single-writer
- page-level latch coupling은 비범위
- 이번 구현에서는 단일 스레드 REPL이므로 lock은 구조만 잡아둔다

---

## 11. 디버그 / 관측 명령

### 11.1 `.btree`

B+ tree 구조를 트리 형태로 출력한다.

```text
minidb> .btree
B+ Tree (root: page 2, height: 3)
  [INTERNAL page=2] keys=3: 1000 2000 3000
    [INTERNAL page=5] keys=2: 500 750
      [LEAF page=10] keys=15: 1 2 3 ... 15
      [LEAF page=11] keys=14: 16 17 ... 29
      ...
    [INTERNAL page=6] keys=2: 1250 1500
      ...
```

### 11.2 `.pages`

page 할당 현황을 출력한다.

```text
minidb> .pages
Total pages: 150
  HEADER:   1
  HEAP:     100
  LEAF:     30
  INTERNAL: 10
  FREE:     9
Free page list: 140 -> 138 -> 135 -> ... -> 0
```

### 11.3 `.stats`

DB 통계를 출력한다.

```text
minidb> .stats
Rows: 50000 (live)
Next ID: 50001
Page size: 4096
Row size: 44
Rows per heap page: ~92
File size: 614400 bytes (150 pages)
Free pages: 9
```

### 11.4 `EXPLAIN`

```text
minidb> EXPLAIN SELECT * FROM users WHERE id = 500;
Access Path: INDEX_LOOKUP
  Index: B+ Tree (id)
  Target: id = 500

minidb> EXPLAIN SELECT * FROM users WHERE name = 'alice';
Access Path: TABLE_SCAN
  Filter: name = 'alice'
  Scan: all heap pages
```

---

## 12. 구현 순서 (7 Step)

### Step 1. 프로젝트 골격과 인터페이스

작업:
- `include/`, `src/`, `tests/` 디렉터리 구조 생성
- Makefile 작성 (`-Wall -Wextra -Werror -fsanitize=address,undefined`)
- 모든 모듈의 최소 헤더 파일 정의
- CLI REPL 기본 loop (readline 또는 fgets)
- statement type enum, page type enum 정의

완료 기준:
- `make` 로 빌드 성공
- REPL에서 입력을 받고 "unknown command" 출력

이해할 것:
- parser / planner / executor / storage를 분리하는 이유
- 이 골격이 이후 모든 Step의 기반

### Step 2. 스키마 → 메모리 레이아웃

작업:
- column_meta_t, row_layout_t 구현
- `CREATE TABLE` 파싱 → row_layout 계산
- row serialize/deserialize 구현
- `id` 시스템 컬럼 자동 추가

완료 기준:
- row 하나를 byte buffer로 변환하고 다시 복원 가능
- row_size와 page당 row 수 계산이 안정적

이해할 것:
- 스키마 = 메모리 배치 규칙
- 고정 길이 → page 내 위치를 산술로 계산 가능

### Step 3. pager

작업:
- `sysconf(_SC_PAGESIZE)`로 page_size 결정
- `pread`/`pwrite` 기반 page read/write 구현
- page frame cache (MAX_FRAMES) 구현
- frame metadata: page_id, is_dirty, pin_count, used_tick
- pager_alloc_page / pager_free_page 구현
- free page list (singly linked) 구현
- dirty page flush (commit/종료 시)
- DB header page 초기화/로드

완료 기준:
- 새 DB 생성 시 header page, first heap page, root index page 생성
- 재실행 후 header를 다시 읽을 수 있음
- dirty page flush와 free page 재사용 동작

이해할 것:
- pager = DB의 메모리 관리자 (malloc에 대응)
- page frame cache = malloc의 free list와 같은 역할

### Step 4. slotted heap table

작업:
- heap page header, slot directory 구현
- heap_insert: free slot 우선 재사용, 없으면 새 slot/page
- heap_fetch: row_ref → row bytes
- heap_delete: slot → FREE, free slot chain 갱신
- heap_scan: 전체 heap page 순회, ALIVE만 반환
- next_heap_page_id 체인 관리

완료 기준:
- row 1개 insert 후 바로 fetch 가능
- 여러 page에 걸친 insert 후 순차 scan 가능
- 재실행 후 동일 row 복구 가능
- delete 후 scan에서 제외
- 삭제 후 재삽입 시 free slot 재사용

이해할 것:
- row_ref = 디스크 위의 주소
- tombstone/free slot = lazy delete (malloc의 free와 같은 개념)

### Step 5. on-disk B+ tree

작업:
- leaf search (이진 검색)
- leaf insert
- leaf split → 새 page 할당
- internal insert
- internal split
- root split → 새 root 생성
- leaf sibling 연결 (next/prev)
- delete → leaf에서 entry 제거
- borrow (형제에서 가져오기)
- merge (형제와 합치기) → 빈 page 반환
- root shrink

완료 기준:
- 1, 10, 1000, 100000개 삽입 후 모든 id search 성공
- split 이후에도 오름차순 순회 가능
- delete 후에도 key 검색과 구조 유지 일관

이해할 것:
- B+ tree 노드 = 파일의 page
- fan-out이 핵심 (page 하나에 key ~290개)
- split = 새 page 할당, merge = page 반환

### Step 6. planner / executor 연결

작업:
- lexer/parser: 최소 문법 (CREATE, INSERT, SELECT, DELETE, EXPLAIN)
- statement_t 파싱
- planner: predicate_kind에 따라 access_path 선택
- executor: access_path에 따라 storage API 호출
- EXPLAIN 구현
- `.btree`, `.pages`, `.stats` 메타 명령 구현

완료 기준:
- `WHERE id = ?` → INDEX_LOOKUP 경로
- `WHERE name = ?` → TABLE_SCAN 경로
- `DELETE WHERE id = ?` → INDEX_DELETE 경로
- EXPLAIN이 access path를 정확히 출력

이해할 것:
- planner = SQL과 디스크를 연결하는 다리
- 같은 SELECT지만 읽는 page 수가 수천 배 다를 수 있음

### Step 7. 테스트, 벤치마크, 안정성

작업:
- unit test: pager, schema, heap, bptree, planner
- integration test: insert→restart→select, delete→reinsert, indexed vs non-indexed
- 방어 코드: file magic, version, page type, key count 검증
- benchmark:
  - 1,000,000건 INSERT
  - cold start id lookup (DB 재시작 후)
  - warm cache id lookup
  - warm non-id scan
  - delete + reinsertion 후 파일 크기 확인
- `.btree`, `.pages`, `.stats` 출력 검증

완료 기준:
- 모든 테스트 통과
- benchmark 스크립트 반복 실행 가능
- sanitizer 통과 (-fsanitize=address,undefined)
- README에 benchmark 숫자 기록 가능

---

## 13. 벤치마크 상세

### 13.1 측정 항목

| 항목 | 설명 |
|------|------|
| bulk insert | 1,000,000건 INSERT 총 시간 |
| cold id lookup | DB 재시작 후 임의 id 10,000회 조회 평균 |
| warm id lookup | 캐시 워밍 후 임의 id 10,000회 조회 평균 |
| warm name scan | 캐시 워밍 후 임의 name 1,000회 조회 평균 |
| delete + reinsert | 10,000건 삭제 후 10,000건 재삽입 시간 |

### 13.2 측정 규칙

- `printf` 비용은 제외한다 (출력 없이 측정)
- 최소 3회 반복 후 평균값 기록
- cold start: 프로세스 재시작 + `sync; echo 3 > /proc/sys/vm/drop_caches` (가능한 경우)
- warm cache: 동일 프로세스 내 반복 조회
- 시간 측정: `clock_gettime(CLOCK_MONOTONIC)`

### 13.3 기록할 메타데이터

```text
- OS page size (sysconf 결과)
- 실제 사용 page_size
- row_size
- total rows
- total pages
- file size (bytes)
- B+ tree height
- free page count
```

---

## 14. 검증 계획

### 14.1 unit test

| 모듈 | 테스트 항목 |
|------|------------|
| pager | 새 DB 생성, reopen, page read/write 일치, header 복구, free page alloc/free |
| schema | row layout 계산, serialize/deserialize 왕복, 타입별 크기 |
| table | 단일 insert/fetch, 다수 insert, page 넘침, tombstone delete, free slot reuse, scan 정합성 |
| bptree | 단일 insert/search, 다수 insert, leaf split, internal split, root split, delete, borrow, merge, root shrink, 순차 순회 |
| planner | WHERE id=? → INDEX_LOOKUP, WHERE name=? → TABLE_SCAN, DELETE id=? → INDEX_DELETE |

### 14.2 integration test

- INSERT 후 바로 SELECT WHERE id = ?
- 여러 건 insert 후 SELECT WHERE name = ?
- DELETE WHERE id = ? 후 동일 id 재조회 실패
- 삭제 후 재삽입 시 free slot 재사용
- 프로그램 종료 후 재실행 → SELECT WHERE id = ? 성공
- next_id가 재실행 후에도 이어서 증가

### 14.3 안정성 검증

- `-Wall -Wextra -Werror` 통과
- `-fsanitize=address,undefined` 통과
- 가능하면 `valgrind --leak-check=full` 통과

---

## 15. malloc 경험 대응표

| malloc 과제 | DB 저장 엔진 | 같은 원리 |
|-------------|-------------|-----------|
| `mm_malloc(size)` | `pager_alloc_page()` | free list에서 빈 단위 찾아 반환 |
| `mm_free(ptr)` | `pager_free_page(page_id)` | 단위를 free list에 반환 |
| explicit free list | free page list | 빈 단위를 singly linked list로 연결 |
| coalescing | B+ tree merge | 인접 블록/노드를 합쳐 공간 확보 |
| block split | B+ tree split / heap page 분할 | 꽉 찬 단위를 나누기 |
| header (size, alloc bit) | page header (type, key_count) | 단위 앞에 메타정보 배치 |
| alignment (8바이트) | page 정렬 (page_size 바이트) | 단위 크기에 맞춘 정렬 |
| fragmentation | tombstone 누적 | 삭제 후 사용 불가 공간 증가 |
| sbrk()로 heap 확장 | 파일 끝에 page 추가 | 공간 부족 시 확장 |

---

## 16. 비범위 (명시적 제외)

- UPDATE
- 트랜잭션 (BEGIN / COMMIT / ROLLBACK)
- WAL (Write-Ahead Logging)
- crash recovery
- VACUUM FULL
- 다중 인덱스 / secondary index
- multi-table 일반화
- 가변 길이 row (variable-length)
- cost-based SQL optimizer
- page-level latch coupling
- mmap 기반 I/O
- multi-column key
- duplicate key

---

## 17. 발표용 메시지

한 줄 요약:

> SQL 문장을 파싱해 실행 계획으로 바꾸고, row와 B+ 트리를 같은 .db 바이너리 파일에
> page 단위로 저장했으며, WHERE id = ? 는 디스크 기반 B+ 트리 인덱스를 통해
> 더 적은 page 접근으로 처리했습니다.

저장엔진 심화:

> 삭제는 row를 즉시 당겨 정렬하지 않고 tombstone과 free slot으로 관리했고,
> B+ 트리도 delete와 merge를 지원하도록 만들어 공간 재사용과 인덱스 정합성을
> 함께 유지했습니다.

시스템 관점:

> CSAPP 9장의 page 관점과 malloc 구현 경험을 이용해
> row 단위 malloc 대신 page frame 기반 메모리 모델을 쓰고,
> 디스크 파일도 OS page 크기 단위로 읽고 쓰도록 설계했다.

pread/pwrite 선택:

> mmap은 OS가 page fault와 eviction을 관리하므로 pager 학습 목적에 맞지 않아,
> pread/pwrite로 I/O 타이밍을 프로세스가 직접 제어하는 방식을 선택했다.

---

## 18. 최종 체크리스트

- [ ] OS page size를 sysconf로 읽어 pager에 세팅하는가
- [ ] DB header에 page_size가 저장되고 reopen 시 존중되는가
- [ ] CREATE TABLE 시 스키마 문법으로 row layout이 계산되는가
- [ ] id 자동 증가가 재실행 후 이어지는가
- [ ] heap insert/fetch/delete/reuse가 page 경계를 넘어도 안전한가
- [ ] free slot chain이 정상 동작하는가
- [ ] B+ tree split 후 search가 모두 맞는가
- [ ] B+ tree delete / merge 후에도 구조가 유지되는가
- [ ] pager_alloc_page가 free page list를 우선 사용하는가
- [ ] pager_free_page가 free page list에 올바르게 반환하는가
- [ ] commit/종료 시에만 dirty page가 flush되는가
- [ ] WHERE id = ? 와 WHERE name = ? 성능 차이가 숫자로 보이는가
- [ ] cold start와 warm cache 벤치마크가 구분되어 측정되는가
- [ ] .btree, .pages, .stats, EXPLAIN 으로 내부 구조를 설명할 수 있는가
- [ ] sanitizer 통과하는가
- [ ] 비정상 종료 시 데이터 유실 가능성이 README에 명시되어 있는가
