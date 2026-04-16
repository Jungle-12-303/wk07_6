# minidb & 메모리 시스템 블로그 시리즈

CSAPP 9장(Virtual Memory) 학습과 **minidb**(C로 구현한 디스크 기반 SQL 엔진) 구현을 토대로 정리한 글 모음이다.

이 시리즈는 두 축으로 나뉜다.

- **concepts/** — 하나의 글이 하나의 개념만 설명한다. 내부 링크 없이 독립적으로 완결되므로, 검색으로 한 글만 유입돼도 그 자체로 이해가 끝나도록 썼다.
- **applications/** — 개념을 minidb 구현에 옮기며 의심했던 것, 실험했던 것, 확인한 결과의 기록이다.

각 글 안에서 다른 글로의 내부 링크는 넣지 않았다. 이 README만이 전체 시리즈의 유일한 지도 역할을 한다.

---

## concepts — 개념 완결형

| #  | 제목 | 주제 영역 |
|----|------|-----------|
| 01 | [메모리 계층 구조와 접근 비용](./concepts/01-memory-hierarchy.md) | CPU · Cache · DRAM |
| 02 | [캐시 라인과 공간·시간 지역성](./concepts/02-cache-line-and-locality.md) | Cache line · Locality · False sharing |
| 03 | [가상 메모리의 세 가지 역할: 캐싱, 관리, 보호](./concepts/03-virtual-memory-three-problems.md) | Caching · Management · Protection |
| 04 | [페이지 테이블과 PTE의 구조](./concepts/04-page-table-and-pte.md) | Page · Page frame · PTE |
| 05 | [다단계 페이지 테이블과 MMU의 주소 변환](./concepts/05-multi-level-page-table-and-mmu.md) | Multi-level paging · MMU · CR3 |
| 06 | [TLB와 주소 변환 캐싱](./concepts/06-tlb.md) | TLB · Hit/Miss · Context switch |
| 07 | [Page Fault와 Demand Paging](./concepts/07-page-fault-and-demand-paging.md) | Minor/Major/Invalid fault · Lazy alloc |
| 08 | [Copy-on-Write의 동작 원리](./concepts/08-copy-on-write.md) | COW · Write fault · Page sharing |
| 09 | [특권 모드와 System Call의 경계](./concepts/09-user-kernel-mode-and-syscall.md) | Privilege · Trap · syscall ABI |
| 10 | [프로세스 주소 공간 레이아웃](./concepts/10-process-address-space.md) | Stack · Heap · BSS · Data · Text |
| 11 | [task_struct, mm_struct, VMA의 계층 구조](./concepts/11-task-struct-mm-struct-vma.md) | Linux kernel data structures |
| 12 | [execve와 ELF 프로그램 로딩](./concepts/12-execve-and-program-loading.md) | ELF · Loader · argv/envp |
| 13 | [fork와 Copy-on-Write의 결합](./concepts/13-fork-and-cow.md) | Process creation · PTE duplication |
| 14 | [mmap의 네 가지 매핑 조합](./concepts/14-mmap-four-combinations.md) | File-backed · Anonymous · MAP_SHARED · MAP_PRIVATE |
| 15 | [brk·sbrk와 힙 영역의 확장](./concepts/15-brk-sbrk-and-heap.md) | Program break · Heap growth |
| 16 | [Heap Allocator의 내부 구조](./concepts/16-heap-allocator-internals.md) | Block header · Free list · Split/Coalesce · Fragmentation |
| 17 | [메모리 버그의 근본 원인: 포인터의 메타데이터 부재](./concepts/17-memory-bugs-and-root-cause.md) | UAF · Double free · OOB · Leak · ASan |
| 18 | [GC와 런타임 메모리 관리 전략](./concepts/18-gc-runtime-memory-management.md) | Mark-and-sweep · Ref count · Conservative GC |

## applications — minidb에서 의심하고 시험한 것

| #  | 제목 | 다루는 관찰 |
|----|------|-------------|
| 01 | [DB 블록, OS 페이지, B+ tree 노드의 단위 통일](./applications/01-db-page-equals-os-page-equals-bplus-node.md) | 단위 통일의 근거 |
| 02 | [프레임 캐시 구현: LRU, pin count, dirty bit](./applications/02-implementing-frame-cache-lru.md) | LRU · pin count · dirty bit |
| 03 | [`page_type` 태그 기반 다형성](./applications/03-page-type-tag-polymorphism.md) | C에서의 추상화 발견 |
| 04 | [POD 구조체와 `memcpy` 기반 직렬화](./applications/04-memcpy-replaces-serialization.md) | POD 구조체 · packed |
| 05 | [슬롯 페이지 레이아웃과 내부 단편화 측정](./applications/05-slotted-page-internal-fragmentation.md) | 페이지 활용률 측정 |
| 06 | [프레임 캐시와 OS 페이지 캐시의 이중 캐싱 실험](./applications/06-pread-pwrite-double-cache-doubt.md) | 성능 실험 |
| 07 | [페이지 경계를 고려한 B+ tree split 구현](./applications/07-bplus-tree-split-on-page-boundary.md) | 알고리즘 × 물리 단위 |
| 08 | [메모리 버그 재현과 탐지 도구 비교](./applications/08-reproducing-memory-bugs-on-purpose.md) | UAF · Double free 재현 |
| 09 | [C의 절차적 추상화와 메모리 레이아웃 통제](./applications/09-why-c-is-procedural.md) | OOP 배경에서의 전환 |
| 10 | [하드웨어에서 SQL까지: 4KB 페이지를 중심으로 한 수직 통합](./applications/10-vertical-integration-retrospective.md) | 주간 회고 |

---

## 작성 원칙

1. 개념 글은 **내부 링크 없음**. 필요한 용어는 2~3문장으로 그 글 안에서 재설명한다.
2. 개념 글은 **강한 선언문**으로 시작한다. "무엇이다 → 왜 필요하다 → 어떻게 동작한다 → 한계는 무엇인가"의 순서.
3. 용어 표기는 첫 등장 시 **한글(English)** 형식.
4. 다이어그램은 Mermaid를 기본으로 하되, 메모리 레이아웃·바이트 배치·시각적 피라미드는 ASCII 박스 또는 SVG/PNG로 보완한다. 에셋은 `assets/`.
5. 응용 글은 "결정 → 시도 → 관측 → 결론" 순서. 감정·회고를 허용한다.

## 참고한 출처

- Randal E. Bryant, David R. O'Hallaron, *Computer Systems: A Programmer's Perspective*, 3rd ed. — 9장.
- Linux Kernel Source (`mm/`, `include/linux/mm.h`, `Documentation/vm/`).
- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A — Chapter 4 (Paging).
