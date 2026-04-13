# Q3. 페이지 크기 4KB의 근거 — Goldilocks 원리

> CSAPP 9.3 | 1부. 주소와 공간 | 심화

## 질문

1. 페이지 크기가 너무 작을 때/클 때의 단점을 각각 서술하시오.
2. 4KB가 "적당한" 이유를 내부 단편화, 페이지 테이블 크기, 디스크 I/O 관점에서 설명하시오.
3. [검증] 64-bit 시스템, 페이지 크기 4KB일 때 단일 페이지 테이블의 최대 엔트리 수와 테이블 크기를 계산하시오.

## 답변

페이지가 너무 작으면 페이지 테이블 크기와 번역 관리 비용이 커지고, disk I/O가 너무 잘게 쪼개져 비효율적임.
페이지가 너무 크면 내부 단편화와 과잉 적재(overfetch) 가 커지며, 실제로 안 쓰는 데이터까지 함께 올려 locality 활용이 나빠질 수 있음.
개념적으로 총비용은 C(P)=A/P + B·P 로 볼 수 있고, 이를 미분하면 dC/dP = -A/P² + B = 0, 따라서 균형점은 P* = √(A/B) 임.

64-bit 시스템에서 4KB page면 virtual page 수는 2^64 / 2^12 = 2^52, 
PTE가 8B라면 단일 페이지 테이블 크기는 2^52 × 8 = 2^55B = 32PB 가 되어 비현실적이므로 Multi-Level Page Table 이 필요함.

현대 시스템은 4KB만 고집하지 않고, x86-64는 보통 4KB base page를 유지하는 반면 arm64는 4KB·16KB·64KB base page 를 지원하는 등, 
TLB reach·페이지 테이블 오버헤드·워크로드 특성에 따라 더 큰 base page를 쓰는 방향도 함께 발전 중임.

키워드: Goldilocks principle, C(P)=A/P + B·P, P*=√(A/B), internal fragmentation, overfetch, page table size, 32PB, Multi-Level Page Table, TLB reach, 4KB, 16KB, 64KB, x86-64, arm64

### 최영빈

> 1. 페이지의 크기가 너무 작으면 디스크에 접근해야하는 횟수가 많아져 성능 저하를 유발할 수 있다. 페이지의 크기가 너무 크면 디스크에서 불필요한 데이터를 포함해서 가져올 가능성이 높아진다.

> 3.

## 연결 키워드

- [Fragmentation](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#7-동적-할당--mallocfree의-내부)
- [Multi-Level Page Table](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
