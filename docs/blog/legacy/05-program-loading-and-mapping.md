# 05. 프로그램이 메모리를 얻는 방법 — execve, fork, mmap

> 키워드 트리에서 이 글의 위치: `Program loading and mapping` 섹션 전체
>
> 프로세스의 주소 공간이 어떻게 **처음 만들어지고** 어떻게 **확장되는가?** 이번 글은 세 개의 syscall 로 답한다.

## 들어가며

이번 주 학습에서 가장 "유닉스 답다" 고 느낀 대목이 이 파트였다.
`execve`, `fork`, `mmap` 세 syscall 이 어떻게 조합되어 프로세스 생성/로딩/메모리 확보를 해내는지를 보니,
"모든 게 파일" 이라는 유닉스 철학의 메모리 쪽 버전, 즉 **"모든 게 매핑"** 이 선명해진다.

## execve() — 현재 프로세스의 주소 공간을 날리고 새 프로그램을 올린다

내가 `./build/minidb sql.db` 를 실행하면 셸이 `fork()` 후 `execve("./build/minidb", …)` 를 호출한다.
이 시점에 셸이라는 프로세스의 주소 공간이 통째로 교체된다.

execve 의 내부 동작을 단계로 풀면 다음과 같다.

```
① 기존 VA 공간 전체 삭제 (모든 VMA, page table)
② ELF 헤더를 읽어서 새 프로그램의 구조 파악
③ 새 VMA 들을 생성:
    .text        → file-backed, private, PROT_READ|PROT_EXEC
    .rodata      → file-backed, private, PROT_READ
    .data        → file-backed, private, PROT_READ|PROT_WRITE
    .bss         → anonymous, private, zero-fill-on-demand, R/W
    stack        → anonymous, private, R/W
    shared libs  → file-backed (libc.so 등)
④ PC 를 ELF 의 entry point 로 설정
⑤ 실행 시작 — 이 시점에서 PTE 는 대부분 invalid
⑥ 실제 코드가 실행되면 page fault → 디스크에서 읽기 (demand paging)
```

결정적인 건 ⑤ 단계다. **execve 직후에도 프로그램의 text 는 거의 메모리에 올라와 있지 않다.**
`main()` 의 첫 명령이 실행되는 순간 minor page fault 가 발생하고,
그때서야 해당 text 페이지가 디스크(정확히는 page cache)에서 frame 으로 올라온다.

"실행 파일을 메모리에 로드한다" 는 표현은 옛 시절의 잔재다.
현대 OS 는 로드하지 않는다. **매핑** 한다. 실제 이동은 fault 에 맡긴다.

## mmap() — 파일 또는 익명 메모리를 VA 에 연결하는 일반 메커니즘

`mmap(addr, length, prot, flags, fd, offset)` 은 유닉스 메모리의 **가장 일반적인 원시 도구** 다.
execve 도 내부적으로 이걸 호출하고, `malloc` 도 큰 요청일 때 이걸 쓴다.

mmap 의 4가지 조합은 거의 모든 메모리 사용 패턴을 커버한다.

| fd 와 flags | 예시 용도 | 동작 |
|-------------|----------|------|
| file-backed + MAP_SHARED | 파일 IPC, mmap 기반 I/O | write → 파일에 반영, 다른 프로세스와 공유 가능 |
| file-backed + MAP_PRIVATE | 실행 파일의 text/data 매핑 | 읽기는 파일에서, write 시 COW 복사 |
| anonymous + MAP_SHARED (fd=-1) | 프로세스 간 공유 메모리 | fork 이후에도 공유, IPC 용도 |
| anonymous + MAP_PRIVATE | malloc 의 큰 요청, 스택 확장 | 처음 접근 시 zero-fill |

`munmap()` 으로 매핑을 해제하고, `msync()` 로 dirty 한 매핑을 파일에 flush 한다.

### zero-fill-on-demand 의 우아함

