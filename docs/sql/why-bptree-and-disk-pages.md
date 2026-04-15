# B+ Tree는 왜 디스크와 page에 연결되는가

## 0. 이 문서의 목적

이 문서는 아래 질문에 답하기 위해 만든다.

- 메모리 기반 B+ Tree와 디스크 기반 B+ Tree는 무엇이 다른가?
- B+ Tree가 디스크 page와 어떻게 연결되는가?
- 왜 이 조합이 좋은가?
- 실제 DBMS는 이 둘을 어떻게 쓰는가?

이 개념을 이해하지 않으면, B+ Tree를 구현해도
"왜 이걸 쓰는지" 설명할 수 없다.

## 1. 먼저, 문제를 정의한다

### 1.1 데이터가 100만 건이면 무슨 일이 생기는가

row 하나가 128바이트라고 하자.

```
100만 건 × 128바이트 = 약 128MB
```

이걸 전부 메모리에 올리면?
지금은 괜찮을 수 있다.
하지만 1000만 건이면 1.28GB, 1억 건이면 12.8GB다.
실제 DB는 메모리보다 데이터가 훨씬 크다.

그래서 DB는 데이터를 **파일**에 저장하고,
필요한 부분만 메모리에 올려서 쓴다.

### 1.2 파일에서 데이터를 찾는 비용

파일에서 특정 row를 찾으려면 파일을 읽어야 한다.
디스크 읽기는 메모리 접근보다 **수만~수십만 배** 느리다.

```
메모리 접근: ~100ns (나노초)
SSD 랜덤 읽기: ~100,000ns (0.1ms)
HDD 랜덤 읽기: ~10,000,000ns (10ms)
```

따라서 DB의 성능은 **파일을 몇 번 읽느냐**로 결정된다.
CPU 연산 횟수가 아니다.

## 2. 파일을 page 단위로 다루는 이유

### 2.1 OS가 이미 page 단위로 일한다

CSAPP 9장에서 배운 것:
OS는 메모리를 4KB page 단위로 관리한다.
파일을 읽을 때도 커널은 page cache를 통해 4KB 단위로 읽고 쓴다.

즉 1바이트를 읽으라고 해도, OS는 내부적으로 4KB를 읽는다.
그렇다면 DB도 처음부터 4KB 단위로 읽고 쓰는 게 자연스럽다.

### 2.2 page = 파일의 최소 읽기 단위

DB에서 page란 이것이다:

```
database.db 파일을 4096바이트씩 자른 조각
```

```
page 0: 오프셋 0 ~ 4095
page 1: 오프셋 4096 ~ 8191
page 2: 오프셋 8192 ~ 12287
...
page N: 오프셋 N*4096 ~ (N+1)*4096-1
```

특정 page를 읽으려면:

```c
pread(fd, buffer, 4096, page_id * 4096);
```

이 한 줄이면 된다. 파일 전체를 읽을 필요가 없다.

### 2.3 핵심 원칙

```
DB의 성능 = 몇 개의 page를 읽느냐
```

page를 적게 읽을수록 빠르다.
이 원칙이 B+ Tree를 이해하는 열쇠다.

## 3. 인덱스가 없으면 무슨 일이 생기는가

### 3.1 Table Scan (전체 탐색)

`SELECT * FROM users WHERE id = 500000`

인덱스가 없으면 방법이 하나뿐이다:
첫 번째 heap page부터 마지막까지 **전부** 읽으면서 id를 비교한다.

```
page 크기: 4096바이트
row 크기: 128바이트
page 하나에 들어가는 row 수: 약 30개 (헤더 제외)

100만 건이면 필요한 heap page 수: 약 33,334개
```

최악의 경우 33,334개의 page를 전부 읽어야 한다.
SSD 기준으로 약 3.3초, HDD 기준으로 5분 이상 걸릴 수 있다.

### 3.2 왜 정렬된 배열로는 안 되는가

