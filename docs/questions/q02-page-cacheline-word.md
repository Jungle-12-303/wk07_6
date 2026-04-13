# Q2. 페이지·캐시라인·워드 — 계층이 다르다

> CSAPP 9.1 | 1부. 주소와 공간 | 기본

## 질문

1. 페이지(4KB), 캐시라인(64B), 워드(4/8B) 각각이 속한 메모리 계층을 설명하시오.
2. 세 단위의 크기가 다른 이유를 locality 관점에서 서술하시오.
3. [검증] CPU가 주소 `0x1234`의 int를 읽을 때, 페이지·캐시라인·워드 단위로 어떤 일이 일어나는지 순서대로 설명하시오.

## 답변

페이지는 Disk ↔ DRAM 사이에서 움직이는 VM 단위임.
캐시라인은 DRAM ↔ Cache 사이에서 움직이는 hardware cache 단위임.
워드는 CPU가 register/ALU에서 다루기 좋은 기본 데이터 단위임.
크기가 다른 이유는 locality가 계층마다 다르게 작동하기 때문임.
페이지는 disk I/O 비용 때문에 크게, 
캐시라인은 spatial locality를 살릴 만큼만, 
워드는 CPU 연산과 정렬 기준에 맞게 더 작게 잡음.

주소 0x1234의 int를 읽을 때는 먼저 VA→PA 주소 번역이 일어나고, 
해당 page가 non-resident면 page fault로 disk→DRAM 적재, 
resident라면 PA로 cache line 조회, 
cache miss면 DRAM에서 cache line 적재, 
마지막으로 그 line 안의 필요한 word/bytes를 register로 읽어 연산함.

키워드: page, cache line, word, VM, cache hierarchy, locality, spatial locality, temporal locality, TLB, MMU, VA→PA, page fault, resident, DRAM, disk, register

## 연결 키워드

- [Cache](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#1-물리-세계--실제로-존재하는-것)
- [DRAM](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#1-물리-세계--실제로-존재하는-것)
