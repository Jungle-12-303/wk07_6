# 09. minidb 회고 — 하드웨어 → OS → SQL까지 메모리로 연결되는 한 주

> 앞 8편이 `csapp-ch9-keyword-tree.md` 의 세로축을 내려가면서 정리한 이론 회고였다면,
> 이 글은 그 전부를 한 번에 "한 프로젝트에 쌓아 본" 실습 회고다.

## 한 주의 구조

이번 주 내 일과는 사실상 두 개 축으로 병행됐다.

```
[아침~낮]  CSAPP 9장 학습
           키워드 트리 정리, Q01~Q25 검증 질문 풀이

[오후~저녁] minidb 구현
           pager, schema, slotted heap, B+ tree, parser, planner, executor
```

키워드 트리에서 본 개념을 **그날 저녁에 직접 쌓아 보는** 구조였다.
덕분에 이론이 구현 결정과 만나는 지점이 자주 생겼다. 이 글은 그 만남의 기록.

## 끝에서부터 보면 — SELECT 한 번의 여정

`SELECT * FROM users WHERE id = 500` 한 줄이 minidb 안에서 어떻게 처리되는지를 끝에서부터 거슬러 올라가 본다.

```
입력:   SELECT * FROM users WHERE id = 500

1) Parser:   statement_t {kind=SELECT, predicate=ID_EQ(500)} 로 변환
2) Planner:  "id 조건" → ACCESS_PATH_INDEX_LOOKUP 선택
3) Executor: bptree_search(500) 호출

bptree_search(500):
4) header page (page 0) 를 pager_get_page 로 획득 → root_index_page_id 확인
5) root page (internal node) 를 pager_get_page 로 획득
6) leftmost_child / entries 를 보고 "500 이 어느 자식으로 가야 하는가" 결정
7) 해당 leaf page 를 pager_get_page 로 획득
8) leaf entries 에서 binary search → row_ref{page_id=7, slot_id=10}

heap_fetch(row_ref):
9) heap page 7 을 pager_get_page 로 획득
10) slot 10 을 읽어서 offset 확인
11) page + offset 위치에서 44바이트 row_deserialize
12) values = {500, "Eve", 28}

출력: 500 | Eve | 28
```

12단계 중 **하드웨어와 OS 가 개입하는 지점** 이 얼마나 많은지를 보면 이번 주 학습의 모든 주제가 한 번씩 등장한다.

| 단계 | 어떤 CSAPP 9장 개념이 등장하는가 |
|------|--------------------------------|
| 2 | rule-based planner — 접근 경로라는 "언제 cache, 언제 scan" 선택 |
| 4,5,7,9 | `pager_get_page` — OS 의 VM cache 를 minidb 가 재구현 |
| 4,5,7,9 cache miss | page fault 와 정신적으로 동일 (`pread`) |
| 5,7 | multi-level indexing (B+ tree) — multi-level page table 의 발상 공유 |
| 9,10,11 | slotted page — 우리 손으로 만든 mini-allocator |
| 11 | packed struct + memcpy — **메모리==디스크** 패턴 |
| 전반 | `page_type` 플래그 — C 에서의 다형성 |

## 가장 중요했던 설계 결정 세 가지

### 결정 1: "DB 파일의 한 블록 = OS 페이지 한 개 = B+ tree 노드 한 개"

이걸 맞추는 결정이 minidb 의 뼈대를 결정했다.

```
page_size = sysconf(_SC_PAGESIZE)
  x86_64 Linux : 4096 B
  Apple Silicon: 16384 B
```

**의의:**
- 디스크 I/O 단위가 OS 페이지 단위와 정렬 → OS page cache 와 충돌 없음
- B+ tree 노드 하나가 한 번의 `pread` 로 올라옴
- leaf 는 한 page 에 ~291 entries, internal 은 ~340 entries 가 들어감 → fan-out 높음
- 10억 건도 B+ tree 높이가 ~6 → 디스크 접근 ~6회로 검색 가능

이 결정을 한 순간, **알고리즘이 요구하는 "fan-out 이 높은 탐색 구조"** 와
**하드웨어가 선호하는 "한 번에 옮기는 단위"** 가 정확히 같은 숫자로 맞물렸다.
이번 주 가장 인상 깊은 설계 순간이었다.

### 결정 2: 프레임 캐시를 직접 구현한다 (mmap 을 쓰지 않는다)

`mmap` 을 쓰면 구현이 반으로 줄어든다. 하지만 그러면 **언제 디스크에 쓰이는지** 를 내가 제어하지 못한다.
직접 만든 이유:
- `pread / pwrite` 로 내가 타이밍을 정한다.
- dirty watermark 기반 선제 flush 로 I/O 를 분산시킨다.
- LRU, pin/unpin, dirty tracking 을 직접 다뤄 보고 싶었다.

결과적으로 이 결정은 `docs/04-프레임-캐시-시스템.md` 한 편에 압축된 지식을 만들어 줬다.
**MMU / TLB / page fault 처리** 를 흉내 내는 구조를 내 손으로 한 번 써 보는 경험이었다.

