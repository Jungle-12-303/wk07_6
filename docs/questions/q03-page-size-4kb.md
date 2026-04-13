# Q3. 페이지 크기 4KB의 근거 — Goldilocks 원리

> CSAPP 9.3 | 1부. 주소와 공간 | 심화

## 질문

1. 페이지 크기가 너무 작을 때/클 때의 단점을 각각 서술하시오.
2. 4KB가 "적당한" 이유를 내부 단편화, 페이지 테이블 크기, 디스크 I/O 관점에서 설명하시오.
3. [검증] 64-bit 시스템, 페이지 크기 4KB일 때 단일 페이지 테이블의 최대 엔트리 수와 테이블 크기를 계산하시오.

## 답변

> 1. 페이지의 크기가 너무 작으면 디스크에 접근해야하는 횟수가 많아져 성능 저하를 유발할 수 있다. 페이지의 크기가 너무 크면 디스크에서 불필요한 데이터를 포함해서 가져올 가능성이 높아진다.

> 3. 

## 연결 키워드

- [Fragmentation](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#7-동적-할당--mallocfree의-내부)
- [Multi-Level Page Table](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
