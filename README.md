# MiniDB — 디스크 기반 SQL 엔진

pread/pwrite 기반 pager, slotted heap table, on-disk B+ tree index, rule-based planner를 포함한
최소 SQL 엔진이다. 단일 `.db` 파일에 모든 데이터를 저장하며, 종료 시 dirty page를 일괄 flush한다.


## 실행 환경 (Dev Container)

이 프로젝트는 VS Code Dev Container(Docker)에서 실행한다.

### 사전 요구사항

- Docker Desktop 설치
- VS Code + "Dev Containers" 확장 설치

### 컨테이너 실행 방법

1. VS Code에서 프로젝트 폴더를 연다.
2. `Cmd+Shift+P` (Mac) 또는 `Ctrl+Shift+P` (Windows/Linux) → `Dev Containers: Reopen in Container` 선택.
3. Docker 이미지가 빌드되고 Ubuntu 컨테이너 안에서 터미널이 열린다.
4. 컨테이너 내부 사용자는 `jungle`이며, sudo 권한이 있다.

### 컨테이너 환경

- OS: Ubuntu (latest)
- 컴파일러: gcc (build-essential)
- 디버거: gdb, valgrind
- 시간대: Asia/Seoul
- 로케일: ko_KR.UTF-8


## 빌드

```bash
make          # 전체 빌드 (build/minidb 생성)
make clean    # 빌드 산출물 및 .db 파일 제거
```

컴파일 플래그: `-Wall -Wextra -Werror -g -fsanitize=address,undefined`

AddressSanitizer와 UndefinedBehaviorSanitizer가 기본 활성화되어 있어
메모리 오류와 정의되지 않은 동작을 런타임에 감지한다.


## 실행

```bash
make run      # sql.db 파일로 REPL 실행
```

또는 직접 DB 파일 이름을 지정할 수 있다:

```bash
./build/minidb mydb.db
```

기존 DB 파일이 있으면 자동으로 열고, 없으면 새로 생성한다.

실행하면 아래와 같은 REPL 프롬프트가 나타난다:

```
minidb> Connected to sql.db (page_size=4096)
minidb>
```


## 지원 SQL 명령어

### CREATE TABLE

테이블을 생성한다. `id BIGINT` 컬럼은 시스템이 자동 추가하므로 정의하지 않는다.

```sql
CREATE TABLE users (name VARCHAR(32), age INT);
```

지원 타입:

| 타입 | 크기 | 설명 |
|------|------|------|
| INT | 4바이트 | 32비트 정수 |
| BIGINT | 8바이트 | 64비트 정수 |
| VARCHAR(N) | N바이트 | 가변 길이 문자열 (최대 N자, 기본 32) |

제약: 현재 단일 테이블만 지원한다. 이미 테이블이 존재하면 에러를 반환한다.

### INSERT INTO

행을 삽입한다. `id`는 자동 증가하므로 값 목록에 포함하지 않는다.

```sql
INSERT INTO users VALUES ('홍길동', 25);
INSERT INTO users VALUES ('김철수', 30);
```

출력 예시: `Inserted 1 row (id=1)`

### SELECT * FROM ... WHERE

조건에 맞는 행을 조회한다.

```sql
-- id로 조회 (B+ tree INDEX_LOOKUP 사용, O(log n))
SELECT * FROM users WHERE id = 1;

-- 다른 컬럼으로 조회 (TABLE_SCAN 사용, O(n))
SELECT * FROM users WHERE name = '홍길동';

-- 전체 조회 (TABLE_SCAN)
SELECT * FROM users;
```

출력 예시:

```
id | name | age
-----------+-----------+-----------
1 | 홍길동 | 25
1 row (INDEX_LOOKUP)
```

대량 데이터에서 `id` 인덱스 조회와 일반 컬럼 스캔의 차이는 아래처럼 나타난다.

