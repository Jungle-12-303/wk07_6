# CSAPP Ch9 + SPL 학습 블로그 시리즈

CSAPP 9장 *Virtual Memory* 를 학습하며 정리한 `csapp-ch9-keyword-tree.md` 를 뼈대 삼아,
한 주 동안의 학습 기록과 minidb(SPL 엔진) 구현 경험을 블로그 포스트 단위로 정리한 시리즈.

> 주제 구분은 키워드 트리의 대분류를 따랐다.
> 각 포스트는 "학습 회고 → CSAPP 이론 → minidb 구현" 의 3단 구조로 작성했다.

## 이번 주 학습의 큰 그림

```
하드웨어 (CPU / 캐시 / DRAM)
  ↓
운영체제 (VM / MMU / 커널)
  ↓
런타임 / allocator (malloc / free)
  ↓
응용 프로그램 (SPL, B+ tree)
```

이번 주는 이 4개 계층을 위에서 아래로 한 번, 아래에서 위로 한 번 왕복한 주간이었다.
`csapp-ch9-keyword-tree.md` 에서 꼭짓점에 있는 `Memory Systems` 가
코드 한 줄(`SELECT * FROM users WHERE id = 500`)이 실행되기까지
CPU 레지스터부터 디스크까지 몇 개의 계층을 거쳐야 하는지를 알려줬고,
minidb를 구현하면서 이 계층들을 직접 쌓아봤다.

## 포스트 목록

| # | 제목 | 주제(트리 대분류) | 핵심 키워드 |
|---|------|-------------------|-------------|
| 01 | [물리 세계 — CPU, 캐시, DRAM이 실제로 움직이는 방식](./01-physical-world-cpu-cache-dram.md) | CPU / memory hierarchy | register, cache line, page, locality |
| 02 | [가상 메모리가 풀어내는 세 가지 문제](./02-why-virtual-memory.md) | Virtual Memory | caching / management / protection |
| 03 | [주소 변환 파이프라인 — MMU, TLB, Page Table](./03-address-translation-pipeline.md) | Address translation | MMU, TLB, multi-level page table, PTE |
| 04 | [프로세스가 보는 세계와 커널의 역할](./04-process-address-space-and-kernel.md) | Process address space / Kernel | VMA, task_struct, page fault handler |
| 05 | [프로그램이 메모리를 얻는 방법 — execve, fork, mmap](./05-program-loading-and-mapping.md) | Program loading / mapping | ELF, COW, file-backed / anonymous |
| 06 | [malloc 내부 들여다보기 — heap allocator의 자료구조와 정책](./06-heap-allocator-deep-dive.md) | Heap allocation | header, free list, split/coalesce, placement |
| 07 | [C가 프로그래머에게 떠넘긴 것 — 메모리 버그와 Undefined Behavior](./07-memory-bugs-and-c-tax.md) | Undefined Behavior / memory bug | UAF, double free, OOB, leak |
| 08 | [GC와 런타임, 그리고 C에서의 추상화 — 클래스 없이 타입을 만드는 법](./08-runtime-gc-and-c-abstraction.md) | Runtime memory management / C abstraction | mark-and-sweep, page-as-type |
| 09 | [minidb 회고 — 하드웨어 → OS → SQL까지 메모리로 연결되는 한 주](./09-minidb-retrospective.md) | 전체 종합 회고 | page fit, B+ tree on disk, memory-aware SQL |

## 이번 주 가장 인상 깊었던 두 가지

**1. 하드웨어 → OS → SQL 까지 하나의 메모리 최적화 축이 존재한다**

CSAPP 9장의 `malloc` / `realloc` / `free` 구현과, 그 아래의 page·cache·TLB 를 공부하면서
"메모리 접근 비용" 이라는 단일 축이 하드웨어·OS·소프트웨어 전 계층을 관통하고 있다는 걸 느꼈다.
minidb의 B+ tree 를 구현할 때 노드 크기를 `sysconf(_SC_PAGESIZE)` 로 잡고,
page 하나에 모든 것을 담기로 결정한 순간 그 축이 구체적인 코드로 연결됐다.
알고리즘이 요구하는 탐색 구조(B+ tree)와 하드웨어가 선호하는 접근 단위(page)가
정확히 같은 단위로 맞물렸을 때, 이론과 구현이 같은 방향으로 수렴하는 경험을 했다.

**2. C에서도 추상화를 할 수 있다 — 다만 그 단위가 "class"가 아니라 "page"다**

C++, C#, Java로 객체지향을 먼저 익힌 사람이 C를 처음 다루면,
class·interface·상속 같은 추상화 도구가 없어서 답답하다.
그런데 minidb에서 모든 페이지의 **첫 4바이트를 `page_type` 플래그**로 쓰는 순간,
"같은 4096바이트 블록을 헤더의 플래그로 다형성을 부여하는" 추상화가 가능했다.
타입이라는 식별자, 상속이라는 관계로 추상화하던 경험과 달리,
**메모리 블록과 그 블록 안의 플래그**로 추상화하는 새로운 감각을 얻었다.
이 인사이트는 포스트 08 에서 따로 정리했다.

## 문서 간 연결

키워드 트리의 수직 축을 기준으로 각 포스트가 이어진다.

```
[01 물리 세계]
      ↓ CPU가 주소를 계산하면
[02 왜 가상 메모리인가]
      ↓ VA를 PA로 바꿔야 하므로
[03 주소 변환 파이프라인]
      ↓ 그 VA는 어떤 공간에 속하는가
[04 프로세스 주소 공간과 커널]
      ↓ 그 공간은 어떻게 만들어지는가
[05 프로그램 로딩과 매핑]
      ↓ 동적 객체는 어떻게 할당되는가
[06 heap allocator]
      ↓ 잘못 쓰면 어떻게 되는가
[07 메모리 버그와 C의 대가]
      ↓ 다른 언어는 이 문제를 어떻게 푸는가
[08 GC와 런타임, C 추상화]
      ↓ 이 모든 걸 실제로 쌓아본 프로젝트
[09 minidb 회고]
```
