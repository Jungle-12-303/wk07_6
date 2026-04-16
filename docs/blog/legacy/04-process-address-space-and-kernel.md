# 04. 프로세스가 보는 세계와 커널의 역할

> 키워드 트리에서 이 글의 위치: `Process address space` 와 `Kernel` 의 교차점
>
> 하드웨어가 주소를 번역해 주는 건 알겠다. 그런데 그 주소 공간을 **누가 만들고 유지하는가?** 이 글은 그 질문에 답한다.

## 프로세스가 보는 주소 공간은 어떻게 생겼나

리눅스에서 `cat /proc/$$/maps` 를 열면 내 셸의 주소 공간이 보인다.
처음 보면 놀랍게 많은 영역으로 쪼개져 있는데, 그 모든 영역은 결국 아래 7개 범주로 분류된다.

```
높은 주소 ↑
    stack          ← call frame, local variable, return address
    memory-mapped  ← mmap 영역, shared library, dlopen 라이브러리
    heap           ← malloc 이 관리하는 동적 객체
    bss            ← zero-initialized global / static
    data           ← initialized global / static
    rodata         ← 읽기 전용 상수, string literal
    text           ← 실행 코드
낮은 주소 ↓
```

이 레이아웃은 **모든 프로세스가 (거의) 동일하게** 본다.
`main()` 의 주소, `printf` 의 주소, `errno` 의 주소는 프로세스마다 다를 수 있지만
"코드는 text 에, 지역변수는 stack 에, malloc 은 heap 에" 라는 **구조** 는 똑같다.

이게 가능한 이유는 앞서 02편에서 본 `management` 때문이다.
VA 덕분에 모든 프로세스가 자기만의 깨끗한 주소 공간을 받는다. 물리적으로는 같은 DRAM 을 공유하고 있지만.

## 커널 공간 vs 유저 공간

x86-64 의 48-bit VA 공간에서 상위 절반은 **커널용**, 하위 절반은 **유저용** 이다.

```
0xFFFFFFFFFFFFFFFF  ┌─────────────────┐
                    │  커널 공간       │  ← supervisor 만 접근 가능
                    │  (상위 절반)     │     PTE 의 U/S 비트 = 0
0xFFFF800000000000  ├─────────────────┤
                    │  (쓰지 않는     │
                    │   canonical gap)│
0x00007FFFFFFFFFFF  ├─────────────────┤
                    │  유저 공간       │  ← user mode 접근 가능
                    │  (하위 절반)     │     PTE 의 U/S 비트 = 1
0x0000000000000000  └─────────────────┘
```

이 분할의 중요한 특징은 **"커널 공간의 매핑은 모든 프로세스가 공유한다"** 는 점이다.
덕분에 syscall 처리 중 커널이 자기 자료구조에 접근할 때 TLB / cache 손실이 적다.
(물론 최근엔 Meltdown/KPTI 이후 유저와 커널 page table 을 분리하는 추세라 상황이 좀 달라졌다.)

## 스레드가 공유하는 것, 공유하지 않는 것

같은 프로세스의 스레드끼리는 주소 공간이 공유된다. 정확히 말하면 이렇다.

| 자원 | 스레드 간 공유? |
|------|-----------------|
| text (code) | 공유 |
| data / bss (전역 변수) | 공유 |
| heap | 공유 |
| VA space / page table | 공유 |
| file descriptor table | 공유 |
| register | 스레드별 |
| stack | 스레드별 |
| scheduling state | 스레드별 |

이 분할이 **동시성의 난이도** 를 정한다.
heap 과 global data 를 공유하기 때문에 race condition 이 생길 수 있고,
stack 과 register 는 스레드별이라 자연스럽게 thread-local 이다.

## task_struct / mm_struct / VMA — 커널이 주소 공간을 추적하는 방법

Linux 커널에서 프로세스 하나는 `task_struct` 다. 그 안에 `mm_struct` 가 있고,
`mm_struct` 안에는 여러 **VMA (Virtual Memory Area)** 가 리스트 / red-black tree 로 묶여 있다.