"그러면 row를 id 순서로 정렬해서 저장하고 이진 탐색하면 되지 않나?"

이진 탐색 자체는 좋다. 하지만 문제는 **삽입**이다.

id 500인 row를 중간에 넣으려면?
뒤에 있는 row를 전부 한 칸씩 밀어야 한다.
100만 건이면 50만 건을 이동해야 한다.
이건 page 수만 개를 다시 써야 한다는 뜻이다.

```
정렬된 배열:
- 검색: O(log N) page 읽기 → 좋다
- 삽입: O(N) page 쓰기 → 치명적으로 느리다
- 삭제: O(N) page 쓰기 → 치명적으로 느리다
```

검색도 빠르고 삽입도 빠른 구조가 필요하다.
그것이 B+ Tree다.

## 4. B+ Tree가 해결하는 문제

### 4.1 B+ Tree의 핵심 아이디어

B+ Tree는 이런 질문에 답하는 구조다:

```
"id = 500000인 row가 파일의 어디에 있는가?"
```

답을 찾기 위해 읽어야 하는 page 수를 **극적으로 줄여준다.**

### 4.2 구체적인 숫자로 보기

page 하나가 4096바이트라고 하자.

**leaf page (B+ Tree의 맨 아래 층):**

```c
typedef struct {
    uint64_t key;       // 8바이트 (id 값)
    uint32_t page_id;   // 4바이트 (row가 있는 heap page 번호)
    uint16_t slot_id;   // 2바이트 (page 안에서 몇 번째 slot)
} leaf_entry_t;         // = 14바이트
```

leaf page 헤더가 약 24바이트라면,
하나의 leaf page에 들어가는 entry 수:

```
(4096 - 24) / 14 = 약 290개
```

**internal page (B+ Tree의 중간 층):**

```c
typedef struct {
    uint64_t key;                  // 8바이트
    uint32_t right_child_page_id;  // 4바이트
} internal_entry_t;                // = 12바이트
```

leftmost_child까지 포함하면,
하나의 internal page가 가리킬 수 있는 자식 수:

```
(4096 - 28) / 12 + 1 = 약 340개
```

이 숫자를 **fan-out**이라 부른다.
fan-out이 클수록 트리가 낮아진다.

### 4.3 100만 건에서 트리 높이 계산

```
높이 1 (root만): 340개 커버
높이 2 (root + leaf): 340 × 290 = 98,600개 커버
높이 3 (root + internal + leaf): 340 × 340 × 290 = 약 33,524,000개 커버
```

**100만 건이면 높이 3이면 충분하다.**

즉, id = 500000을 찾기 위해 읽는 page 수:

```
root page 1개 + internal page 1개 + leaf page 1개 + heap page 1개 = 4개
```

비교:

```
Table Scan:  33,334개 page 읽기
B+ Tree:     4개 page 읽기
```

**약 8,000배 차이**다.

## 5. 메모리 기반 vs 디스크 기반 B+ Tree

### 5.1 메모리 기반 B+ Tree

```c
// 노드를 malloc으로 할당
typedef struct BPNode {
    bool is_leaf;
    int key_count;
    uint64_t keys[ORDER - 1];

    // 내부 노드: 자식 포인터
    struct BPNode *children[ORDER];

    // 리프 노드: 데이터 포인터
    void *values[ORDER - 1];

    // 리프 연결
    struct BPNode *next;
} BPNode;

// 검색
BPNode *node = root;  // 메모리 포인터로 바로 접근
while (!node->is_leaf) {
    int i = find_child_index(node, key);
    node = node->children[i];  // 포인터 따라가기 (매우 빠름)
}
```

특징:
- 노드마다 `malloc()`으로 할당
- 자식을 찾을 때 포인터를 따라감
- 매우 빠르다 (나노초 단위)
- **프로그램 종료하면 전부 사라진다**
- 메모리에 전부 올라가야 한다

