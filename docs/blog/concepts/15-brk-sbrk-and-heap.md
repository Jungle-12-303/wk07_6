# brk·sbrk와 힙 영역의 확장

프로세스의 **힙(Heap)** 은 동적 할당을 위한 영역이다.
그런데 흥미로운 점은, 커널에게 "힙"이라는 자료구조 자체가 없다는 것이다.

커널은 단지 데이터 영역 바로 위에 있는 한 경계—**program break**—의 값을 유지할 뿐이다.
이 값을 위로 밀면 힙이 커진다.
아래로 당기면 줄어든다.
그 값을 움직이는 시스템 콜이 **`brk`** 와 **`sbrk`** 다.

## 힙이란 무엇인가

힙은 BSS 바로 위 가상 주소에서 시작해 **`mm_struct->start_brk`** 부터 **`mm_struct->brk`** 까지 이어지는 단일 VMA다. 이 VMA의 권한은 `rw-` 이고, `vm_file`은 `NULL`(익명 매핑)이다.

```
  ┌──────────────────────────────┐
  │           ...                 │
  ├──────────────────────────────┤  ← mm_struct->brk  (움직이는 경계)
  │         Heap VMA              │
  │  (rw-, anonymous)              │
  ├──────────────────────────────┤  ← mm_struct->start_brk
  │           BSS                 │
  │           Data                │
  │           Text                │
  └──────────────────────────────┘
```

`brk`가 `start_brk`와 같은 상태에서는 힙의 크기가 0이다. `brk`를 위로 밀면 그 만큼 힙 VMA의 크기가 늘어나고, 새로 생긴 가상 주소가 프로세스에 쓸 수 있게 된다.

## brk와 sbrk의 시그니처

```c
int   brk(void *addr);        // break를 addr로 설정
void *sbrk(intptr_t incr);    // break를 incr만큼 이동, 이전 값 반환
```

- `brk(addr)`: break를 `addr`로 설정한다. 성공 시 0, 실패 시 -1.
- `sbrk(incr)`: break를 `incr` 바이트만큼 이동한다.
  양수는 확장, 음수는 축소, 0은 현재 break 조회.
  **이전** break 값을 반환한다.
  즉 `sbrk(0)`은 "지금 break가 어디 있는가"를 묻는 관용구다.

이 두 호출은 커널 내부에서 본질적으로 `mm_struct->brk`를 움직이는 연산이다.
경계가 위로 움직이면 힙 VMA가 확장되고, 아래로 움직이면 축소된다.

확장된 페이지들은 요구 페이징으로 처리된다.
VMA만 커질 뿐, 실제 물리 프레임은 처음 접근 시 0-채움 프레임이 배정된다.

## 힙 확장 과정

```mermaid
sequenceDiagram
    participant App
    participant Libc as glibc malloc
    participant K as Kernel
    participant PT as 페이지 테이블
    App->>Libc: malloc(1KB)
    Libc->>K: brk(현재_break + 4KB)
    K->>K: 힙 VMA 끝을 +4KB 확장
    K-->>Libc: 성공
    Libc-->>App: payload 포인터 반환
    App->>App: *p = 1; (첫 접근)
    App->>PT: 가상 주소 접근 → PTE.P=0
    PT->>K: Page Fault
    K->>K: 0으로 채운 프레임 할당, PTE 업데이트
    K-->>App: 명령 재실행 (쓰기 성공)
```

`brk`로 받은 메모리는 커널 입장에서는 4 KB 단위로 늘어나는 VMA 영역이다.
glibc `malloc` 입장에서는 자신이 관리할 전체 풀이다.

`malloc`은 한 번 `brk`로 큰 덩어리를 받아 내부 자료구조로 쪼개 관리한다.

## 힙 = `brk` 공간, `malloc` = 할당기

여기서 혼동을 풀어야 한다.

**힙**은 주소 공간의 한 영역이다.
**`malloc`** 은 그 영역을 조각내 사용자에게 나눠 주는 **할당기**다.
두 역할은 다르다.