### 결정 3: 모든 페이지의 첫 4바이트를 `page_type` 태그로 쓴다

C 는 클래스가 없다. 그런데 HEAP / LEAF / INTERNAL / FREE / HEADER 페이지가 모두
같은 4KB 블록이라는 공통 그릇 위에 있다. 이들을 어떻게 하나로 묶을까?

답은 **"블록 안에 타입을 써 넣는다"** 였다. 첫 4바이트 = `page_type` enum.
나머지 바이트는 그 타입에 따라 다른 구조체로 해석. `switch (ptype)` 가 vtable 역할.

이 패턴의 편안함이 놀라웠다. 예컨대 `.pages` 메타 명령어를 만들 때:

```c
for (page_id = 0; page_id < total_pages; page_id++) {
    uint8_t *page = pager_get_page(pager, page_id);
    uint32_t ptype;
    memcpy(&ptype, page, sizeof(uint32_t));
    counts[ptype]++;
    pager_unpin(pager, page_id);
}
```

10 줄이 안 되는 루프 하나로 DB 전체의 페이지 타입 분포를 집계할 수 있다.
같은 걸 상속 + 다형성으로 풀었다면 더 길고 더 간접적이었을 것.

## "C 가 거부감이 있었다" 에서 시작한 여정

개인적으로 가장 큰 변화는 **C 에 대한 내 태도** 였다.

이번 주 시작 전의 나는 이런 상태였다.
- C++ / C# / Java 로 객체지향을 먼저 익힌 사람
- class, interface, 상속, 다형성을 "당연한 추상화 도구" 로 내면화한 상태
- C 는 "그 도구들이 빠져 있어서 답답한 언어" 로 인식

그래서 C 로 minidb 를 쌓기 시작했을 때 애매함이 컸다.
- "이걸 class 로 묶고 싶은데 struct 랑 함수로만 표현해야 한다."
- "상속으로 heap / leaf / internal 을 모두 PageBase 에서 파생시키고 싶은데."
- "인터페이스로 PageSerializer 를 추상화하고 싶은데."

그런데 구현이 진행될수록 이 방향이 **틀린 질문** 이었다는 걸 알게 됐다.
DB 라는 문제 영역에서는 "무엇이 class 가 될 수 있는가" 가 아니라 "무엇이 **page** 가 될 수 있는가" 가 진짜 질문이었다.

### 전환점 — "page 자체를 추상화의 단위로 쓴다"

전환은 B+ tree 구현 중에 왔다. leaf node 와 internal node 를 별도 struct 로 정의하다가,
문득 `두 구조의 공통점은 "4096바이트 블록 + 첫 4바이트 타입" 이구나` 라는 걸 인지했다.
이 공통점을 뽑아내면 leaf 와 internal 의 차이는 "타입 플래그에 따른 해석" 의 문제로 축소된다.

이 관점을 받아들이자 코드가 단순해졌다.
- free page list, heap chain, B+ tree 가 전부 같은 pager 위에서 돌아간다.
- page_id 라는 **4바이트 정수** 가 포인터 역할을 한다. DB 파일을 닫았다 열어도 유효하다.
- 새 페이지 타입을 추가하고 싶으면? `#define` 하나 추가 + switch 한 줄 추가.

상속 계층으로 했다면 `PageBase → HeapPage / LeafPage / InternalPage → ...` 같은 트리가 됐을 것이고,
직렬화/역직렬화 메서드가 각 클래스마다 있었을 것이다.
지금은 그 계층 전체가 없다. 대신 **같은 4KB 블록과 그 안의 플래그** 가 있다.

### 이번 주에 내가 얻은 것

C 에 대한 거부감이 사라진 건 아니다. 여전히 C++ / Rust 의 안전 장치가 그립고,
class 기반 설계가 더 잘 맞는 문제 영역이 많다는 것도 안다.

하지만 **"추상화의 단위" 가 꼭 타입일 필요는 없다** 는 감각을 얻었다.
이 감각은 앞으로 시스템 소프트웨어를 읽을 때 (커널, DB, 네트워크 스택) 훨씬 편하게 해 줄 것 같다.
"왜 여기선 class 를 안 쓰지?" 가 아니라 "여기선 **무엇** 이 추상화의 그릇인가?" 를 묻게 된다.

## 하드웨어 → OS → SQL 의 수직 통합

이번 주의 다른 큰 배움은 **세 계층이 사실은 하나의 축 위에 있다** 는 것이었다.

```
하드웨어:  CPU 가 cache line 을 64B 로 옮긴다. TLB 가 번역을 캐싱한다.
           DRAM 접근은 ~100 cycle, 디스크는 ~10,000,000 cycle.

OS:        그 비용 차이를 흡수하기 위해 VM 이 DRAM 을 disk-backed 데이터의 cache 로 쓴다.
           page fault 가 "비용이 큰 접근" 을 "드물게 일어나는 이벤트" 로 만든다.

minidb:    OS 의 page cache 를 pager frame cache 로 다시 한 번 흡수한다.
           B+ tree 로 "1건을 찾기 위해 1300억 개를 건드리지 않는" 구조를 만든다.
           rule-based planner 가 "언제 cache 를 쓸지, 언제 scan 할지" 정한다.
```

