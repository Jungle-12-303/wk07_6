# Week7 B+ Tree SQL Blueprint

## 목표

저장 엔진은 처음부터 새로 만들고,
parser는 6주차 구조를 참고해 이번 과제에 필요한 최소 문법으로 간소화한다.
`입력 -> 파싱 -> 실행 -> 저장소` 경계는 명확하게 유지한다.

이번 주 기본 목표는 `Package 2. 저장엔진 심화형` 이다.
즉 단순한 insert/select 데모가 아니라,
`pager + slotted heap + B+ tree + delete/reuse + 방어 코드` 까지
설명 가능한 수준으로 만드는 것이 목표다.

## 작업 방식

- 팀 분업이 아니라, 1인 또는 전원이 같은 프롬프트로 전체를 함께 만든다.
- 목표는 "분업으로 빠르게 완성"이 아니라 "전 과정을 이해하며 구현"이다.
- AI로 코드를 생성하더라도, 생성된 코드의 핵심 로직은 반드시 이해하고 설명할 수 있어야 한다.
- 각 Step을 순서대로 밟되, 이전 Step이 안정화된 뒤 다음으로 넘어간다.

## 최종 데모 기준

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
- 따라서 프로젝트 골격부터 새로 만들고 필요한 계층을 직접 정의해야 한다.
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
      -> pager (page frame cache)
      -> slotted heap table
      -> b+ tree (on-disk, page 기반)
      -> page allocator (free page list)
      -> database.db
