# 08. GC와 런타임, 그리고 C에서의 추상화 — 클래스 없이 타입을 만드는 법

> 키워드 트리에서 이 글의 위치: `Runtime memory management → GC` 와, 그에 대응하는 minidb 의 **C 기반 페이지 추상화 패턴**.
>
> 앞 글(07) 의 끝에서 "다른 언어들은 어떻게 푸는가?" 로 마무리했다. 이 글은 그 답과 함께, C 에서 객체지향 없이도 추상화가 가능하다는 것을 이번 주 경험으로 보여 준다.

## 각 언어는 "메모리 수명" 을 어떻게 푸는가

수명 관리 방식은 크게 네 가지다.

| 접근 | 예시 언어 | 핵심 |
|------|----------|------|
| 수동 관리 | C | 프로그래머가 `free` |
| RAII + smart pointer | C++ | 스코프 종료 시 소멸자 자동 호출 |
| ownership 추적 | Rust | 컴파일 타임에 lifetime 검증 |
| garbage collection | JVM, Go, Python, JS | 런타임이 도달 불가능 객체 수거 |

이 중 GC 가 시스템 엔지니어 입장에서 가장 흥미로운 이유는 **"런타임이 수명을 자동 결정한다"** 는 점이다.
allocator 는 "어디에 놓는가" 를, GC 는 "언제 죽는가" 를 결정한다 — 서로 다른 역할이지만 같은 힙을 공유한다.

## GC 의 기본 모델 — 메모리를 방향 그래프로 본다

GC 의 출발점은 이 관점이다.

> **메모리는 포인터로 연결된 directed graph 다.**
> 노드 = 힙 블록, 간선 = 포인터.

```
  root set (stack, register, global)
   │
   ▼
 ┌───┐       ┌───┐       ┌───┐
 │ A │─────→ │ B │─────→ │ C │
 └───┘       └───┘       └───┘
   │                       ▲
   │         ┌───┐         │
   └───────→ │ D │─────────┘
             └───┘

 ┌───┐       ┌───┐
 │ E │─────→ │ F │    ← root 에서 도달 불가 → 쓰레기
 └───┘       └───┘
```

**root set** 은 프로그램이 "확실히 살아 있다" 고 아는 포인터들의 집합이다.
stack frame 의 지역 변수, CPU register, global variable.

**reachability** 는 root 에서 포인터를 따라 도달할 수 있느냐다.
도달 가능하면 살아 있는 것, 도달 불가면 쓰레기.

## mark-and-sweep — 가장 직관적인 GC

```
Phase 1: Mark (root 에서 DFS/BFS)
  for r in root_set:
    mark(r)
  mark(obj):
    if obj is marked: return
    obj.mark_bit = 1
    for ptr in obj.outgoing_pointers:
      mark(ptr)

Phase 2: Sweep (힙 전체 순회)
  for blk in heap:
    if blk.mark_bit == 1:
      blk.mark_bit = 0   # 다음 라운드를 위해 리셋
    else:
      free(blk)          # 도달 불가 → allocator 에 반환
```

간단하지만 **멈춤(stop-the-world)** 시간이 길다. 프로덕션 GC 는 이걸 정교하게 다듬는다.

- **generational GC** — "대부분의 객체는 금방 죽는다" 는 관찰을 이용해 새 객체만 자주 수거
- **incremental / concurrent GC** — 응용이 돌아가면서 조금씩 수거
- **reference counting** — 참조 카운트를 유지하다 0이 되면 즉시 해제 (Python, Swift)
  - 순환 참조는 별도 감지 필요

## C 에서 GC 를 한다면? — `isPtr` 문제

CSAPP 9.10.2 가 다루는 주제다. C 에서 보수적(conservative) GC 를 구현할 때의 한계.

문제: **어떤 워드가 포인터인지 정수인지 구분할 수 없다.**

```c
int x = 0x4000;   // 이건 숫자인가? 아니면 0x4000 주소의 포인터인가?
```

C 는 타입 정보를 런타임에 유지하지 않는다. GC 가 mark phase 에서 스택을 스캔할 때
`0x4000` 이 포인터처럼 **보이면** "일단 mark" 할 수밖에 없다.
이러면 **"살아 있지 않은데 살아 있다고 오인하는"** 케이스가 생겨서 회수를 못 한다.
→ 이것이 보수적 GC 가 메모리 낭비를 만드는 이유.

또 C 포인터는 블록 내부를 가리킬 수 있다 (`p + 4`).
GC 가 "이 주소가 힙 어느 블록에 속하는가?" 를 빠르게 알아내려면 balanced tree 같은 별도 자료구조가 필요하다.
`isPtr(w)` 한 번이 $O(\log n)$.

이 대목이 "**C 는 GC 와 궁합이 나쁘다**" 라는 자주 듣던 말의 구체적인 이유였다.
언어가 포인터와 정수를 구분해 주지 않기 때문.

