# 02. 가상 메모리가 풀어내는 세 가지 문제

> 키워드 트리에서 이 글의 위치: `address generation → Virtual Memory → why Virtual Memory exists (caching / management / protection)`
>
> 이 글은 "왜 CPU가 계산한 주소를 그대로 DRAM 주소로 쓰지 않는가?" 라는 질문 하나에 답한다.

## 왜 VA 라는 중간 단계가 필요한가 — 먼저 없는 세계를 상상해 보기

CPU가 `mov rax, [0x1000]` 을 실행한다고 해 보자. 주소 `0x1000` 은 어디일까?

**가상 메모리가 없는 세계**:
- `0x1000` 은 DRAM 의 물리 주소 `0x1000` 이다.
- 그런데 이 시스템에 프로세스가 여러 개 있다면?
- 프로세스 A 가 `0x1000` 에 뭔가 쓴 순간, 프로세스 B 도 `0x1000` 을 자기 것으로 쓰려고 한다.
- 두 프로세스가 같은 DRAM 영역을 놓고 충돌한다.

이 문제를 "링커가 알아서 안 겹치게 배치" 하는 방식으로 해결할 수 있을까?
이론적으론 가능하지만, 실제 OS 는 수백 개 프로세스를 동시에 돌리고, 그 중 아무거나 종료되고,
새로운 프로세스가 수시로 생성된다. 정적 링크만으로는 감당되지 않는다.

CSAPP 9장은 이 문제를 세 가지로 쪼개서 VA 가 필요한 이유를 정리한다. 키워드 트리의 해당 자리가 그 세 이유다.

```
why Virtual Memory exists
  ├── caching (§9.3)
  ├── management (§9.4)
  └── protection (§9.5)
```

## 이유 1 — caching: DRAM 을 disk-backed data 의 캐시로 쓴다 (§9.3)

가상 메모리의 가장 급진적인 관점은 이것이다.

> **DRAM 은 디스크에 저장된 데이터의 "캐시" 다.**

평소에는 DRAM = 메인 메모리, 디스크 = 저장소 라고 생각했는데,
CSAPP 9장은 이 관계를 한 번 뒤집는다. "프로그램의 진짜 주소 공간은 디스크 위에 있고,
DRAM 은 그걸 당겨다 놓는 캐시일 뿐이다." 라는 관점이다.

이 관점이 중요한 이유는 `DRAM cache 특성` 을 설명해 주기 때문이다.

```
일반 cache (L1/L2/L3)        DRAM cache (VM 에서의 DRAM)
──────────────────────        ──────────────────────────
hit: 1~40 cycle              hit: 100 cycle
miss: 100 cycle (DRAM)       miss: ~10,000,000 cycle (HDD) 또는 ~100,000 cycle (SSD)
→ miss penalty 크지 않음      → miss penalty 극도로 크다
```

miss penalty 가 크기 때문에 VM 의 DRAM cache 는 다른 cache 와 다른 결정을 한다.

- **큰 page 크기 (4KB, 16KB, huge page 는 2MB)** — miss 한 번에 더 많이 가져오기
- **fully associative** — 어떤 page 든 어떤 frame 에 넣을 수 있음 (cache 처럼 set 제한 없음)
- **write-back** — dirty page 만 디스크에 돌려보냄 (매번 쓰면 너무 느림)
- **software 기반 처리** — page fault 처리는 OS 커널이 맡는다. 하드웨어만으로는 감당 불가

### minidb 에서의 대응

재밌는 건 이 구조가 minidb 의 **pager 프레임 캐시** 와 거의 완전히 대응된다는 점이다.

```
OS 가상 메모리                    minidb pager
────────────────────────────      ────────────────────────────
VA                                 page_id
PA (frame 번호)                    frame index
page table                         frame 배열 + find_frame()
TLB                                (없음 — frame 배열이 작아서 선형 탐색)
page fault → 디스크에서 로드        cache miss → pread()
dirty page → write-back            dirty frame → pwrite() on evict
LRU / reference bit                used_tick (가장 작은 값 eviction)
```

즉 나는 이번 주에 **가상 메모리의 DRAM-as-cache 관점을 그대로 SQL 엔진 안에 재구현**했다.
4MB 안팎의 프레임 버퍼로 850MB DB 파일을 다루는 구조가 OS 가 DRAM 으로 디스크를 캐싱하는 그림과 같다.

`docs/04-프레임-캐시-시스템.md` 에 이 대응이 상세히 정리되어 있다.

## 이유 2 — management: 물리적으로 흩어진 메모리를 "연속된 공간" 처럼 보이게 한다 (§9.4)

가상 메모리가 없으면 프로그램은 "내가 쓸 수 있는 주소 범위" 를 실행 시점에만 알 수 있다.
있으면? 모든 프로그램이 **같은 레이아웃** (text → data → bss → heap … → stack) 을 공유한다.

VA 덕분에 OS 는 다음 네 가지를 단순하게 만들 수 있다.

