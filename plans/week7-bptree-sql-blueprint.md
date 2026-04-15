# Week7 B+ Tree SQL Blueprint

## 목표

지난 주차 코드를 가져오지 않고 처음부터 다시 구현하되,
`입력 -> 파싱 -> 실행 -> 저장소` 경계는 명확하게 유지한다.

이번 주 기본 목표는 `Package 2. 저장엔진 심화형` 이다.
즉 단순한 insert/select 데모가 아니라,
`pager + slotted heap + B+ tree + delete/reuse + 방어 코드` 까지
설명 가능한 수준으로 만드는 것이 목표다.

최종 데모 기준은 아래와 같다.

- `INSERT` 시 `id` 자동 증가
- row는 `.db` 파일의 heap page에 저장
- `id -> row_ref` 는 B+ tree leaf에 저장
- `SELECT ... WHERE id = ?` 는 인덱스 사용
- `SELECT ... WHERE name = ?` 는 heap scan 사용
- `DELETE` 후 tombstone과 free slot/page 재사용이 동작
- `EXPLAIN`, `.btree`, `.pages` 로 내부 구조를 설명 가능
- 재실행 후에도 데이터와 `next_id` 가 유지
- 100만 건 이상 insert 후 `id lookup` 과 `non-id scan` 성능 비교

## 현재 상태 요약

- 현재 week7 저장소는 구현 코드보다 문서 중심이다.
- 따라서 이번 주는 기준 코드를 가져오는 방식이 아니라
  프로젝트 골격부터 새로 만들고 필요한 계층을 직접 정의해야 한다.
- 이번 주 핵심은 SQL breadth보다 storage depth다.

## 권장 아키텍처

```text
input
  -> lexer/parser
  -> statement AST
  -> planner
  -> executor
  -> storage engine
      -> schema layout
      -> pager
      -> slotted heap table
      -> b+ tree
      -> allocator / free lists
      -> database.db
```

핵심 규칙은 아래 다섯 가지다.

1. parser는 storage를 직접 호출하지 않는다.
2. planner가 `WHERE id = ?` 인 경우 `INDEX_LOOKUP` 또는 `INDEX_DELETE` 를 선택한다.
3. row는 heap page에 저장하고, B+ tree leaf에는 `key + row_ref` 만 저장한다.
4. row별 `malloc()` 을 피하고 page 중심 메모리 모델을 유지한다.
5. delete는 file compaction이 아니라 tombstone + free list 재사용으로 처리한다.

## 데이터 모델

### 1. DB header page

```c
typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t page_size;
    uint32_t root_index_page_id;
    uint32_t first_heap_page_id;
    uint32_t next_page_id;
    uint32_t free_page_head;
    uint64_t next_id;
    uint64_t row_count;
} db_header_t;
```

### 2. row_ref

```c
typedef struct {
    uint32_t page_id;
    uint16_t slot_id;
} row_ref_t;
```

### 3. heap page

```c
typedef struct {
    uint32_t page_type;
    uint16_t slot_count;
    uint16_t free_slot_count;
    uint16_t free_space_offset;
    uint16_t reserved;
} heap_page_header_t;
```

### 4. B+ tree leaf entry

```c
typedef struct {
    uint64_t key;
    row_ref_t row_ref;
} leaf_entry_t;
```

## 구현 순서

## Step 1. 프로젝트 골격과 인터페이스 만들기

작업:

- `include/`, `src/`, `tests/` 구조를 만든다.
- lexer/parser, planner, executor, pager, table, bptree의 최소 인터페이스를 정의한다.
- 단일 테이블 기준 CLI와 statement 타입을 정한다.

완료 기준:

- week7 저장소에서 새 코드베이스가 바로 빌드된다.
- `CREATE TABLE`, `INSERT`, `SELECT`, `DELETE`, `EXPLAIN` 용 statement 골격이 있다.

주의:

- 이 단계에서는 저장 형식과 API 경계를 먼저 고정한다.
- 과거 구현물을 베이스라인으로 두지 않는다.