## escape analysis — 힙에 갈 필요 없는 객체는 스택으로

일부 런타임(Go, JVM) 은 함수가 리턴한 후에도 참조되는 객체와 그렇지 않은 객체를 컴파일 타임에 분석한다.
"escape 하지 않는" 객체는 힙이 아니라 **스택** 에 할당해 버린다.

```go
func foo() {
    x := &Point{1, 2}   // 포인터지만 foo 밖으로 escape 안 함
    bar(x)              // → 스택 할당으로 최적화
}
```

이 최적화가 힙 allocation 을 크게 줄인다. GC 부담이 감소한다.

## 이제 본론 — C 에서는 추상화를 어떻게 하는가

여기서부터가 이번 주 **가장 인상 깊었던 경험** 이다.

C 는 class 가 없다. interface 도 상속도 가상함수도 없다.
C++ / C# / Java 에서 자연스럽게 쓰던 추상화 도구가 전부 빠져 있다.

그런데 minidb 를 짜면서 **다른 종류의 추상화** 가 가능하다는 걸 알게 됐다.
이 경험을 한 문장으로 정리하면 이렇다.

> **같은 메모리 블록을, 헤더의 플래그에 따라 다른 타입으로 해석한다.**

## 패턴의 정체 — "page type tag + packed struct"

minidb 의 핵심 설계는 이렇다.

```c
// 모든 페이지의 첫 4바이트는 page_type 이다.
#define PAGE_TYPE_HEADER    0x01
#define PAGE_TYPE_HEAP      0x02
#define PAGE_TYPE_LEAF      0x03
#define PAGE_TYPE_INTERNAL  0x04
#define PAGE_TYPE_FREE      0x05

// packed 구조체가 곧 디스크 포맷
typedef struct {
    uint32_t page_type;              // 0x02
    uint32_t next_heap_page_id;
    uint16_t slot_count;
    uint16_t free_slot_head;
    uint16_t free_space_offset;
    uint16_t reserved;
} __attribute__((packed)) heap_page_header_t;

typedef struct {
    uint32_t page_type;              // 0x03
    uint32_t parent_page_id;
    uint32_t key_count;
    uint32_t next_leaf_page_id;
    uint32_t prev_leaf_page_id;
} __attribute__((packed)) leaf_page_header_t;

typedef struct {
    uint32_t page_type;              // 0x04
    uint32_t parent_page_id;
    uint32_t key_count;
    uint32_t leftmost_child_page_id;
} __attribute__((packed)) internal_page_header_t;
```

사용할 때는 다음처럼 **같은 4096바이트 블록을 다른 구조체로 해석** 한다.

```c
uint8_t *page = pager_get_page(pager, page_id);   // 디스크에서 4KB 로드

// 첫 4바이트만 읽어서 타입 판별
uint32_t ptype;
memcpy(&ptype, page, sizeof(uint32_t));

switch (ptype) {
    case PAGE_TYPE_HEAP: {
        heap_page_header_t hph;
        memcpy(&hph, page, sizeof(hph));
        slot_t *slots = (slot_t *)(page + sizeof(hph));
        // ... heap 로직 ...
        break;
    }
    case PAGE_TYPE_LEAF: {
        leaf_page_header_t lph;
        memcpy(&lph, page, sizeof(lph));
        leaf_entry_t *entries = (leaf_entry_t *)(page + sizeof(lph));
        // ... leaf 로직 ...
        break;
    }
    case PAGE_TYPE_INTERNAL: {
        internal_page_header_t iph;
        memcpy(&iph, page, sizeof(iph));
        internal_entry_t *entries = (internal_entry_t *)(page + sizeof(iph));
        // ... internal 로직 ...
        break;
    }
}
```

## 이 패턴을 OOP 언어의 개념과 대응시키면

```
C++ / OOP 개념                  minidb 의 C 대응
───────────────────────         ─────────────────────────────────
class                           packed struct + 관련 함수 묶음
상속 / 다형성                    page_type 첫 4바이트 + switch
vtable                          page_type 에 따른 분기
생성자                           memset + 필드 초기화 + memcpy
직렬화 / 역직렬화                불필요 (메모리 == 디스크 포맷)
vector<T>                       포인터 산술 (page + offset + i*size)
shared_ptr (참조 카운팅)         pin_count
map<key, value>                 B+ tree (정렬된 엔트리 배열)
포인터 (메모리 주소)              page_id (파일 내 위치)
new / delete                    pager_alloc_page / pager_free_page
캐시 (LRU map)                  frame_t 배열 + used_tick
```

가장 인상적이었던 대응 두 가지.

**1) "page_type 첫 4바이트" = "vtable 포인터"**

C++ 객체에는 자동으로 vtable 포인터가 앞에 붙는다. 그 포인터로 가상 메서드 디스패치가 이뤄진다.
minidb 는 그 아이디어를 뒤집는다. "vtable" 대신 **4바이트 enum 태그** 를 맨 앞에 둔다.
이걸로 switch 해서 디스패치하는 것이 vtable 방식의 런타임 다형성과 정신적으로 같다.

