# 06. malloc 내부 들여다보기 — heap allocator의 자료구조와 정책

> 키워드 트리에서 이 글의 위치: `Heap allocation` 절 전체 (malloc API, alignment, heap block, free list, placement, split/coalesce …)
>
> 이번 주 개인 발표 주제였다. heap 에 왜 "아직 공간이 남아 있는데" `malloc` 이 실패하는지를 이해하는 여정.

## 이 질문 하나에서 출발한다

> 총 가용 메모리가 80B 이고 60B 를 요청했는데 왜 `malloc` 이 실패하는가?

이 질문이 heap allocator 의 거의 모든 이슈를 끌어낸다. 답은 단편화다.

```
[16B 할당][48B 가용][16B 할당][32B 가용]

총 가용 = 48 + 32 = 80B
요청   = 60B

48 도 32 도 60보다 작고, 중간에 16B 할당 블록이 끼어 있어서 합칠 수 없다.
→ malloc(60) 실패.
```

**"메모리가 남아 있느냐" 가 아니라 "사용 가능한 형태로 남아 있느냐"** 가 진짜 질문이다.
이 관점이 자리잡으면 allocator 의 모든 설계 결정 (free list, split, coalesce, placement) 이 하나의 축으로 꿰어진다.

## allocator 의 목표는 두 개이고 서로 충돌한다

| 목표 | 의미 |
|------|------|
| throughput | 초당 처리 가능한 alloc/free 횟수 |
| memory utilization | `(사용 중인 바이트 합) / (heap 이 OS 에서 받은 총 바이트)` |

throughput 을 올리려면 탐색을 덜 해야 하고, utilization 을 올리려면 더 꼼꼼히 탐색해서 작은 조각까지 재사용해야 한다.
이 둘이 충돌한다는 사실이 allocator 설계의 긴장감을 만든다.

## heap block — 모든 블록은 "헤더 + 페이로드 + (footer)" 구조

allocated 블록이든 free 블록이든, 모든 블록은 동일한 포맷을 따른다.

```
┌──────────┬────────────────┬──────────┬──────────┐
│ header   │   payload      │ padding  │ footer?  │
│ (1 word) │   (사용자 영역)  │ (정렬용)  │(1 word)  │
└──────────┴────────────────┴──────────┴──────────┘
```

헤더는 **크기 + 상태 플래그** 를 1 word 에 담는다.

### 하위 3비트 트릭

heap 은 보통 8B 또는 16B 단위로 정렬된다. 그러면 모든 블록 크기의 하위 3비트는 언제나 0이다.
그 안 쓰는 비트를 플래그로 재활용한다.

```
header (32-bit 예시):
  31 ............... 3  2  1  0
  ├─── block size ────┤  r  p  a
                         │  │  │
                         │  │  └── allocated (1) / free (0)
                         │  └───── prev block allocated (footer 생략 최적화)
                         └──────── reserved
```

- **bit 0** (`a`): 내가 할당된 블록인가 free 블록인가
- **bit 1** (`p`): 내 **앞** 블록이 할당된 상태인가 (footer 생략 가능 여부)
- **bit 2** (`r`): reserved

이 설계의 아름다움은, **정렬 규칙 하나가 메타데이터 공간을 공짜로 만들어 준다** 는 점이다.
minidb 에서 `page_type` 을 첫 4바이트에 넣는 발상과 정확히 같은 계열이다.
"공짜로 비어 있는 비트를 플래그로 쓰자."

### boundary tag — footer 는 왜 있는가

free 블록의 끝에 같은 정보를 한 번 더 복사해 두는 것이 boundary tag(footer) 다.
이게 있는 이유는 **역방향 coalesce** 때문이다.

현재 free 된 블록의 **앞 블록** 이 free 인지 알려면 앞 블록의 끝에 있는 footer 를 읽어야 한다.
footer 가 없으면 heap 을 앞에서부터 전부 순회해야 한다.

prev-allocated 비트를 헤더에 넣는 최적화를 하면 allocated 블록의 footer 는 생략할 수 있다.
이것도 "절약 한 바이트가 모이면 큰 이득" 인 사례.

## free block organization — 빈 블록을 어떻게 묶을 것인가

allocated 블록들 사이사이에 흩어진 free 블록을 어떻게 빠르게 찾을 것인가?
세 가지 접근이 있다.

**1) implicit free list** — 모든 블록을 순회

```
heap: [A16][F32][A24][F16][A8][F64][A16]

malloc(24) 요청:
  헤더를 따라 앞에서부터 순회 → 첫 번째 free 블록 중 24 이상을 찾음
  탐색: F32 선택
```