## Step 2. 스키마를 메모리 레이아웃으로 바꾸기

작업:

- 컬럼 메타데이터에서 fixed-size row layout을 계산한다.
- 각 컬럼의 offset, width, type을 계산해 `row_layout_t` 로 보관한다.
- `id` 는 사용자 입력이 아니라 엔진이 자동으로 채우는 시스템 컬럼으로 취급한다.

완료 기준:

- 임의 row 하나를 byte buffer로 serialize/deserialize 할 수 있다.
- `row_size` 와 page 내 저장 가능 row 수 계산이 안정적이다.

핵심 이해 포인트:

- 이번 주 과제는 SQL 문자열 처리보다 row layout 계산이 더 중요하다.
- 스키마가 곧 메모리 배치 규칙이어야 이후 page offset 계산이 단순해진다.

## Step 3. pager 만들기

작업:

- `PAGE_SIZE = 4096` 으로 고정한다.
- `pread()` / `pwrite()` 기반 page read/write 를 구현한다.
- 메모리에는 고정 개수 page frame cache를 둔다.
- frame metadata는 `page_id`, `is_dirty`, `pin_count`, `used_tick` 정도면 충분하다.
- free page list를 통해 빈 page 재사용 경로를 만든다.

완료 기준:

- 새 DB 생성 시 header page, first heap page, root index page를 만든다.
- 재실행 후 header를 다시 읽을 수 있다.
- dirty page flush와 free page 재사용이 된다.

핵심 이해 포인트:

- CSAPP 9장 관점에서는 "DB도 page 단위"라는 점이 제일 중요하다.
- malloc 과제를 연결하려면 row마다 동적할당하지 말고 frame 배열을 직접 관리하는 쪽이 더 교육적이다.

## Step 4. slotted heap table 만들기

작업:

- slotted heap page를 구현한다.
- row insert 시 free slot이 있는 page를 우선 찾고 없으면 새 page를 할당한다.
- insert 결과로 `row_ref_t` 를 반환한다.
- row fetch는 `row_ref_t` 로 page와 slot을 바로 계산해 읽는다.
- `DELETE` 시 tombstone 처리와 free slot 재사용을 구현한다.

완료 기준:

- row 1개 insert 후 바로 fetch 가능
- 여러 page에 걸친 insert 후 순차 scan 가능
- 재실행 후 동일 row 복구 가능
- delete 후 scan에서 제외된다
- 삭제 후 재삽입 시 free slot이 재사용된다

## Step 5. on-disk B+ tree 만들기

작업:

- leaf search
- leaf insert
- leaf split
- internal insert
- internal split
- root split
- leaf sibling 연결
- delete
- borrow / merge
- root shrink

완료 기준:

- 1, 10, 1000, 100000개 삽입 후 모든 `id` search 성공
- split 이후에도 오름차순 순회 가능
- delete 후에도 key 검색과 구조 유지가 일관된다

주의:

- `id` 는 unique key 하나만 지원한다.
- secondary index, multi-column key, page-level latch는 제외한다.

## Step 6. planner/executor 연결

작업:

- `statement_type` 을 `CREATE`, `INSERT`, `SELECT`, `DELETE`, `EXPLAIN` 로 분리한다.
- `predicate_kind` 를 `NONE`, `ID_EQ`, `FIELD_EQ` 로 분리한다.
- planner에서 `WHERE id = ?` 는 `ACCESS_PATH_INDEX_LOOKUP`
- `DELETE WHERE id = ?` 는 `ACCESS_PATH_INDEX_DELETE`
- 그 외 조건은 `ACCESS_PATH_TABLE_SCAN`
- executor에서 access path에 따라 `bptree_search()`, `bptree_delete()`, `heap_scan()` 을 실행한다.

완료 기준:

- 같은 `SELECT` 문이라도 조건에 따라 다른 실행 경로를 탄다.
- `WHERE id = ?` 는 전체 scan 없이 결과를 반환한다.
- `DELETE WHERE id = ?` 는 heap과 index를 함께 갱신한다.
- `EXPLAIN` 이 `INDEX_LOOKUP`, `TABLE_SCAN`, `INDEX_DELETE` 를 보여준다.

## Step 7. 안정성, 동시성, 테스트, 벤치마크

작업:

- unit test: pager, row layout, heap insert/fetch/delete, bptree split/search/delete, planner
- integration test: insert -> restart -> select, indexed select, non-indexed select, delete -> reinsertion
- file magic/version, page type, key count 등 방어 코드 추가
- `DB` 전역 `RW lock` 기반 multi-reader / single-writer 동시성 추가
- benchmark: 1,000,000 insert, warm id lookup, warm non-id scan

완료 기준:

- 기능 테스트가 통과한다.
- benchmark 스크립트가 반복 실행 가능하다.
- README에 숫자를 남길 수 있다.
- `.btree`, `.pages`, `EXPLAIN` 으로 내부 상태를 설명할 수 있다.

벤치마크 규칙:

- `printf` 비용은 제외한다.
- 최소 3회 반복 후 평균값을 기록한다.
- 가능하면 cold/warm cache를 구분한다.

## 팀 분업 추천

### Track A. 저장소 코어

- pager
- db header
- heap page
- free slot / free page reuse
- flush/persistence

### Track B. 인덱스 코어

- leaf/internal page format
- search/insert/split/delete
- merge/root shrink

### Track C. SQL 연결

- statement 확장
- planner 추가
- executor에서 access path 분기
- `EXPLAIN`, `.btree`, `.pages`, `.stats`

### Track D. 검증과 안전장치

- unit/integration test
- sanitizer 설정
- corruption detection
- benchmark와 README 정리

병렬화 규칙:

- A와 B는 page format 합의 후 병렬 진행 가능
- C는 A의 heap fetch/delete 시그니처가 정해지면 바로 시작 가능
- D는 각 트랙의 최소 API가 나오면 바로 병렬 진행 가능

## 이번 주 비기능 목표

- row별 동적할당 최소화
- page 단위 locality 확보
- 재실행 가능성 보장
- 삭제와 공간 재사용 경로 설명 가능
- 방어 코드와 관측 도구 확보
- README만으로 데모 설명 가능

## 발표에서 강조할 메시지

한 줄 요약:

`우리는 SQL 문장을 파싱해 실행 계획으로 바꾸고, row와 B+ 트리를 같은 .db 바이너리 파일에 page 단위로 저장했으며, WHERE id = ? 는 디스크 기반 B+ 트리 인덱스를 통해 더 적은 page 접근으로 처리했습니다.`

심화 메시지:

`삭제는 row를 즉시 당겨 정렬하지 않고 tombstone과 free list로 관리했고, B+ 트리도 delete와 merge를 지원하도록 만들어 공간 재사용과 인덱스 정합성을 함께 유지했습니다.`

시스템 관점 요약:

`CSAPP 9장의 page 관점과 malloc 구현 경험을 이용해 row 단위 malloc 대신 page frame 기반 메모리 모델을 쓰고, 디스크 파일도 4KB page 단위로 읽고 쓰도록 설계했다.`

## 비범위

- update
- transaction
- WAL
- crash recovery
- multi-index
- multi-table 일반화
- variable-length row
- page-level latch coupling
- `VACUUM` 실제 구현

## 마지막 체크리스트

- 이전 주차 코드를 가져오지 않고 새 코드베이스로 빌드되는가
- `id` 자동 증가가 재실행 후 이어지는가
- heap insert/fetch/delete/reuse가 page 경계를 넘어도 안전한가
- B+ tree split 후 search가 모두 맞는가
- B+ tree delete / merge 후에도 구조가 유지되는가
- `WHERE id = ?` 와 `WHERE name = ?` 성능 차이가 숫자로 보이는가
- `.btree`, `.pages`, `EXPLAIN` 으로 내부 구조를 설명할 수 있는가