```
task_struct                    mm_struct              VMAs
────────────                   ─────────             ──────────────────────────
pid, state,                    pgd (최상위 page      VMA [0x400000, 0x401000)
register, ...                   table 의 주소)        → prot=R-X, file-backed (text)
                                                     VMA [0x600000, 0x601000)
                 ──────→       VMA list / rbtree     → prot=RW-, file-backed (data)
                                                     VMA [0x7fff0000, 0x80000000)
                                                     → prot=RW-, anonymous (stack)
                                                     ...
```

여기서 **VMA 와 page table 은 서로 다른 것** 이라는 점이 중요하다.

| VMA | page table |
|-----|------------|
| "어떤 주소 범위가, 어떤 의미로, 어떤 권한을 갖는가" | "어떤 VA 가, 지금 어느 물리 프레임에 매핑돼 있는가" |
| 의미 (what) | 현재 매핑 (where) |
| 넓은 VA 범위 (MB~GB 단위) | page 단위 (4KB) |

예를 들어 `mmap(NULL, 4MB, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)` 을 부르면,
그 순간 VMA 가 하나 생겨서 "0x7f... 부터 4MB 는 anonymous private R/W 입니다" 라고 기록된다.
하지만 page table 의 PTE 는 여전히 invalid 상태다.
실제로 그 영역에 write 가 일어날 때 비로소 page fault 가 발생하고, 그때 PTE 가 채워진다.

이 구분은 `malloc(4MB)` 가 왜 즉시 돌아올 수 있는지를 설명한다.
OS 는 "나중에 쓸 수 있게 공간만 예약" 해 두고, 실제 물리 frame 은 **처음 쓸 때만** 할당한다.

## 공유 페이지 — 같은 물리 프레임을 여러 프로세스가 본다

키워드 트리에는 `shared page` 항목이 있다.
여러 프로세스가 같은 physical frame 을 자기 VA 로 보는 구조인데, 대표 케이스 세 가지.

**1) shared library (libc 같은)**
수백 개 프로세스가 libc 를 쓰지만 libc 의 `.text` 는 물리 frame 하나만 있다.
각 프로세스의 page table 에서 그 frame 을 가리키도록 매핑만 해 둔다.

**2) file-backed shared mmap**
여러 프로세스가 같은 파일을 `MAP_SHARED` 로 매핑하면, 모두가 같은 frame 을 본다.
한 프로세스의 write 가 즉시 다른 프로세스에서 보인다. IPC 의 기본 메커니즘.

**3) COW 직후의 fork()**
fork 직후 parent 와 child 는 모든 private page 를 공유한다. read-only 로 마크되어 있고,
write 가 일어나는 순간만 각자 복사본을 만든다 (03편 참조).

## 커널의 역할 — 누가 이 모든 걸 관리하는가

앞서의 구조가 유지되려면 커널이 여러 역할을 동시에 해내야 한다.
키워드 트리의 `Kernel` 절은 이것을 여섯 덩어리로 정리한다.

### 1. page fault handler

모든 page fault 는 커널의 이 함수 하나로 수렴한다.
- 원인을 분류 (minor / major / invalid / COW / permission)
- 해당 VMA 가 존재하는지 확인
- 파일 읽기, zero-fill, swap-in, COW 복사 중 필요한 동작 수행
- PTE 갱신 + TLB 갱신

### 2. syscall / interrupt / exception — 커널로 들어가는 경로

user mode 는 커널 코드를 직접 실행할 수 없다. 전환은 세 가지 경로로만 일어난다.
- **syscall** — 프로그램이 명시적으로 요청 (`read`, `write`, `mmap`, `brk` …)
- **interrupt** — 외부 이벤트 (타이머, 네트워크 카드 등)
- **exception** — 현재 명령이 문제를 일으킴 (page fault, divide by zero …)

세 경로 모두 "user mode → kernel mode" 전환이고, 진입 후에는 커널 스택으로 바뀌어 처리한다.

### 3. physical page allocator — buddy allocator

커널은 물리 메모리를 **페이지(프레임) 단위** 로 관리한다.
대표 자료구조가 **buddy allocator** 다. 2의 거듭제곱 크기로 연속된 프레임을 할당/반환하면서
합쳐서 더 큰 블록을 만들 수 있을 때 합친다(coalesce). 외부 단편화를 줄이는 전형적 구조다.