쉽지만 탐색 비용이 $O(n)$ (전체 블록 수).
연습용 구현에 딱이지만 실제로는 못 쓴다.

**2) explicit free list** — free 블록만 연결

free 블록의 payload 자리에 prev/next 포인터를 넣는다. 어차피 free 니까 payload 공간을 빌려 쓸 수 있다.
탐색 비용이 $O(f)$ (free 블록 수) 로 줄어든다. 순서는 LIFO(가장 최근 free 를 앞에) 또는 address-ordered.

**3) segregated free list** — 크기 클래스별 별도 리스트

```
bin[0]: 1~8B     → [F8] ↔ [F8] ↔ [F8]
bin[1]: 9~16B    → [F16] ↔ [F12]
bin[2]: 17~32B   → [F32] ↔ [F24] ↔ [F28]
bin[3]: 33~64B   → ...
```

요청이 들어오면 그 크기에 맞는 bin 부터 탐색. 거의 $O(1)$ 이 된다.
없으면 더 큰 bin 에서 꺼내서 split.

- `simple segregated storage` — bin 안의 블록 크기가 고정. split/coalesce 안 함.
- `segregated fits` — bin 안에 범위 있는 크기들. split/coalesce 있음.
- `buddy system` — 2의 거듭제곱 단위로만 관리, 이웃과만 coalesce 가능.

glibc, jemalloc, tcmalloc 모두 segregated 계열 변종이다.

## placement policy — 어떤 블록을 고를 것인가

free list 를 정리해도 **"그 안에서 어떤 블록을 고를 것인가"** 가 남는다.

| 정책 | 전략 | 특징 |
|------|------|------|
| first fit | 찾자마자 사용 | 빠름. 앞쪽에 작은 조각 누적 → 외부 단편화 |
| next fit | 마지막으로 할당한 위치부터 순회 | 캐시에 유리하나 외부 단편화 악화 가능 |
| best fit | 요청 크기에 가장 가까운 블록 선택 | 작은 조각은 줄지만 탐색 비용 증가 |

"어떤 블록을 먼저 고를지" 가 **단편화의 모양을 결정한다** 는 것이 이 주의 발견 중 하나.
같은 데이터, 같은 요청 시퀀스여도 정책에 따라 heap 의 모양이 완전히 달라진다.

## split / coalesce — allocator 의 두 기본 동작

**split**: 선택된 free 블록이 요청보다 클 때, 나머지가 최소 블록 크기 이상이면 쪼갠다.

```
F32 블록에서 16B 요청:
  할당된 16B  |  남은 16B (새 free 블록)

최소 블록 크기보다 작으면? 나눠도 못 쓰는 찌꺼기가 되므로 그냥 통째로 준다 (internal fragmentation 감수).
```

**coalesce**: `free()` 직후 인접한 free 블록을 합친다. 4가지 경우.

```
case 1: 앞 할당 + 뒤 할당 → 합치지 않음
case 2: 앞 할당 + 뒤 free  → 현재 + 뒤 합체
case 3: 앞 free + 뒤 할당  → 앞 + 현재 합체
case 4: 앞 free + 뒤 free  → 앞 + 현재 + 뒤 합체
```

**immediate coalescing** — free() 마다 즉시 합침. 단순.
**deferred coalescing** — 나중에 (예: malloc 실패 시) 한꺼번에 합침. 성능 중심.

## large allocation path — 큰 요청은 별도 경로

128KB 이상이면 glibc 는 기본적으로 `mmap` 으로 처리한다 (05편).
왜? 큰 블록은:
- arena 안에서 찾기도 어렵고
- 반환했을 때 arena 의 끝이 아니면 OS 로 돌려줄 수도 없다

큰 블록은 **독립된 anonymous mmap** 으로 처리하고, `free()` 시 `munmap` 으로 즉시 OS 에 돌려준다.

## thread-aware allocator — arena / tcache

멀티스레드에서 lock 한 개로 heap 을 보호하면 contention 이 병목이 된다.
glibc malloc 은 **arena** 여러 개를 만들어서 스레드마다 독립된 heap 영역을 쓰게 한다.
또 스레드 로컬 캐시(`tcache`) 를 둬서 같은 스레드의 반복된 할당/해제는 lock 없이 처리한다.

메모리 footprint 가 늘어나는 대가를 감수하고 contention 을 줄이는 것.

## free() 의 설계 — 왜 pointer 만 받을까