### 5.2 디스크 기반 B+ Tree

```c
// 노드 = 파일의 page
// "노드를 읽는다" = "파일에서 page를 읽는다"

// leaf page를 파일에서 읽는 모습
uint8_t page_buf[PAGE_SIZE];
pread(fd, page_buf, PAGE_SIZE, leaf_page_id * PAGE_SIZE);

// page_buf 안에 header와 entry가 직렬화되어 있다
leaf_page_header_t *header = (leaf_page_header_t *)page_buf;
leaf_entry_t *entries = (leaf_entry_t *)(page_buf + sizeof(*header));

// 검색
for (int i = 0; i < header->key_count; i++) {
    if (entries[i].key == target_key) {
        // 찾았다! row는 이 위치에 있다
        row_ref_t ref = entries[i].row_ref;
        // ref.page_id, ref.slot_id로 heap page 읽기
    }
}
```

특징:
- 노드 = 파일 안의 page (4096바이트 블록)
- 자식을 찾을 때 page_id로 파일 offset 계산 → pread()
- 메모리 접근보다 느리다 (밀리초 단위)
- **프로그램 종료해도 파일에 남아있다**
- 필요한 page만 읽으면 된다

### 5.3 핵심 차이를 한 눈에

```
                메모리 기반              디스크 기반
─────────────────────────────────────────────────────
노드 위치       malloc된 메모리 주소     파일 안의 page 번호
자식 찾기       포인터 역참조            pread(fd, ..., page_id * 4096)
속도            ~100ns                   ~100,000ns (SSD)
영속성          프로그램 종료 시 소멸     파일에 영구 보존
메모리 사용     전체 트리가 메모리에     필요한 page만 메모리에
용량 한계       물리 메모리 크기         디스크 크기
```

### 5.4 실제 DBMS는 어떻게 하는가

SQLite, PostgreSQL, MySQL InnoDB 모두 **디스크 기반**이다.
이유는 간단하다: 데이터가 메모리보다 크기 때문이다.

다만 자주 읽는 page는 메모리에 캐시해둔다.
이것을 **page cache** 또는 **buffer pool**이라 부른다.

```
첫 번째 조회: 파일에서 page 읽기 (느림)
두 번째 조회: 이미 메모리에 있으면 바로 사용 (빠름)
```

이번 과제에서 만드는 pager의 frame cache가 정확히 이 역할이다.

## 6. B+ Tree의 각 연산이 page와 어떻게 연결되는가

### 6.1 Search (검색)

`WHERE id = 500000`을 찾는 과정:

```
Step 1. root page 읽기 (page 2)
        ┌────────────────────────────────────────┐
        │ [100000] [200000] [300000] [400000] ...│
        │ ↓        ↓        ↓        ↓           │
        │ pg10     pg11     pg12     pg13    pg14 │
        └────────────────────────────────────────┘
        500000 > 400000 → 오른쪽 끝 자식 = page 14

Step 2. internal page 읽기 (page 14)
        ┌────────────────────────────────────────┐
        │ [410000] [420000] ... [490000] [500000]│
        │ ↓        ↓            ↓        ↓       │
        │ pg201    pg202        pg209    pg210    │
        └────────────────────────────────────────┘
        500000 >= 500000 → 자식 = page 210

Step 3. leaf page 읽기 (page 210)
        ┌────────────────────────────────────────┐
        │ key=499991 → heap(pg8001, slot3)       │
        │ key=499992 → heap(pg8001, slot4)       │
        │ ...                                     │
        │ key=500000 → heap(pg8002, slot12)  ←찾음│
        │ ...                                     │
        └────────────────────────────────────────┘

Step 4. heap page 읽기 (page 8002)
        slot 12에서 row 데이터를 꺼낸다.
        {id: 500000, name: "홍길동", age: 25}
```

**총 page 읽기: 4번.** 이것이 B+ Tree의 힘이다.