각 계층이 같은 문제를 해결한다. **"비싼 접근 경로를 피하라"**.
단지 계층마다 단위(cache line / page / page_id)와 도구(cache / TLB / frame cache)가 다를 뿐.

이 관점이 잡히고 나니, CSAPP 9장의 `핵심 연결` 절이 한 줄씩 다르게 읽혔다.

> `C object -> pointer -> load/store -> CPU -> VA -> MMU -> TLB -> page table -> PTE -> PA -> cache line -> DRAM`

이게 나한테는 "그냥 주소 하나 번역되는 경로" 가 아니라,
**내가 쓴 `pager_get_page(5)` 한 줄이 어느 계층에 어떤 일을 요청하는지** 의 풀버전 지도가 됐다.

## 이번 주의 가장 인상 깊은 순간 두 장면

### 장면 1: "페이지 단위로 노드를 만들자" 라고 결정한 순간

B+ tree 구현을 시작할 때, 교과서의 `#define ORDER 4` 같은 상수를 쓰려다가 멈췄다.
"왜 ORDER 가 4여야 하지? 이건 디스크와 무관하게 정한 숫자인데."

대신 `4096 - sizeof(header)` 로 쓸 수 있는 공간을 계산하고, `leaf_entry_t` 14B 로 나눠서 ~291 를 얻었다.
이 숫자는 이론적 숫자가 아니라 **하드웨어가 준 숫자** 였다. 그 순간 B+ tree 의 모든 상수가
각자의 물리적 근거를 갖게 됐다.

### 장면 2: `memcpy(&lph, page, sizeof(lph))` 한 줄이 직렬화를 대체한 순간

OOP 로 짤 때는 "페이지를 객체로 역직렬화 → 로직 수행 → 객체를 페이지로 직렬화" 였다.
C 로 짤 때는 "페이지를 `memcpy` 로 헤더에 복사 → 로직 수행 → 헤더를 `memcpy` 로 페이지에 복사" 였다.
같은 일인 것 같지만 결정적인 차이가 있다.
**packed struct 덕분에 "변환" 이 없다. "복사" 만 있다.**

이 단순함이 준 편안함이 컸다. 디버깅할 때 hexdump 한 번으로 모든 게 보인다.

## 앞으로 이어지는 질문들

이번 주의 학습이 끝이 아니고 다음 주로 이어지는 질문이 몇 개 남았다.

- **동시성 제어**: minidb 는 단일 클라이언트 전용. 멀티 커넥션에서 lock / MVCC 를 어떻게 넣을까?
- **WAL**: 현재는 정상 종료 시에만 dirty flush. 비정상 종료 내구성을 주려면 write-ahead log 가 필요.
- **단일 테이블 제한**: 스키마 여러 개를 관리하려면 header page 구조를 어떻게 확장할까?
- **internal 노드의 borrow/merge**: 지금은 leaf 만 한다. 대량 삭제 시 internal underfull 문제.

이 질문들은 다음 주의 learning path 가 될 것 같다.

## 마무리 — 블로그 시리즈의 끝

시리즈 전체를 다시 읽어 보면, 01 에서 시작한 "CPU 가 load/store 로 메모리를 만진다" 부터
09 까지 내려오면서 한 번도 "메모리" 라는 주제에서 벗어나지 않았다는 것이 보인다.
이게 `csapp-ch9-keyword-tree.md` 가 원래 만들어진 목적이기도 했다.
hardware, memory, kernel, C, allocator, runtime, bug 를 **하나의 tree** 로 묶기.

이번 주를 지나고 나서 나에게 남은 것은 이 tree 의 세로축이다.
가장 위의 `C object` 에서 가장 아래의 `cache line / DRAM` 까지, 어느 층에서 문제가 일어나든
아래로든 위로든 움직일 수 있는 감각.

그리고 그 감각이 minidb 라는 실제 코드로 구현돼 있다는 것. 이게 이번 주의 가장 큰 기록이다.

---

## 블로그 시리즈 전체 목차

- [README — 시리즈 개요](./README.md)
- [01 — 물리 세계: CPU, 캐시, DRAM](./01-physical-world-cpu-cache-dram.md)
- [02 — 가상 메모리가 풀어내는 세 가지 문제](./02-why-virtual-memory.md)
- [03 — 주소 변환 파이프라인: MMU, TLB, Page Table](./03-address-translation-pipeline.md)
- [04 — 프로세스 주소 공간과 커널](./04-process-address-space-and-kernel.md)
- [05 — 프로그램이 메모리를 얻는 방법: execve, fork, mmap](./05-program-loading-and-mapping.md)
- [06 — malloc 내부 들여다보기](./06-heap-allocator-deep-dive.md)
- [07 — 메모리 버그와 C의 대가](./07-memory-bugs-and-c-tax.md)
- [08 — GC, 런타임, 그리고 C에서의 추상화](./08-runtime-gc-and-c-abstraction.md)
- **09 — minidb 회고 (이 글)**