`mmap(NULL, 4MB, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0)` 은
즉시 반환된다. VMA 만 만들어질 뿐, 실제 frame 은 하나도 할당되지 않는다.

이후 그 영역에 write 가 일어나면:
- page fault 발생
- 커널이 frame 하나를 잡는다
- 그 frame 을 **0으로 채운다**
- PTE 를 세팅해서 write 를 완료시킨다

"0으로 초기화된 메모리" 를 프로그램에 돌려줄 때, OS 는 실제로 0을 복사하지 않고
**"0으로 가득 찬 영원한 페이지"** (zero page) 를 READ-ONLY 로 공유해 두고,
write 시에만 COW 로 새 frame 을 만든다. 이중의 lazy 가 쌓여 있는 셈.

## fork() — 주소 공간을 복제한다 (사실은 공유한다)

`fork()` 는 parent 의 모든 VMA 를 복제해서 child 에게 준다. 하지만 실제로는 거의 복사하지 않는다.

```
fork() 흐름:
  ① child 의 mm_struct 를 만들고 parent 의 VMA 리스트를 복제
  ② 모든 private page 에 대해 parent 와 child 양쪽 PTE 를 **read-only** 로 마크
  ③ shared 영역은 그대로 공유 (페이지 자체가 이미 공유 가능)
  ④ fork 리턴

이후 parent/child 어느 쪽이든 write 를 시도하면:
  ⑤ protection fault 발생
  ⑥ 커널이 해당 page 하나만 새 frame 에 복사
  ⑦ 두 PTE 를 R/W 로 갱신
  ⑧ 명령어 재실행
```

`fork() + exec()` 패턴이 낭비가 아닌 이유가 여기 있다.
fork 로 복제해 놓고 바로 exec 을 호출하면, 그 사이에 write 가 거의 없기 때문에 **복사 비용이 0에 가깝다**.
다음 단계인 exec 이 어차피 주소 공간을 전부 폐기할 테니까.

## brk / sbrk — 전통적인 heap 확장 방식

`malloc` 이 큰 영역을 달라고 할 때 OS 에게 내미는 카드가 둘이다.

**brk / sbrk**: 프로세스의 heap top 을 앞으로 밀어 올린다.

```
프로세스 초기:
    | ... | heap(0)  | ... |
                    ↑ brk (현재 heap 의 끝)

sbrk(1MB):
    | ... | heap(1MB)  | ... |
                        ↑ brk + 1MB
```

특징:
- 연속된 heap 한 덩어리
- 작은 할당에 유리 (경계 이동만으로 처리)
- OS 에 반환하기 어려움 (끝부분이 아니면 축소 불가)

**mmap**: 새 anonymous VMA 를 어딘가에 만든다.

특징:
- 독립된 영역
- 큰 할당에 유리
- `munmap` 으로 즉시 OS 에 반환 가능

glibc malloc 은 기본적으로 **128KB 이상이면 mmap, 이하면 sbrk** 로 처리한다.
이 경계는 `mallopt(M_MMAP_THRESHOLD)` 로 조정 가능하다.

## shared library — 물리 프레임 하나로 수백 프로세스가 libc 를 쓴다

`/lib/x86_64-linux-gnu/libc.so.6` 의 `.text` 섹션은 DRAM 에 물리적으로 **딱 한 벌** 만 있다.
그걸 모든 프로세스가 각자의 page table 로 연결해서 본다.

```
프로세스 A 의 VA 0x7f1000 ─┐
프로세스 B 의 VA 0x7f2000 ─┼──→ 같은 물리 frame (libc 의 printf 코드)
프로세스 C 의 VA 0x7f5000 ─┘
```

이 구조 덕분에 수백 개 프로세스가 libc 를 쓰는 서버에서도 메모리가 폭발하지 않는다.
그리고 이것이 가능한 이유는 `PIE` (Position-Independent Executable) 와 `ASLR` 덕분이다.
libc 는 어느 주소에 로드되든 상관없이 동작하도록 컴파일되어 있어서, 각 프로세스마다 다른 VA 에 매핑되어도 문제없다.

