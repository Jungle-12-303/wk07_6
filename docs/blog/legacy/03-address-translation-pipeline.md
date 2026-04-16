# 03. 주소 변환 파이프라인 — MMU, TLB, Page Table

> 키워드 트리에서 이 글의 위치: `Address translation → MMU → TLB / page table → PTE → page walk → page fault`
>
> 이 글은 `mov rax, [0x7fff1234]` 한 줄이 실제 DRAM 주소에 닿기까지 몇 번의 단계를 거치는지를 따라간다.

## 큰 그림 — 8단계로 압축한 번역 파이프라인

CPU 가 VA 를 발행한 순간부터 데이터가 레지스터에 들어올 때까지의 과정은 다음과 같다.
키워드 트리의 `translation pipeline` 에 정리된 흐름을 번호로 풀었다.

```
① CPU가 VA 발행
② VPN(상위 비트) 추출, VPO(하위 12비트) 추출
③ TLB 조회 (VPN 으로 질의)
    ④-a hit  → PPN 즉시 획득
    ④-b miss → page walk:
                CR3 → PML4[VPN1] → PDPT[VPN2] → PD[VPN3] → PT[VPN4] → PTE
                (4레벨이면 4회 메모리 접근)
⑤ PTE 검사:
    valid  + 메모리에 있음 → PPN 획득, TLB 갱신
    valid  + 디스크에 있음 → page fault → 커널이 로드
    invalid              → SIGSEGV
⑥ PA = PPN + PPO
⑦ cache 조회 (PA 로 tag 비교)
⑧ hit 면 데이터 반환, miss 면 DRAM 접근
```

이번 주에 이 8단계가 머릿속에 자리잡는 순간, "페이지 폴트" 라는 단어가 갑자기 아주 구체적인 사건으로 느껴지기 시작했다.

## VA / PA 의 구조 — VPN + VPO

4KB page 를 쓰는 x86-64 를 기준으로 설명한다.

```
VA (48-bit):
    ┌──────────────────────────────┬───────────────┐
    │            VPN (36-bit)       │   VPO (12-bit) │
    └──────────────────────────────┴───────────────┘
                 ↓ 번역                   ↓ 그대로 복사
PA (52-bit):
    ┌──────────────────────────────┬───────────────┐
    │            PPN (40-bit)       │   PPO (12-bit) │
    └──────────────────────────────┴───────────────┘
```

여기서 핵심은 `VPO == PPO` 라는 점이다. page 크기가 같기 때문에
**page 내부의 오프셋은 번역 대상이 아니다.** MMU 는 상위 bit 만 번역한다.

이 사실이 VIPT cache (나중에 설명) 를 가능하게 만든다.

## TLB — 번역 결과의 캐시

매번 page walk 를 하면 메모리 접근이 4배 느려진다. (4-level paging 기준)
그래서 MMU 는 번역 결과를 **TLB(Translation Lookaside Buffer)** 라는 작은 캐시에 저장해 둔다.

```
TLB 구조 (set-associative cache):
    VPN → (TLBT, TLBI) 로 쪼개서 질의
    hit: 1 cycle 만에 PPN + permission 반환
    miss: page walk 필요
    flush: CR3 교체 시 전체 무효화 (보통의 경우)
    PCID/ASID: 프로세스 식별자로 flush 최소화
```

TLB 의 흥미로운 점은 **permission 도 캐시한다** 는 것이다.
PTE 의 R/W, U/S, NX 비트가 함께 들어가 있어서, TLB hit 면 권한 검사도 동시에 끝난다.

이번 주 팀 리뷰에서 나온 질문 중 하나가 "TLB flush 는 얼마나 비싼가?" 였는데,
context switch 마다 TLB 를 통째로 버리면 switch 직후 성능이 크게 떨어진다.
그래서 현대 CPU 는 PCID (Process-Context Identifier) 로 프로세스마다 TLB 엔트리에 태그를 달아,
switch 해도 해당 프로세스 엔트리는 살려 둔다.

## multi-level page table — 왜 여러 단계인가

48-bit VA 를 단일 레벨로 관리하면 PTE 가 `2^36` 개, 즉 페이지 테이블만 512GB 다. 불가능하다.
그래서 x86-64 는 **4-level paging** 을 쓴다.