```text
minidb> select * from users where id = 100000
id | name | email | age
-----------+------------+------------+-----------
100000 | Amber Shin | user100000@test.com | 27
1행 조회 (INDEX_LOOKUP)
[debug] 소요: 2.73ms | 페이지 로드: 5 (히트: 1, 미스: 4) | 디스크 기록: 0

minidb> select * from users where email = user100000@test.com
id | name | email | age
-----------+------------+------------+-----------
100000 | Amber Shin | user100000@test.com | 27
1행 조회 (TABLE_SCAN)
[debug] 소요: 663.36ms | 페이지 로드: 20834 (히트: 0, 미스: 20834) | 디스크 기록: 0
```

즉 `WHERE id = ...`는 B+ tree를 따라 필요한 페이지 몇 개만 읽지만, `WHERE email = ...` 같은 조건은 인덱스가 없어서 heap 전체를 순차 스캔한다.

### DELETE FROM ... WHERE

조건에 맞는 행을 삭제한다. 삭제된 슬롯은 free slot chain에 연결되어 재사용된다.

```sql
-- id로 삭제 (INDEX_DELETE, O(log n))
DELETE FROM users WHERE id = 1;

-- 다른 컬럼으로 삭제 (TABLE_SCAN으로 대상을 찾은 뒤 일괄 삭제)
DELETE FROM users WHERE name = '홍길동';
```

### EXPLAIN

실행 계획을 보여준다. 실제 실행은 하지 않는다.

```sql
EXPLAIN SELECT * FROM users WHERE id = 1;
EXPLAIN DELETE FROM users WHERE name = '김철수';
```

출력 예시:

```
Access Path: INDEX_LOOKUP
  Index: B+ Tree (id)
  Target: id = 1
EXPLAIN done
```

접근 경로(Access Path) 종류:

| Access Path | 의미 | 사용 조건 |
|-------------|------|----------|
| INDEX_LOOKUP | B+ tree 인덱스 검색 | SELECT WHERE id = N |
| TABLE_SCAN | 전체 heap page 순차 탐색 | SELECT WHERE (id 외 컬럼) 또는 조건 없음 |
| INDEX_DELETE | B+ tree로 찾아 삭제 | DELETE WHERE id = N |
| INSERT | heap 삽입 + 인덱스 등록 | INSERT INTO |
| CREATE_TABLE | 스키마 등록 | CREATE TABLE |


## 메타 명령어

REPL에서 `.`으로 시작하는 디버그 명령어를 사용할 수 있다.

| 명령어 | 설명 |
|--------|------|
| `.stats` | DB 통계 정보 출력 |
| `.debug` | 쿼리 디버그 모드 ON/OFF 토글 |
| `.pages` | 페이지 유형별 개수 및 free page list 출력 |
| `.btree` | B+ tree 구조 출력 |
| `.log` | pager flush 로그 ON/OFF 토글 |
| `.flush` | 모든 dirty 페이지를 수동으로 디스크에 기록 |
| `.exit`, `.quit` | DB를 flush하고 종료 |

### .btree

B+ tree의 전체 구조를 트리 형태로 출력한다. 각 노드의 페이지 번호와 키 목록을 보여준다.

```
minidb> .btree
B+ Tree (root: page 2)
  [LEAF page=2] keys=3: 1 2 3
```

대량 삽입 후에는 internal 노드와 여러 leaf 노드가 트리 구조로 표시된다.

### .pages

데이터베이스 파일의 페이지 구성을 타입별로 보여준다. free page list도 출력한다.

```
minidb> .pages
Total pages: 4
  HEADER:   1
  HEAP:     1
  LEAF:     1
  INTERNAL: 0
  FREE:     0
```

| 페이지 타입 | 설명 |
|------------|------|
| HEADER | DB 메타데이터 (page 0, 항상 1개) |
| HEAP | 행 데이터 저장 (slotted page) |
| LEAF | B+ tree 리프 노드 (key → row_ref 매핑) |
| INTERNAL | B+ tree 내부 노드 (라우팅) |
| FREE | 삭제 후 반환된 빈 페이지 (free page list) |

### .stats

데이터베이스 통계 정보를 출력한다.