## minidb 에서의 대응 — DB 파일이 곧 ELF, pager 가 곧 loader

minidb 를 이 관점에서 재해석해 보면 재밌는 그림이 나온다.

| OS 개념 | minidb 대응 |
|---------|-------------|
| ELF 파일 | `.db` 파일 |
| ELF 헤더 | page 0 의 `db_header_t` (magic, version, page_size, schema) |
| ELF segment | page 타입별 구간 (HEADER / HEAP / LEAF / INTERNAL / FREE) |
| execve 가 ELF 를 매핑 | `pager_open()` 이 DB 파일을 열고 frame cache 준비 |
| demand paging | `pager_get_page()` 가 cache miss 시 pread |
| mmap file-backed + private | frame 에 page 를 읽고 메모리에서만 수정 |
| msync / write-back | `pager_flush_all()` 이 dirty frame 을 pwrite |
| sbrk | `pager_alloc_page()` 가 DB 파일을 뒤로 확장 |
| munmap / free page | free page list 에 반환 (나중 alloc 에서 재사용) |

흥미로운 포인트는 minidb 가 `mmap()` 을 **사용하지 않는다** 는 점이다.
그 대신 `pread/pwrite` 로 직접 관리한다. 왜 그랬는가?

**선택 이유:**
1. flush 타이밍을 **내가** 제어하고 싶었다. `msync()` 를 부르는 대신, dirty watermark 를 내가 정한다.
2. page fault 를 OS 에 맡기면 언제 발생할지 예측 불가. pager 안에서 `pread()` 를 명시적으로 부르면 시점이 예측 가능하다.
3. 학습 목적 — pager 의 frame cache / LRU / dirty tracking 을 직접 구현해 보는 게 이번 주의 핵심 목표였다.

production 급 DB 는 상황에 따라 `mmap` 을 쓰기도 한다 (예: SQLite 일부 모드, LMDB).
반대로 PostgreSQL 은 `pread/pwrite` 기반 shared buffer 를 쓴다.
이번 주 이 논쟁의 양쪽을 직접 체험해 본 느낌이었다.

## 이번 주 인사이트 — "매핑은 예약, 접근이 실행"

execve / fork / mmap 세 syscall 을 한 줄로 요약하면 다음과 같다.

- **execve**: "이 프로세스의 주소 공간을 이 ELF 로 재구성해라" — 대부분 아직 매핑만 하고 비워 둔다.
- **fork**: "이 주소 공간을 복제해라" — 거의 공유하고, write 시점까지 미룬다.
- **mmap**: "이 VA 범위를 이 파일/메모리에 연결해라" — 접근 전까지는 frame 을 안 준다.

셋 다 공통 패턴은 **"즉시 할 일을 최소화하고, 실제 접근 때까지 미룬다"** 는 것이다.
page fault 가 이 미루기의 회수 메커니즘이다.

minidb 에서도 같은 패턴을 적용했다.
`pager_alloc_page()` 는 page_id 를 즉시 발급하지만, 실제로 메모리를 채우거나 디스크에 쓰는 건
**처음 사용될 때** (frame 에 할당될 때) 와 **flush 될 때** 까지 미룬다.
이 "즉시 하지 않는 용기" 가 시스템 프로그래밍에서 가장 어려운 교훈 중 하나였다.

## 다음 글로의 연결

지금까지는 OS 가 프로세스에게 제공하는 메모리 레이아웃을 봤다.
그 중 `heap` 영역 안에서 `malloc / free` 가 실제로 어떤 자료구조와 정책으로 동작하는지는 아직 안 봤다.
다음 글은 heap allocator 의 내부를 파고든다.

→ [06. malloc 내부 들여다보기 — heap allocator의 자료구조와 정책](./06-heap-allocator-deep-dive.md)