| 단순화 대상 | 없다면 | 있으면 |
|-------------|--------|--------|
| 링킹 | 링커가 런타임 배치를 알아야 함 | 모든 프로그램이 동일한 VA 레이아웃 |
| 로딩 | execve 마다 배치 계산 | execve 가 동일 구조로 매핑 |
| 공유 | 물리 페이지를 복사해서 전달 | 여러 VA → 같은 물리 프레임 |
| 할당 | 연속된 물리 메모리 블록 필요 | 연속 VA + 분산 PA |

**"연속 VA + 분산 PA"** 는 특히 결정적이다.
`malloc(4MB)` 로 받은 VA 는 연속이지만, 실제 4MB 를 구성하는 물리 프레임은
DRAM 여기저기 흩어져 있다. 그럼에도 프로그래머는 그걸 "연속된 4MB" 로 쓸 수 있다.
이 추상화가 없으면 fragmentation 에 시달리는 것은 OS 가 아니라 모든 프로그램이 된다.

## 이유 3 — protection: 프로세스 간, 사용자/커널 간 접근을 막는다 (§9.5)

PTE (page table entry) 에는 단순히 PPN 만 있는 게 아니라 **권한 플래그** 가 같이 있다.

```
PTE flags:
  R/W      — 읽기만 가능 / 쓰기 가능
  U/S      — user 모드로 접근 가능 / supervisor 만 접근 가능
  NX/XD    — 실행 금지 (no-execute)
```

즉 MMU 는 VA→PA 번역을 해 주면서 **매번 접근 권한을 검사**한다.
read-only page 에 write 하면 `page protection fault` → `SIGSEGV`.
user 코드가 supervisor 페이지를 읽으면 마찬가지로 거부.

이 방식 덕분에 OS 는 한 프로세스가 다른 프로세스의 메모리, 또는 커널의 메모리를 침범하는 것을
**CPU 하드웨어 수준에서** 막을 수 있다. 모든 요청마다 소프트웨어가 검사하는 방식으로는 감당되지 않는다.

보안 측면에서 여기에 `ASLR` (주소 난수화) 이 더해진다.
text, stack, heap, shared library 의 **시작 위치를 프로세스마다 무작위화**해서
공격자가 "스택 0xbfff…" 같은 고정 주소를 추측하기 어렵게 한다.

## page state — resident / not resident / unallocated

가상 메모리의 page 는 다음 세 상태 중 하나에 있다.

| 상태 | 의미 | PTE 표시 | 접근 시 |
|------|------|----------|---------|
| resident | DRAM 에 frame 이 있음 | valid=1, PPN 유효 | 바로 번역 성공 |
| not resident | 매핑은 있지만 디스크에 있음 | valid=0, disk 위치 유효 | page fault → 디스크에서 로드 |
| unallocated | 매핑 자체가 없음 | invalid | SIGSEGV |

이 세 상태를 구분하는 게 왜 중요한지, minidb 와 비교하면 바로 보인다.

- minidb 에서 `pager_get_page(5)` 를 호출했을 때,
  - frame 에 있으면 → **resident** 와 같다. 바로 포인터 반환.
  - 디스크에만 있으면 → **not resident** 와 같다. `pread()` 로 로드.
  - `page_id=5` 가 DB 파일 범위를 벗어나면 → **unallocated** 와 같다. 에러.

즉 OS 의 page state 3분할이 내 pager 의 `find_frame → evict_frame → read_page` 흐름과 그대로 대응한다.

## 이번 주 인사이트 — "왜" 를 이해하고 나면 설계가 달라진다

VM 을 공부하기 전엔 "malloc 이 큰 걸 달라고 하면 OS 가 알아서 해 주겠지" 정도였다.
이번 주를 지나고 나서 세 가지 문제를 나눠서 보는 습관이 생겼다.

1. **이건 caching 문제인가?** → 히트율과 miss penalty 를 물어본다.
2. **이건 management 문제인가?** → 연속성 가정과 fragmentation 을 물어본다.
3. **이건 protection 문제인가?** → 권한, 격리, 공격 벡터를 물어본다.

minidb 에서 `pager_flush_all()` 을 설계할 때 이 셋이 전부 적용됐다.

- caching: dirty frame 을 언제 내보낼지 (watermark, LRU)
- management: DB 파일 안에서 페이지를 어떻게 배치할지 (heap chain, free page list)
- protection: 아직 없다. minidb 에는 멀티 프로세스 보호가 필요 없기 때문.

세 번째가 "지금 이 시스템은 필요 없음" 이라는 판단이 가능해진 것도 이 분류 덕분이다.

## 다음 글로의 연결

VA 가 존재해야 한다는 건 알았다. 그럼 그 VA 는 **실제로 어떻게** PA 로 번역되는가?
TLB, page table, multi-level paging, page fault 처리 — 다음 글에서 파이프라인을 따라간다.

→ [03. 주소 변환 파이프라인 — MMU, TLB, Page Table](./03-address-translation-pipeline.md)