```

핵심 규칙은 아래 여섯 가지다.

1. parser는 storage를 직접 호출하지 않는다.
2. planner가 `WHERE id = ?` 인 경우 `INDEX_LOOKUP` 또는 `INDEX_DELETE` 를 선택한다.
3. row는 heap page에 저장하고, B+ tree leaf에는 `key + row_ref` 만 저장한다.
4. row별 `malloc()` 을 피하고 page 중심 메모리 모델을 유지한다.
5. delete는 file compaction이 아니라 tombstone + free list 재사용으로 처리한다.
6. B+ tree의 노드 = 파일의 page. 자식을 포인터가 아니라 page 번호로 가리킨다.

## 이번 과제의 핵심 개념

### DB의 성능 = 파일을 몇 번 읽느냐

100만 건 기준:

- Table Scan: heap page 약 33,334개 전부 읽기
- B+ Tree: root → internal → leaf → heap = page 4개

이 차이가 B+ Tree를 쓰는 이유의 전부다.
상세 설명: [why-bptree-and-disk-pages.md](../docs/sql/why-bptree-and-disk-pages.md)

### malloc 경험이 직접 적용되는 지점

| malloc 과제 | 이번 DB 과제 | 연결 |
|-------------|-------------|------|
| `mm_malloc()` | `pager_alloc_page()` | free list에서 빈 블록/page 찾기 |
| `mm_free()` | `pager_free_page()` | 블록/page를 free list에 반환 |
| explicit free list | free page list | 다음 빈 블록/page를 체인으로 관리 |
| coalescing | B+ tree merge | 인접 블록/노드를 합치기 |
| split block | B+ tree split / heap page split | 꽉 찬 블록/page를 나누기 |
| header metadata | page header | 블록/page 앞에 메타정보 배치 |
| alignment (8바이트) | page 정렬 (4096바이트) | 단위 크기에 맞춘 정렬 |

이 표를 발표에서 보여주면 "malloc 경험을 DB에 적용했다"는 메시지가 분명해진다.

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
- Makefile 작성 (`-Wall -Wextra -Werror -fsanitize=address,undefined`)
- lexer/parser, planner, executor, pager, table, bptree의 최소 인터페이스(헤더)를 정의한다.
- 단일 테이블 기준 CLI와 statement 타입을 정한다.

완료 기준:

- 새 코드베이스가 바로 빌드된다.
- `CREATE TABLE`, `INSERT`, `SELECT`, `DELETE`, `EXPLAIN` 용 statement 골격이 있다.

이 단계에서 이해할 것:

- 왜 parser / planner / executor / storage를 분리하는가?
  parser가 곧바로 pager를 부르면 나중에 인덱스 경로를 추가할 수 없다.
- 이 골격이 이후 모든 Step의 기반이 된다.

## Step 2. 스키마를 메모리 레이아웃으로 바꾸기

작업:

- 컬럼 메타데이터에서 fixed-size row layout을 계산한다.
- 각 컬럼의 offset, width, type을 계산해 `row_layout_t` 로 보관한다.
- `id` 는 사용자 입력이 아니라 엔진이 자동으로 채우는 시스템 컬럼으로 취급한다.

완료 기준:

- 임의 row 하나를 byte buffer로 serialize/deserialize 할 수 있다.
- `row_size` 와 page 내 저장 가능 row 수 계산이 안정적이다.

이 단계에서 이해할 것:

- 스키마 = 메모리 배치 규칙이다.
  `CREATE TABLE users (id INT, name VARCHAR(32), age INT)` 이면
  `id: offset 0, 8바이트 | name: offset 8, 32바이트 | age: offset 40, 4바이트`
  → `row_size = 44바이트`.
- 고정 길이로 잡으면 page 안에서 row 위치를 산술로 계산할 수 있다.
- 이것이 CSAPP에서 배운 "메모리 = 연속된 바이트 배열"의 직접 적용이다.

## Step 3. pager 만들기

작업:

- page size는 `sysconf(_SC_PAGESIZE)`로 OS에서 동적으로 가져온다 (보통 4096).
- `pread()` / `pwrite()` 기반 page read/write 를 구현한다 (mmap 아님).
- 메모리에는 고정 개수 page frame cache를 둔다.
- frame metadata는 `page_id`, `is_dirty`, `pin_count`, `used_tick` 정도면 충분하다.
- free page list를 통해 빈 page 재사용 경로를 만든다.

완료 기준:

- 새 DB 생성 시 header page, first heap page, root index page를 만든다.
- 재실행 후 header를 다시 읽을 수 있다.
- dirty page flush와 free page 재사용이 된다.

이 단계에서 이해할 것:

- **pager = DB의 메모리 관리자다.** malloc이 heap 메모리를 관리하듯,
  pager는 파일의 page를 관리한다.
- page frame cache는 malloc의 free list와 같은 역할이다.
  고정 크기 frame 배열을 미리 잡아두고, page를 읽으면 빈 frame에 넣는다.
- `pager_alloc_page()` = `malloc()`: free page list에서 빈 page를 찾고,
  없으면 파일 끝에 새 page를 추가한다.
- `pager_free_page()` = `free()`: page를 free page list에 반환한다.
- dirty flag = "이 page는 수정되었으니 파일에 다시 써야 한다"는 표시.
- 이 pager가 heap table과 B+ tree 모두의 I/O를 담당한다.

malloc 연결:

```
malloc: sbrk()로 heap 확장 → free list에서 블록 찾기 → 반환
pager:  파일 끝에 page 추가 → free page list에서 page 찾기 → 반환
```

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

이 단계에서 이해할 것:

- **row_ref = 디스크 위의 주소다.**
  메모리 포인터가 `(세그먼트, 오프셋)` 이듯,
  row_ref는 `(page_id, slot_id)` 로 파일 안의 row 위치를 가리킨다.
  프로그램을 껐다 켜도 이 주소는 유효하다.
- **tombstone = lazy delete다.**
  malloc에서 free하면 즉시 메모리를 OS에 반환하지 않듯,
  DB에서도 delete하면 즉시 row를 지우지 않고 "삭제됨" 표시만 한다.
  나중에 insert가 오면 그 자리를 재사용한다.
- slotted page 안에서 free slot을 찾는 것은
  malloc에서 free list를 순회해 빈 블록을 찾는 것과 같다.

## Step 5. on-disk B+ tree 만들기

작업:

- leaf search
- leaf insert
- leaf split
- internal insert
- internal split
- root split
- leaf sibling 연결 (next_leaf_page_id)
- delete
- borrow / merge
- root shrink

완료 기준:

- 1, 10, 1000, 100000개 삽입 후 모든 `id` search 성공
- split 이후에도 오름차순 순회 가능
- delete 후에도 key 검색과 구조 유지가 일관된다

이 단계에서 이해할 것:

- **B+ tree의 노드 = 파일의 page다.**
  메모리 기반 트리는 `node->children[i]` (포인터)로 자식을 찾지만,
  디스크 기반 트리는 `pread(fd, buf, 4096, child_page_id * 4096)` 으로 자식을 찾는다.
- **fan-out이 핵심이다.**
  page 하나에 key가 약 290개 들어가므로,
  100만 건도 높이 3으로 커버된다.
  BST는 높이 20 → page 20개, B+ tree는 높이 3 → page 3개.
- **split = 새 page 할당이다.**
  leaf가 꽉 차면 pager에서 새 page를 받아 반쪽을 옮긴다.
  이것은 malloc에서 블록이 부족하면 새 블록을 할당하는 것과 같다.
- **merge = page 반환이다.**
  delete 후 node가 너무 비면 형제와 합치고,
  빈 page를 free page list에 반환한다.
  이것은 malloc에서 인접 free 블록을 합치는 coalescing과 같다.
- 상세 개념: [why-bptree-and-disk-pages.md](../docs/sql/why-bptree-and-disk-pages.md)

주의:

- `id` 는 unique key 하나만 지원한다.
- secondary index, multi-column key, page-level latch는 제외한다.

## Step 6. planner/executor 연결

작업:

- parser: 6주차 구조를 참고해 최소 문법으로 간소화한다.
  parser에 시간을 과도하게 쓰지 않는다.
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

이 단계에서 이해할 것:

- **planner가 SQL과 디스크를 연결하는 다리다.**
  `WHERE id = 500000` → planner가 "id에 인덱스 있다" 판단 →
  `INDEX_LOOKUP` 선택 → B+ tree search → page 4개만 읽기.
  `WHERE name = 'alice'` → planner가 "name에 인덱스 없다" 판단 →
  `TABLE_SCAN` 선택 → heap page 33,334개 전부 읽기.
- 같은 `SELECT` 문이지만 읽는 page 수가 8,000배 다르다.
  이 차이가 인덱스의 본질이다.

## Step 7. 테스트, 벤치마크, 안정성

작업:

- unit test: pager, row layout, heap insert/fetch/delete, bptree split/search/delete, planner
- integration test: insert → restart → select, indexed select, non-indexed select, delete → reinsertion
- file magic/version, page type, key count 등 방어 코드 추가
- benchmark: 1,000,000 insert, warm id lookup, warm non-id scan
- `.btree`, `.pages`, `.stats` 메타 명령 구현

완료 기준:

- 기능 테스트가 통과한다.
- benchmark 스크립트가 반복 실행 가능하다.
- README에 숫자를 남길 수 있다.
- `.btree`, `.pages`, `EXPLAIN` 으로 내부 상태를 설명할 수 있다.

벤치마크 규칙:

- `printf` 비용은 제외한다.
- 최소 3회 반복 후 평균값을 기록한다.
- 가능하면 cold/warm cache를 구분한다.

이 단계에서 확인할 것:

- sanitizer (`-fsanitize=address,undefined`) 통과 여부
- 가능하면 `valgrind` leak check
- "AI가 생성한 코드도 검증했다"는 발표 근거를 만든다.

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

malloc 연결 메시지:

`pager의 page 할당/반환은 malloc/free와 같은 구조이고, B+ tree의 split/merge는 블록 분할/합치기와 같은 구조다. malloc lab에서 배운 메모리 관리 원리를 디스크 저장 엔진에 그대로 적용했다.`

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

- 저장 엔진이 새 코드베이스로 빌드되는가
- `id` 자동 증가가 재실행 후 이어지는가
- heap insert/fetch/delete/reuse가 page 경계를 넘어도 안전한가
- B+ tree split 후 search가 모두 맞는가
- B+ tree delete / merge 후에도 구조가 유지되는가
- `WHERE id = ?` 와 `WHERE name = ?` 성능 차이가 숫자로 보이는가
- `.btree`, `.pages`, `EXPLAIN` 으로 내부 구조를 설명할 수 있는가
- 각 Step의 "이 단계에서 이해할 것"을 실제로 설명할 수 있는가