`free(p)` 는 크기를 받지 않는다. 어떻게 크기를 아는가?

**답: header 에서 역으로 찾는다.**

```c
void free(void *p) {
    if (p == NULL) return;
    header_t *hdr = (header_t *)((char *)p - sizeof(header_t));
    size_t size = hdr->size;
    // ... free list 에 넣기 ...
}
```

이 설계의 함의.
- invalid pointer (malloc 으로 받지 않은 주소) 에 대한 free 는 **정의되지 않은 동작**.
- allocator 는 p 앞 word 를 무조건 header 로 해석한다. 거기에 뭐가 있든.
- 그래서 **double free 는 치명적** 이다. 이미 free list 에 들어간 블록의 header 를 다시 건드리면 리스트가 손상된다.

이 계약이 우리가 포스트 07 에서 볼 모든 버그의 출발점이다.

## production allocator 들의 차이

| allocator | 특징 |
|-----------|------|
| glibc malloc (ptmalloc) | 전통적. arena + tcache. 일반 리눅스의 기본. |
| jemalloc | 단편화에 강함. FB / Rust 에서 기본. |
| tcmalloc | 구글. thread-local cache 중심. 높은 throughput. |

세 구현은 출발한 설계 가정이 다르지만 **segregated free list, 크기 클래스, thread-local cache** 라는 공통점을 가진다.

## minidb 에서의 대응 — "mini allocator" 를 페이지 단위로 짰다

흥미로운 점은, minidb 의 **heap page 슬롯 관리** 가 이 모든 개념의 축소판이라는 것이다.

| glibc allocator 개념 | minidb heap page 대응 |
|----------------------|------------------------|
| heap block header (size + flags) | `slot_t` (offset + status + next_free) |
| allocated / free 플래그 | `SLOT_ALIVE` / `SLOT_FREE` |
| explicit free list | `free_slot_head` + `slot.next_free` chain (LIFO) |
| immediate coalescing | 안 함 (single-row slot 이라 불필요) |
| prologue / epilogue block | heap page 헤더 / `free_space_offset` 경계 |
| split | (고정 크기 row 라 불필요) |
| first fit / best fit | `free_slot_head` 에서 바로 꺼냄 (LIFO = 최근 free 선호) |
| boundary tag | 안 씀 (역방향 coalesce 불필요) |

특히 **`free_slot_head` + `next_free` LIFO 체인** 이 CSAPP 교과서의 explicit free list 와 거의 동일하다.

또 DB 전체 레벨에서는 **free page list** 가 있다. 이건 allocator 에서 page 단위로 한 번 더 올라간 구조.
삭제나 merge 로 비워진 page 를 `free_page_head` 에 LIFO 로 연결해 두고, `pager_alloc_page()` 가 그 리스트부터 본다.

즉 minidb 는 **"row 단위 mini-allocator (slot)"** 와 **"page 단위 mini-allocator (free page list)"** 두 계층을 가진다.
각각의 계층이 CSAPP 의 allocator 개념을 그대로 재현한다.

## 이번 주 인사이트 — allocator 는 "자료구조 + 정책" 이다

이번 주 개인 발표를 준비하면서 allocator 를 `문제 → 구조 → 동작 → 위험` 의 네 덩어리로 풀었다.

```
문제:  단편화 (내부 + 외부)
구조:  free list 조직 (implicit / explicit / segregated) + 헤더 + boundary tag
동작:  placement policy + split + coalesce
위험:  double free, use-after-free, invalid free, leak (다음 글)
```

이 구조가 내 머릿속에 자리잡은 순간, **allocator 는 magic 이 아니라 순수한 자료구조 + 정책 문제** 로 보이기 시작했다.
그리고 그 구조를 minidb 의 slot manager 와 free page list 로 직접 구현해 본 경험이 이해를 단단하게 해 줬다.

"malloc 은 어떻게 동작할까?" 를 알고 쓰는 것과 모르고 쓰는 것의 차이는,
`free` 를 빼먹었을 때 죄책감이 드느냐 안 드느냐 의 차이 정도로 컸다.

## 다음 글로의 연결

지금까지는 allocator 가 잘 동작하는 경우를 봤다. 그런데 allocator 의 계약을 프로그래머가 어기면 어떤 일이 벌어지는가?
다음 글은 C 의 자유가 만들어 내는 대가 — 메모리 버그 전집을 다룬다.

→ [07. C가 프로그래머에게 떠넘긴 것 — 메모리 버그와 Undefined Behavior](./07-memory-bugs-and-c-tax.md)