```
minidb> .stats
Rows: 3 (live)
Next ID: 4
Page size: 4096
Row size: 44
Rows per heap page: ~85
Total pages: 4
B+ Tree height: 1
Free page head: 0
```

| 항목 | 의미 |
|------|------|
| Rows | 현재 살아있는 행 수 |
| Next ID | 다음 INSERT 시 부여될 id |
| Page size | OS 페이지 크기 (sysconf 기반) |
| Row size | 행 하나의 직렬화 크기 (바이트) |
| Rows per heap page | heap 페이지당 예상 행 수 |
| Total pages | DB 파일 내 전체 페이지 수 |
| B+ Tree height | B+ tree 높이 |
| Free page head | free page list의 첫 페이지 (0이면 없음) |

### .debug

쿼리 디버그 모드를 켜거나 끈다. ON 상태에서는 각 SQL 실행 후 소요 시간, 페이지 로드 수, 캐시 히트/미스, 디스크 기록 수를 출력한다.

```
minidb> .debug
디버그 모드: ON
```

출력 예시:

```text
[debug] 소요: 2.73ms | 페이지 로드: 5 (히트: 1, 미스: 4) | 디스크 기록: 0
```

### .log

pager의 flush 로그 출력을 켜거나 끈다. dirty 페이지가 eviction되거나 flush될 때 어떤 페이지가 디스크에 기록되는지 stderr에 출력한다.

```
minidb> .log
pager 로그: ON
```

### .flush

현재 메모리 캐시에 남아 있는 모든 dirty 페이지를 즉시 디스크에 기록한다.

```
minidb> .flush
모든 dirty 페이지를 디스크에 기록했습니다.
```

### .exit / .quit

REPL을 종료한다. dirty page를 flush하고 DB 파일을 닫는다.

```
minidb> .exit
Bye.
```


## 테스트

```bash
make test     # 전체 테스트 실행 (73개 assertion)
```

테스트 바이너리는 `build/test_all`로 생성되며, 8개 테스트 스위트를 포함한다:

| 스위트 | 검증 항목 |
|--------|----------|
| test_schema | 레이아웃 계산, serialize/deserialize 왕복 |
| test_pager | DB 생성/재오픈, magic 검증, alloc/free/realloc |
| test_heap | insert/fetch/delete, free slot 재사용, scan |
| test_bptree | 단건 insert/search, 중복 거부, 2000건 bulk insert, 500건 delete |
| test_parser | CREATE TABLE, INSERT, SELECT, DELETE, EXPLAIN 파싱 |
| test_planner | 조건별 접근 경로 선택 (INDEX_LOOKUP, TABLE_SCAN, INDEX_DELETE) |
| test_persistence | 100건 삽입 → 종료 → 재오픈 → 데이터 정합성 검증 |
| test_delete_reuse | 삽입 → 삭제 → 재삽입 → 카운트 및 인덱스 정합성 |

실행 결과 예시:

```
=== MiniDB Test Suite ===

[test_schema]
[test_pager]
[test_heap]
[test_bptree]
[test_parser]
[test_planner]
[test_persistence]
[test_delete_reuse]

=== Results: 73/73 passed ===
```


## 프로젝트 구조

```
.
├── .devcontainer/          # Docker Dev Container 설정
│   ├── Dockerfile          # Ubuntu + gcc + gdb + valgrind
│   └── devcontainer.json   # VS Code 확장 및 설정
├── include/
│   ├── storage/
│   │   ├── page_format.h   # 페이지/헤더/슬롯 구조체 정의
│   │   ├── pager.h         # page frame cache API
│   │   ├── schema.h        # 행 직렬화/역직렬화
│   │   ├── table.h         # slotted heap table API
│   │   └── bptree.h        # B+ tree index API
│   └── sql/
│       ├── statement.h     # SQL 문장 구조체
│       ├── parser.h        # SQL 파서
│       ├── planner.h       # rule-based 플래너
│       └── executor.h      # 실행기
├── src/
│   ├── storage/
│   │   ├── pager.c         # pread/pwrite 기반 pager (LRU, free page list)
│   │   ├── schema.c        # 컬럼 레이아웃 계산, memcpy 기반 직렬화
│   │   ├── table.c         # slotted heap (free slot chain 재사용)
│   │   └── bptree.c        # on-disk B+ tree (split/merge/borrow)
│   ├── sql/
│   │   ├── parser.c        # 최소 SQL 파서
│   │   ├── planner.c       # 접근 경로 결정
│   │   └── executor.c      # 실행 디스패치
│   └── main.c              # REPL + 메타 명령어
├── tests/
│   └── test_all.c          # 73개 assertion, 8개 스위트
├── docs/                   # 설계 문서
├── Makefile
└── .gitignore
```