- **커널**: 힙의 경계(break)만 관리한다. 경계 안쪽이 어떻게 쪼개져 쓰이는지는 모른다.
- **glibc(`malloc`/`free`)**: break 안쪽을 블록(header + payload + footer)으로 쪼개고, free 리스트를 관리한다.

그래서 `malloc`은 `brk`를 직접 부르지 않는다.
내부적으로 필요한 만큼만 호출한다.

작은 할당을 받을 때마다 시스템 콜을 부르면 성능이 망가진다.
그래서 한 번의 `brk`로 크게 확장하고, 내부 자료구조에서 잘라서 쓴다.

## `brk`의 한계 — 왜 큰 할당은 mmap으로 가는가

`brk`로만 힙을 관리하면 두 가지 한계에 부딪힌다.

**1. 반환의 어려움**

힙은 단일 연속 VMA다.
중간에 큰 free 블록이 있어도 `brk`를 아래로 당기려면 상단의 모든 블록이 free 상태여야 한다.
한 블록만 상단 근처에 살아 있어도 그 아래 수십 MB를 돌려줄 수 없다.

```
 brk ──▶ | living block |                ← 이것 때문에
         |   free 50MB   |                   brk를 당길 수 없음
         |   free 20MB   |
 start_brk ─▶
```

**2. 스레드 경합**

`brk`는 프로세스 전체의 단일 경계다.
여러 스레드가 동시에 `malloc` 할 때 잠금이 필요하다.

그래서 glibc의 `malloc`은 **큰 할당(기본 128 KB 이상)** 에 대해 `brk` 대신 **anonymous private `mmap`** 을 사용한다.
`mmap`으로 받은 영역은 개별 VMA이므로 `free` 시점에 `munmap`으로 통째로 반환할 수 있다.
힙 단편화의 상단 문제를 우회하는 방법이다.

## sbrk(0)로 break 위치 확인

```c
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    printf("initial break: %p\n", sbrk(0));
    void *p = malloc(1024);
    printf("after malloc(1KB): %p\n", sbrk(0));
    free(p);
    printf("after free:        %p\n", sbrk(0));

    void *big = malloc(1 * 1024 * 1024); // 1MB
    printf("after malloc(1MB): %p\n", sbrk(0));
    free(big);
    printf("after free(1MB):   %p\n", sbrk(0));
    return 0;
}
```

첫 번째 `malloc(1KB)` 이후 break는 수 KB 또는 수십 KB 증가할 수 있다.
`malloc`이 여유 있게 한 번에 확장하는 것이다.

`free` 후에는 대체로 break가 원위치로 돌아오지 않는다.
블록은 free list에 들어갈 뿐이다.

큰 할당(1 MB)은 `mmap` 경로로 가므로 break에 변화가 없을 수 있다.
이 한 번의 실험으로 glibc `malloc`의 전략을 대략 엿볼 수 있다.

## brk가 실패할 때

`brk(addr)`가 실패하는 주된 원인은:

- 주소 공간 자체가 부족함 (`brk`가 다른 VMA, 예를 들어 공유 라이브러리 매핑과 충돌)
- `RLIMIT_DATA` 같은 자원 한도 초과
- 시스템 전체의 물리 메모리/스왑 고갈 (요청이 너무 크면)

실패하면 `brk`는 -1을, `sbrk`는 `(void *) -1`을 반환한다. 이 때 `malloc`은 `mmap` 경로로 전환하거나 `NULL`을 반환한다.

`brk`와 `sbrk`는 단 하나의 값—program break—을 움직이는 가장 단순한 메모리 시스템 콜이다.

그런데 그 한 값이 힙 VMA의 상단을 정의한다.
그 영역 위에 `malloc`이 자기 자료구조를 세운다.

커널은 경계만 알고, 할당기는 경계 안을 쪼갠다.
이 역할 분담 때문에 "힙"이라는 추상이 유저 공간의 편의로 성립한다.

동시에 `brk`의 한계(반환의 어려움)가 큰 할당을 `mmap`으로 이전하게 만드는 동력이 된다.