### 4. kernel object allocator — slab/slub

커널 내부 자료구조(`task_struct`, `inode`, `dentry`) 는 size 가 일정한 경우가 많다.
slab/slub 은 **같은 크기 객체들을 재사용** 하기 위한 풀이다.
매번 buddy allocator 에서 4KB 를 받아오는 대신, 미리 받아 둔 페이지를 여러 개의 동일 크기 슬롯으로 쪼개서 관리한다.
유저 공간의 `segregated free list` 와 정신적으로 같은 구조다.

### 5. page cache

파일 I/O 와 memory mapping 이 만나는 핵심 계층이다.
`read()` 와 `mmap()` 은 유저 관점에서 다르게 보이지만, 둘 다 결국 page cache 를 거친다.
같은 파일을 한 프로세스는 read, 다른 프로세스는 mmap 으로 접근해도 page cache 를 공유하므로 일관적이다.

### 6. overcommit / OOM killer

Linux 는 기본적으로 **메모리를 "약속" 만 해 둔다**. `malloc(100GB)` 이 성공해도
실제 100GB 를 쓰기 시작하면 어딘가에서 실패한다. 실패 대상이 되는 프로세스를 고르는 것이 OOM killer.
이 정책은 "anonymous page 는 처음 쓸 때만 진짜 생긴다" 는 가정 위에서 돌아간다.

## minidb 에서의 대응 — frame 과 VMA 의 분리

minidb 는 VMA 와 page table 의 구분을 얕게 흉내 낸다.

| OS 개념 | minidb 대응 |
|---------|-------------|
| VMA (무슨 영역인가) | `page_type` 플래그 (HEADER / HEAP / LEAF / INTERNAL / FREE) |
| page table (어느 frame 인가) | frame 배열의 `page_id → frame index` 매핑 |
| task_struct (실행 주체) | DB 파일을 연 프로세스 하나 |
| page fault handler | `evict_frame()` + `pread()` |
| shared page | (없음 — minidb 는 단일 프로세스) |
| buddy allocator | (간단화) `pager_alloc_page()` 가 연속 page 를 한 개씩 할당 |
| slab allocator | (간단화) `free_page_head` 기반 free page list |

특히 `page_type` 한 필드로 "이 페이지의 의미" 를 표현한다는 점은
VMA 의 `prot`/`flags` 가 "이 VA 범위의 의미" 를 표현하는 방식과 정확히 같다.
하드웨어에 가까운 pager 가 사실상 "단일 프로세스의 VM 서브시스템" 을 재구현하고 있는 셈이다.

## 이번 주 인사이트 — "공간" 과 "매핑" 은 다른 개념이다

이번 주 가장 큰 개념적 변화 중 하나가 VMA 와 page table 을 분리해서 생각하게 된 것이다.

- 공간(VMA): "언젠가 여기에 뭐가 올 거다" 라는 **예약**
- 매핑(PTE): "지금 여기에 뭐가 있다" 라는 **현재 상태**

예약만 하고 실제로는 비워 두는 설계는 OS 뿐 아니라 DB 에서도 같다.
minidb 에서 `pager_alloc_page()` 가 page_id 를 발급하지만, 실제로 그 페이지에 값이 쓰이는 건 이후 시점이다.
dirty frame 이 evict 될 때 비로소 디스크에 물리적으로 기록된다.

"공간을 마련하는 시점" 과 "실제로 물리 자원을 쓰는 시점" 을 분리하는 감각은
VM 에서 배워서 minidb 에 적용하게 된 대표적인 전이(transfer) 학습이었다.

## 다음 글로의 연결

프로세스가 보는 주소 공간은 **어떻게 생겨날까?** `execve`, `fork`, `mmap` 이 그 답이다.
다음 글은 프로그램 로딩과 메모리 매핑의 실제 동작을 다룬다.

→ [05. 프로그램이 메모리를 얻는 방법 — execve, fork, mmap](./05-program-loading-and-mapping.md)