### 6.2 Insert (삽입)

`INSERT INTO users VALUES ('김철수', 30)` → 엔진이 id=1000001 부여

```
Step 1. heap page에 row 저장
        - 빈 slot이 있는 heap page를 찾는다 (또는 새 page 할당)
        - row를 직렬화해서 slot에 쓴다
        - row_ref = (page_id: 8500, slot_id: 7)을 얻는다

Step 2. B+ Tree에 (key=1000001, row_ref) 삽입
        - root부터 leaf까지 내려간다 (search와 같은 경로)
        - leaf page에 entry를 추가한다

Step 3. leaf가 꽉 찼으면? → split
        - leaf page를 반으로 나눈다
        - 새 leaf page를 할당한다 (pager에서 빈 page 받기)
        - 반쪽 entry를 새 page로 옮긴다
        - 부모 internal page에 새 key와 새 page 번호를 추가한다
        - 부모도 꽉 찼으면? → 부모도 split (재귀적)
```

split이 일어나면 **새 page가 파일에 추가**된다.
이것이 B+ Tree가 파일과 연결되는 핵심 지점이다.
트리가 커지면 = 파일에 page가 늘어난다.

### 6.3 Delete (삭제)

`DELETE FROM users WHERE id = 500000`

```
Step 1. B+ Tree에서 id=500000 검색 → row_ref(8002, 12) 획득
Step 2. heap page 8002의 slot 12를 tombstone 처리
Step 3. B+ Tree leaf에서 key=500000 entry 제거
Step 4. leaf의 entry가 너무 적어졌으면?
        - 옆 leaf에서 빌려오기 (borrow/redistribution)
        - 그래도 안 되면 두 leaf를 합치기 (merge)
        - 합친 후 빈 page는 free page list로 반환
```

merge 후 빈 page가 생기면, 나중에 insert 시 재사용할 수 있다.
이것이 malloc 과제의 `free()` 후 재사용과 같은 개념이다.

## 7. 왜 하필 B+ Tree인가 (다른 구조와 비교)

### 7.1 이진 탐색 트리 (BST)

```
fan-out = 2 (자식이 최대 2개)
100만 건 → 높이 약 20
→ page 20개 읽기
```

B+ Tree는 fan-out이 340이라서 높이가 3이다.
같은 100만 건에서 20번 vs 3번.

### 7.2 해시 테이블

```
id = 500000 → hash(500000) → bucket 번호 → 1~2번 page 읽기
```

단건 조회는 B+ Tree보다 빠를 수 있다.
하지만 해시는 **범위 조회를 못 한다.**

```sql
SELECT * FROM users WHERE id BETWEEN 1000 AND 2000;
```

B+ Tree는 leaf가 정렬되어 있고 next_leaf로 연결되어 있어서
1000을 찾은 후 leaf를 따라가면 된다.
해시는 1000개 key를 각각 조회해야 한다.

### 7.3 비교 정리

```
                  단건 조회    범위 조회    삽입      삭제      디스크 친화성
───────────────────────────────────────────────────────────────────────────
정렬 배열         O(log N)     좋음         O(N)      O(N)      좋음
BST               O(log N)     가능         O(log N)  O(log N)  나쁨 (높이 큼)
해시              O(1)          불가능       O(1)      O(1)      보통
B+ Tree           O(log N)     매우 좋음    O(log N)  O(log N)  매우 좋음
```

B+ Tree는 모든 면에서 "나쁘지 않고", 디스크 환경에서 특히 강하다.
이것이 거의 모든 DBMS가 B+ Tree를 기본 인덱스로 쓰는 이유다.

## 8. B+ Tree가 "디스크 친화적"인 이유

### 8.1 fan-out이 크다 = 트리가 낮다 = page를 적게 읽는다

이것이 가장 중요한 이유다.

BST는 노드 하나에 key 하나, 자식 둘.
B+ Tree는 노드(page) 하나에 key 수백 개, 자식 수백 개.