## 전체 로직 흐름

### 1. DB 열기 (pager_open)

```
main() → pager_open()
  ├── sysconf(_SC_PAGESIZE)로 OS 페이지 크기 획득
  ├── 파일 존재 여부 확인
  ├── [신규] page 0(header) + page 1(heap) + page 2(leaf root) 초기화 → fsync
  └── [기존] page 0에서 header 읽기 → magic 검증 → page_size 복원
```

pager는 MAX_FRAMES(256)개의 frame을 메모리에 할당한다. 각 frame은 page_size 바이트 버퍼다.
모든 페이지 접근은 `pager_get_page()`를 통해 frame cache를 거친다.
cache miss 시 LRU eviction으로 가장 오래된 unpinned frame을 교체하고, dirty면 디스크에 먼저 쓴다.

### 2. CREATE TABLE

```
입력: "CREATE TABLE users (name VARCHAR(32), age INT)"
  → parser: 컬럼 정의 파싱 (name, type, size)
  → executor: id BIGINT 시스템 컬럼 자동 추가
  → schema_compute_layout(): 각 컬럼 offset 계산, row_size 확정
  → header에 column_meta 저장, header_dirty = true
```

스키마 정보는 db_header_t의 columns[] 배열에 저장된다.
row_size는 모든 컬럼 size의 합이며, 이후 모든 행 직렬화의 기준이 된다.

### 3. INSERT

```
입력: "INSERT INTO users VALUES ('홍길동', 25)"
  → parser: 값 문자열 파싱
  → planner: ACCESS_PATH_INSERT 선택
  → executor:
      1. next_id 할당 (자동 증가)
      2. row_value_t[] 구성 → row_serialize() → 바이트 버퍼
      3. heap_insert(): 공간 있는 heap page 탐색 또는 신규 할당
         ├── free slot chain에 빈 슬롯 있으면 재사용
         └── 없으면 새 슬롯 추가 (앞에서 뒤로), 행 데이터는 페이지 끝에서 앞으로
      4. bptree_insert(): B+ tree에 (key=id, value=row_ref) 삽입
         ├── find_leaf()로 리프 탐색
         ├── 공간 있으면 정렬 유지하며 삽입
         └── 가득 차면 split_leaf() → promote key → insert_into_parent()
      5. row_count++, next_id++
```

heap page의 slotted 구조는 다음과 같다:

```
[header][slot_0][slot_1]...[slot_N]   ← 앞에서 뒤로 증가
                 [free space]
[row_N]...[row_1][row_0]              ← 페이지 끝에서 앞으로 증가
```

### 4. SELECT (INDEX_LOOKUP)

```
입력: "SELECT * FROM users WHERE id = 1"
  → planner: PREDICATE_ID_EQ → ACCESS_PATH_INDEX_LOOKUP
  → executor:
      1. bptree_search(key=1): B+ tree에서 row_ref 획득
         └── root → internal_child_for_key() → ... → leaf에서 binary search
      2. heap_fetch(row_ref): 해당 페이지의 슬롯에서 행 데이터 읽기
      3. row_deserialize() → 화면 출력
```

시간 복잡도: O(log n) — B+ tree 높이만큼 페이지 접근

### 5. SELECT (TABLE_SCAN)