```
VA 48-bit = 9+9+9+9 + 12
    PML4 index (9bit)  →  PML4 table (4KB, 512개 PTE)
    PDPT index (9bit)  →  PDPT table
    PD   index (9bit)  →  PD   table
    PT   index (9bit)  →  PT   table (여기서 PPN 을 얻는다)
    PPO       (12bit)  →  page 안의 offset
```

각 레벨의 테이블이 정확히 **한 페이지 (4KB = 512 × 8B PTE)** 라는 사실이 우아하다.
page 라는 단위를 정해 두고, 그 단위를 테이블 자체에도 재사용한다. 자기참조적 설계.

**쓰지 않는 VA 범위에 대해서는 테이블을 만들지 않는다.**
이게 multi-level 의 진짜 이익이다. 빈 주소 공간을 가진 프로세스도 PML4 하나만 있으면 시작할 수 있고,
VA 를 실제로 쓰기 시작할 때 필요한 레벨의 테이블만 lazy 하게 생성된다.

최신 서버는 57-bit VA 를 쓰는 `PML5` (5-level paging) 도 쓴다.

## PTE — Page Table Entry 의 필드

PTE 는 단순한 PPN 포인터가 아니다. 여러 비트가 함께 들어가 있다.

| 필드 | 의미 |
|------|------|
| present / valid | 이 매핑이 유효한가 (0이면 fault) |
| PPN | 물리 프레임 번호 (40-bit) |
| R/W | 쓰기 허용 여부 |
| U/S | 유저 모드 접근 허용 여부 |
| dirty (D) | 쓰기 발생 여부 (write-back 시 사용) |
| accessed (A) | 접근 여부 (page replacement 알고리즘용) |
| PCD | Page Cache Disable |
| PWT | Page Write-Through |

특히 `dirty` 와 `accessed` 는 **하드웨어가 프로그래머 대신 관찰해 주는 비트** 다.
PTE 를 건드리지 않아도 CPU 가 page 에 접근하거나 쓰면 해당 비트를 자동으로 켠다.
OS 는 이 비트를 주기적으로 읽어서 LRU 근사치를 만들거나, dirty 인 page 만 디스크에 쓴다.

PTE 상태는 크게 세 가지로 나뉜다.

```
valid=1, 메모리에 있음   → PPN 변환 성공
valid=1, 디스크에 있음   → page fault → 커널이 로드 → 재시도
valid=0, 매핑 자체 없음  → SIGSEGV
```

## page fault — 버그일 수도 있고 정상 이벤트일 수도 있다

내가 이번 주 가장 혼란스러워 한 부분이 여기였다.
"page fault" 라고 하면 반사적으로 "에러" 라고 생각했는데, CSAPP 9장은 이걸 셋으로 쪼갠다.

| 유형 | 의미 | 처리 |
|------|------|------|
| minor fault | 메모리엔 있는데 현재 PTE 에 매핑이 안 된 경우 | 디스크 I/O 없이 해결 |
| major fault | 디스크에서 읽어와야 하는 경우 | disk I/O 발생 |
| invalid reference | 잘못된 포인터, 권한 위반 | SIGSEGV |

즉 page fault 의 절대 다수는 **정상적인 lazy loading 이벤트** 다.
`execve` 직후 text/data 가 실제로 메모리에 올라가는 것도 fault 를 통해서다.
`malloc` 으로 받은 페이지에 처음 write 하는 것도 (anonymous private + zero-fill-on-demand) fault 를 거친다.

fault 처리 흐름은 다음과 같다.

```
① 하드웨어 fault 발생 → 커널 모드 진입
② 커널이 fault address, error code 로 원인 판단
③ 해당 VMA 가 있는가? → 없으면 SIGSEGV
④ 권한이 맞는가? (R/W/X)
⑤ 필요하면 frame 하나를 할당 (또는 eviction)
⑥ disk 에서 읽기 or zero-fill
⑦ PTE 갱신 (valid=1, PPN 세팅)
⑧ TLB 갱신
⑨ fault 를 일으킨 명령어를 다시 실행
```

step 9 가 결정적이다. fault 가 해결되면 CPU 는 **같은 VA 로 같은 명령을 다시 실행** 한다.
이제는 매핑이 유효하므로 번역이 성공하고, 프로그램은 아무 일도 없었던 것처럼 계속 돌아간다.

## Copy-on-Write (COW) — fault 를 "기능" 으로 활용하는 대표 사례

`fork()` 가 성능적으로 감당되는 이유는 COW 덕분이다.