**2) "packed struct" = "메모리 레이아웃 = 디스크 포맷"**

클래스 기반 추상화에서는 보통 **직렬화/역직렬화** 라는 중간 레이어가 필요하다.
디스크로 저장할 땐 JSON/바이너리 포맷으로 변환하고, 읽을 땐 다시 객체로 복원.
minidb 는 그 변환이 없다. `__attribute__((packed))` 덕분에 **메모리의 바이트 배치가 곧 디스크 포맷** 이다.
`memcpy` 한 번으로 두 세계를 오간다.

## 왜 이게 새로운 추상화인가

객체지향으로 추상화를 배운 사람이 당연하게 여기는 건 다음 두 가지다.

- **타입 식별자가 컴파일 타임에 정해진다** — `HeapPage` 클래스인지 `LeafPage` 클래스인지가 코드에 박혀 있다.
- **추상화의 단위가 "클래스" 다** — 하나의 의미 묶음은 하나의 클래스.

minidb 의 패턴은 둘 다 다르다.

- **타입 식별자가 런타임에 메모리 블록 안에 들어 있다** — 같은 4KB 블록이 어떤 타입인지는 첫 4바이트를 읽어야 안다.
- **추상화의 단위가 "페이지(메모리 블록) + 플래그" 다** — 의미가 블록 자체에 담긴다.

이 방식은 C++ 의 상속 대신 **"같은 용기(container)에 다른 내용물"** 이라는 발상에 가깝다.
container 는 페이지, 내용물은 페이지 타입. 하드웨어가 이미 제공하는 "4KB 단위" 를 그대로 추상화의 단위로 빌린다.

## 이 패턴이 왜 DB 에서 자연스러운가

이 발상이 DB 같은 시스템에서 자연스럽게 나오는 이유는 세 가지다.

**1) 디스크 I/O 단위와 맞는다**
pread/pwrite 는 바이트 단위로 할 수 있지만, OS 는 페이지 단위로 캐싱한다.
추상화의 단위를 페이지와 맞추면 I/O 가 낭비되지 않는다.

**2) 메모리와 디스크의 일관성**
packed struct 가 직렬화/역직렬화 비용을 없애기 때문에,
같은 자료구조 코드가 **"메모리 안에서 작동" 과 "디스크 위에 저장됨"** 에 대해 공통된다.

**3) 디버깅이 쉽다**
문제가 생겼을 때 `hexdump -C sql.db | head` 만 해도 구조가 바로 보인다.
JSON/Protobuf 같은 직렬화 포맷이었다면 파싱 도구가 필요했을 것.

## 이번 주 인사이트 — "추상화의 단위" 가 바뀌었다

이번 주 이전에 내가 알던 추상화는 전부 **"타입" 중심** 이었다.

- C++ : 클래스라는 타입을 만들고 인스턴스를 만든다.
- Java: 인터페이스라는 타입 계약을 만들고 구현체를 만든다.
- TypeScript: 제네릭 타입으로 구조를 표현한다.

그런데 minidb 에서 시도한 추상화는 **"메모리 블록과 그 안의 플래그" 중심** 이었다.

- 메모리 블록 = 추상화의 그릇
- 블록 내부의 플래그(page_type) = 런타임 타입 정보
- 같은 그릇을 플래그에 따라 다른 의미로 해석

이 방식을 "관리 언어에서 쓰겠다" 는 건 아니다. 프로덕션 애플리케이션에서는 여전히 class 기반 추상화가 답일 것이다.
다만 **"페이지 = 추상화 단위"** 라는 발상이 OS 의 VM, 커널의 slab allocator, DB 의 B+ tree 같은
**하드웨어 가까운 소프트웨어** 에서 어떻게 반복적으로 나타나는지를 체험한 것이 이번 주의 큰 배움이었다.

C 가 클래스가 없어서 답답한 게 아니라, C 가 있는 **레벨**(하드웨어 가까움)에서는 클래스보다
페이지 + 플래그 조합이 더 자연스러운 추상화가 된다는 걸 알게 됐다.

## 관련 문서

이 패턴은 minidb 문서 `docs/05-C-메모리-추상화-패턴.md` 에 코드 레벨로 정리되어 있다.
이 블로그 글이 "감상" 이라면 그 문서는 "레퍼런스".

## 다음 글로의 연결

이제 8편의 이론을 마무리하고, 마지막으로 이번 주의 **구현 경험 전체** 를 minidb 라는 프로젝트의 회고로 엮는다.
하드웨어 → OS → SQL 까지의 수직 통합, 그리고 그 과정에서 실제 내 손에 남은 감각들.

→ [09. minidb 회고 — 하드웨어 → OS → SQL까지 메모리로 연결되는 한 주](./09-minidb-retrospective.md)