노드 하나를 읽는 비용이 같다면 (둘 다 page 1개 읽기),
더 많은 key를 한 번에 처리하는 쪽이 유리하다.

```
BST:     높이 20 → page 20개 읽기
B+ Tree: 높이 3  → page 3개 읽기
```

### 8.2 leaf가 연결되어 있다 = 순차 읽기가 가능하다

B+ Tree의 leaf는 linked list처럼 연결되어 있다.
다음 leaf의 page_id를 알고 있으므로 순서대로 읽을 수 있다.

```
leaf page 210 → next: page 211
leaf page 211 → next: page 212
leaf page 212 → next: page 213
```

이것은 디스크의 **순차 읽기** 패턴과 잘 맞는다.
순차 읽기는 랜덤 읽기보다 훨씬 빠르다.

### 8.3 노드 크기 = page 크기로 맞출 수 있다

B+ Tree 노드 하나 = 파일의 page 하나로 만들면,
노드를 읽는 것 = pread 한 번으로 완결된다.

다른 트리 구조는 노드 크기가 작아서
하나의 page 안에 여러 노드가 섞이거나,
하나의 노드가 여러 page에 걸치는 문제가 생길 수 있다.

## 9. malloc 경험과의 연결

### 9.1 page 할당 = malloc

```
malloc(size)     → free list에서 빈 블록 찾기 → 반환
pager_alloc()    → free page list에서 빈 page 찾기 → 반환
```

B+ Tree에서 split이 일어나면 새 page가 필요하다.
pager가 free page list를 관리하고 빈 page를 내준다.

### 9.2 page 해제 = free

```
free(ptr)        → 블록을 free list에 반환
pager_free(pg)   → page를 free page list에 반환
```

B+ Tree에서 merge가 일어나면 빈 page가 생긴다.
이 page를 free page list에 반환하면
나중에 다른 split이나 heap insert에서 재사용한다.

### 9.3 free list 구조

malloc 과제에서 배운 implicit/explicit free list와
DB의 free page list는 같은 개념이다:

```
malloc:  header → next_free → next_free → NULL
DB:      db_header.free_page_head → page.next_free → page.next_free → 0
```

### 9.4 단편화 문제

malloc에서 external fragmentation이 생기듯,
DB에서도 delete가 많으면 중간중간 빈 page가 생긴다.

malloc: compaction 또는 coalescing으로 해결
DB: VACUUM으로 해결 (이번 과제에서는 제외)

## 10. 정리: 전체 그림

```
SQL: SELECT * FROM users WHERE id = 500000;

[Parser] "id = 500000 조건이구나"
    ↓
[Planner] "id에 인덱스가 있다 → INDEX_LOOKUP"
    ↓
[Executor] "B+ Tree에서 찾자"
    ↓
[B+ Tree Search]
    root (page 2) → pread → key 비교 → 자식 page 14
    internal (page 14) → pread → key 비교 → 자식 page 210
    leaf (page 210) → pread → key=500000 찾음 → row_ref(8002, 12)
    ↓
[Heap Fetch]
    heap (page 8002) → pread → slot 12 → row 데이터
    ↓
[결과]
    {id: 500000, name: "홍길동", age: 25}

총 pread 호출: 4번
총 읽은 데이터: 4 × 4096 = 16,384바이트 (16KB)

만약 Table Scan이었다면:
총 pread 호출: 33,334번
총 읽은 데이터: 33,334 × 4096 = 약 130MB
```

이것이 B+ Tree, 디스크 page, 인덱싱이 하나로 연결되는 전체 그림이다.

B+ Tree는 단순한 자료구조가 아니다.
"파일에서 필요한 page만 골라 읽기 위한 지도"다.
그 지도 자체도 같은 파일의 page에 저장되어 있어서,
프로그램을 껐다 켜도 지도가 사라지지 않는다.