```
입력: "SELECT * FROM users WHERE name = '홍길동'"
  → planner: PREDICATE_FIELD_EQ → ACCESS_PATH_TABLE_SCAN
  → executor:
      1. heap_scan(): 모든 heap page를 순회
      2. 각 페이지의 모든 ALIVE 슬롯에 대해:
         ├── row_deserialize()
         ├── predicate 평가 (컬럼 값 비교)
         └── 일치하면 출력
```

시간 복잡도: O(n) — 전체 행 순회

### 6. DELETE

```
입력: "DELETE FROM users WHERE id = 1"
  → planner: PREDICATE_ID_EQ → ACCESS_PATH_INDEX_DELETE
  → executor:
      1. bptree_search(key=1) → row_ref 획득
      2. heap_delete(row_ref):
         ├── slot.status = SLOT_FREE
         └── slot을 free_slot_head chain에 연결 (다음 INSERT에서 재사용)
      3. bptree_delete(key=1):
         ├── leaf에서 엔트리 제거
         ├── underflow 시 fix_leaf_after_delete():
         │   ├── 우측 형제에서 borrow 시도
         │   ├── 좌측 형제에서 borrow 시도
         │   └── merge (형제와 합병 + parent separator 제거)
         └── root shrink: root가 0 키면 유일한 자식을 새 root로
      4. row_count--
```

공간 재사용 3단계:

| 단계 | 메커니즘 | 재사용 시점 |
|------|---------|-----------|
| 1단계 | free slot chain | 같은 heap page에 INSERT 시 |
| 2단계 | free page list | merge로 빈 페이지 발생 시 pager_alloc_page()에서 재사용 |
| 3단계 | 파일 축소 없음 | VACUUM FULL 미구현 (설계 결정) |

### 7. 종료 (pager_close)

```
pager_close()
  → pager_flush_all():
      1. 모든 dirty frame을 pwrite()로 디스크에 기록
      2. header를 page 0에 기록
      3. fsync()로 OS 버퍼 강제 flush
  → frame 메모리 해제
  → fd close
```

### 페이지 레이아웃

모든 페이지는 `page_size` 바이트의 고정 크기를 가지며, 페이지 타입에 따라 내부 레이아웃이 달라진다.
핵심 구조체 정의는 `include/storage/page_format.h`에 있다.

새 DB 생성 직후의 기본 페이지 구성은 다음과 같다.

```text
page 0 : DB 헤더 페이지
page 1 : 첫 heap 페이지
page 2 : B+ tree 루트 leaf 페이지
```

#### 1. 헤더 페이지 (`page 0`)

헤더 페이지는 데이터베이스 전체 메타데이터를 저장한다.

```text
[db_header_t][남은 빈 공간]
```

주요 필드:

- `magic`: 이 파일이 MiniDB 파일인지 식별
- `version`: DB 파일 포맷 버전
- `page_size`: 페이지 크기
- `root_index_page_id`: B+ tree 루트 페이지 번호
- `first_heap_page_id`: 첫 heap 페이지 번호
- `next_page_id`: 다음 할당할 페이지 번호
- `free_page_head`: free page list의 시작
- `next_id`: 다음 자동 증가 id
- `row_count`: 현재 살아 있는 행 수
- `column_count`, `row_size`, `columns[]`: 테이블 스키마 정보

즉 header page는 "DB 전체 설명서" 역할을 한다.

#### 2. 힙 페이지 (`PAGE_TYPE_HEAP`)

힙 페이지는 실제 row 데이터를 저장한다.

```text
[heap_page_header_t][slot_0][slot_1]...[slot_N]  ← 앞에서 뒤로 증가
                   [free space]
[row_N]...[row_1][row_0]                         ← 페이지 끝에서 앞으로 증가
```

구성 요소:

- `heap_page_header_t`
  페이지 타입, 다음 heap 페이지 번호, 슬롯 개수, free slot chain 시작점 등
- `slot_t` 배열
  각 row가 페이지 안의 어느 offset에 저장되어 있는지 가리킴