```
parent 가 100MB 메모리를 쓰고 있음
→ fork() 실행

순진하게 복사하면:
  child 를 위해 100MB 를 복사 → 매우 비쌈

COW:
  parent 의 모든 private page 를 read-only 로 마킹 (PTE 수정)
  child 도 같은 PTE 를 공유
  → fork() 자체는 거의 0 비용

이후:
  parent 나 child 가 write 시도
  → read-only 위반 → protection fault
  → 커널이 해당 page 만 새 frame 에 복사
  → 두 PTE 를 각각 R/W 로 업데이트
  → 명령어 재실행
```

즉 fault 라는 "예외 같은" 메커니즘이 사실은 **lazy 복사를 구현하는 도구** 로 쓰이고 있다.
이 관점이 내겐 굉장히 새로웠다. page fault 는 에러 처리 경로가 아니라, VM 이 "필요할 때만 일하기" 위한 기본 동작이다.

## Core i7 의 translation + cache 병렬 — VIPT

CSAPP 의 Core i7 절에서 인상적이었던 건 L1 d-cache 의 **VIPT** (Virtually Indexed, Physically Tagged) 구조였다.

```
TLB 조회:      VPN → PPN (상위 bits 번역)
cache 조회:    VPO 로 set index 선택, PA 로 tag 비교

핵심: VPO == PPO 이므로, TLB 와 cache 를 병렬로 시작할 수 있다.

           VPN ─────→ TLB ─────→ PPN
                                  ↓
           VPO ─────→ cache set 선택 → tag 비교 ← PPN 과 합쳐 PA
```

즉 번역 결과가 나오기 전에 cache 탐색을 미리 시작할 수 있다.
TLB 가 hit 하면 거의 동시에 cache 결과도 나온다.

이 대목을 이해하고 나서, "캐시와 번역은 서로 독립이 아니다" 라는 키워드 트리의 마지막 메모가
왜 중요한지를 알게 됐다.

## minidb 에서의 대응 — pager 가 자체 MMU 를 구현한다

MMU 와 page table 구조는 minidb pager 와 거의 1:1 로 대응한다.

| OS 가상 메모리 | minidb pager |
|----------------|--------------|
| VA | page_id |
| PA | frame index |
| page table | frame 배열의 선형 탐색 (find_frame) |
| TLB | 없음 (frame 배열이 256개라 불필요) |
| PTE valid | frame_t.is_valid |
| PTE dirty | frame_t.is_dirty |
| CR3 / address space switch | DB 파일을 여는 시점 (pager_open) |
| page fault | cache miss → pread() |
| eviction | LRU + dirty write-back |

TLB 에 해당하는 추가 캐시가 없는 이유는, 선형 탐색하는 frame 이 256개라 무시할 수 있는 비용이기 때문이다.
만약 프레임 수가 수천~수만 개로 늘어난다면 해시맵을 추가로 만들어야 할 것이고, 그것이 사실상 TLB 와 같은 역할을 할 것이다.

## 이번 주 인사이트 — page fault 가 "기능" 이라는 감각

하드웨어의 fault 메커니즘이 에러 처리가 아니라
**"필요할 때만 일하기 위한 엔진"** 으로 쓰인다는 것을 받아들이는 데 시간이 걸렸다.
COW, demand paging, lazy file mapping, zero-fill-on-demand …
이 모두가 "처음 접근 시 한 번 fault 를 일으키고, 그때 진짜 일을 시작한다" 는 같은 패턴이다.

이 감각이 생기고 나서 minidb 의 frame cache 에서도 같은 패턴을 더 적극적으로 쓰기 시작했다.
예컨대 새 page 를 alloc 할 때 실제 디스크에 0을 쓰지 않고, 메모리에만 0 으로 초기화해 둔 뒤
처음 flush 될 때 디스크에 기록되게 하는 식이다. 이것은 OS 의 zero-fill-on-demand 를 SQL 레이어에 이식한 셈이다.

## 다음 글로의 연결

여기까지는 MMU 와 하드웨어 중심이었다. 그런데 이 모든 과정은
"어떤 프로세스의 주소 공간인가?" 라는 컨텍스트 안에서만 의미가 있다.
다음 글은 프로세스 주소 공간의 구성과, 그것을 유지·보호하는 커널의 역할을 다룬다.

→ [04. 프로세스가 보는 세계와 커널의 역할](./04-process-address-space-and-kernel.md)