- row 데이터 영역
  `row_serialize()`된 실제 행 바이트 저장

즉 heap page는 "실제 데이터 창고"다.

#### 3. 리프 페이지 (`PAGE_TYPE_LEAF`)

리프 페이지는 인덱스의 말단 노드로, `key -> row_ref` 매핑을 저장한다.

```text
[leaf_page_header_t][leaf_entry_t * N][빈 공간]
```

예시:

```text
key = 100 -> row_ref(page 11, slot 0)
key = 120 -> row_ref(page 11, slot 1)
key = 150 -> row_ref(page 12, slot 3)
```

즉 리프 페이지는 "id를 실제 row 위치로 연결하는 인덱스 결과표"다.

#### 4. 내부 페이지 (`PAGE_TYPE_INTERNAL`)

내부 페이지는 B+ tree 탐색 중간에 사용하는 라우팅 노드다.
실제 row는 저장하지 않고, 어떤 key가 어느 자식 페이지 범위로 가야 하는지만 저장한다.

```text
[internal_page_header_t][internal_entry_t * N][빈 공간]
```

예시:

```text
leftmost_child = page 2
key = 100 -> right_child = page 5
key = 200 -> right_child = page 7
```

의미:

- `key < 100` 이면 `page 2`
- `100 <= key < 200` 이면 `page 5`
- `200 <= key` 이면 `page 7`

즉 internal page는 "어느 leaf 방향으로 내려갈지 정하는 길 안내표"다.

#### 5. free 페이지 (`PAGE_TYPE_FREE`)

삭제나 merge 이후 반납된 페이지는 free page list에 연결된다.

```text
[free_page_header_t][남은 빈 공간]
```

`free_page_header_t`에는 현재 페이지가 free 상태임을 나타내는 타입과, 다음 free 페이지 번호가 저장된다.

#### 페이지 단위 관리의 장점

- 디스크 offset 계산이 단순하다
- 읽기/쓰기와 캐시 관리 단위를 일정하게 맞출 수 있다
- heap, leaf, internal처럼 페이지 역할을 분리하기 쉽다
- B+ tree 노드 하나를 페이지 하나에 대응시켜 탐색 비용을 예측하기 쉽다
- free page list를 통해 빈 페이지를 재사용하기 쉽다

#### 페이지 단위 관리의 단점

- row 하나만 필요해도 페이지 전체를 읽어야 할 수 있다
- 고정 크기 페이지라 내부 단편화가 생길 수 있다
- 작은 수정도 페이지 단위 dirty/flush 관리가 필요하다
- 슬롯, free space, split/merge 등 구현 복잡도가 올라간다
- 페이지 크기 선택이 성능과 공간 효율에 큰 영향을 준다

### 알려진 한계

- 단일 테이블만 지원한다.
- internal 노드의 borrow/merge는 미구현이다 (leaf만 처리). 대량 삭제 시 internal 노드가 underfull 상태로 남을 수 있으나 정합성에는 영향 없다.
- VARCHAR 비교는 바이트 단위 strcmp이다. UTF-8 collation은 지원하지 않는다.
- 동시성 제어(lock/MVCC)는 없다. 단일 클라이언트 전용이다.
- WAL(Write-Ahead Log)이 없으므로 flush 전 비정상 종료 시 데이터가 유실될 수 있다.


## 설계 결정 요약

| 항목 | 결정 | 이유 |
|------|------|------|
| 디스크 I/O | pread/pwrite | flush 시점 직접 제어, pager 학습 |
| 페이지 크기 | sysconf(_SC_PAGESIZE) | OS 최적 크기 동적 로드 |
| 공간 재사용 | tombstone + free slot + free page list | 3단계 공간 회수 |
| 인덱스 | on-disk B+ tree | id 컬럼 자동 인덱싱 |
| 플래너 | rule-based | id 조건 → INDEX, 그 외 → SCAN |
| 저장 형식 | 단일 .db 파일 | 배포 단순화 |
| flush 정책 | 종료/커밋 시 일괄 flush | fsync 호출 최소화 |
