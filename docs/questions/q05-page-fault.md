# Q5. Page fault의 분류 — '에러'가 아니라 '이벤트'

> CSAPP 9.3 | 2부. 페이지 폴트 | 기본

## 질문

1. Page fault가 "에러"가 아니라 "이벤트"인 이유를 설명하시오.
2. Minor / Major / Invalid fault를 각각 구분하시오.
3. [검증] Major fault 발생 시 커널의 처리 순서를 victim 선택부터 명령어 재실행까지 나열하시오.

## 답변

Page fault는 잘못된 접근만 뜻하는 것이 아니라, 필요한 page가 아직 DRAM에 없음을 알리는 정상적인 이벤트이기 때문에 “에러”보다 페이지 적재를 유발하는 예외 이벤트로 보는 것이 정확함.
Minor / Major / Invalid fault의 구분 기준은 합법한 주소인가, page가 이미 메모리에 있나, disk I/O가 필요한가 세 가지임.

Minor fault는 접근한 VA가 합법적이고, 필요한 page도 이미 어떤 형태로는 준비되어 있지만, 현재 CPU가 바로 쓸 수 있는 PTE/매핑 상태만 아직 안 맞아서 생기는 fault임.
핵심은 disk I/O가 없다는 것임.
예를 들어 page가 이미 메모리에 있는데 PTE 갱신만 필요하거나, 다른 프로세스와 shared page를 연결하면 해결되는 경우가 여기에 가까움.
그래서 처리 비용은 주로 커널 진입, 페이지 테이블 수정, TLB 갱신 정도이고, storage access는 없음.

Major fault는 접근한 VA가 합법적이지만, 필요한 page가 현재 DRAM에 없어서 진짜로 disk 또는 swap space에서 읽어와야 하는 fault임.
즉 이 경우는 page fault handler가 victim 선택 → 필요 시 write-back → disk에서 page read → PTE 갱신 → instruction 재실행 순서로 움직임.
비용이 큰 이유는 주소 번역 자체보다 disk I/O latency가 압도적으로 크기 때문임.
Demand Paging에서 우리가 보통 떠올리는 “정상적인 page fault”는 대체로 이 Major fault 쪽임.

Invalid fault는 애초에 그 접근이 정상 page-in 대상이 아닌 경우임.
대표적으로 주소 자체가 어떤 VMA에도 속하지 않음, user mode가 kernel page 접근, read-only page에 write 시도 같은 경우임.
이때는 “page를 가져오면 해결”이 아니라 보호 위반 또는 잘못된 주소 접근이므로, 커널은 보통 SIGSEGV 같은 예외 처리로 프로세스를 종료 방향으로 보냄.

Major fault가 나면 커널은 먼저 victim page를 고르고, victim이 dirty하면 swap space/disk에 write-back함.
그다음 필요한 page를 disk에서 DRAM으로 읽어오고, PTE를 갱신한 뒤 필요하면 TLB 상태도 반영함.
마지막으로 faulting instruction을 재실행하여 이번에는 정상적으로 메모리 접근이 이루어지게 함.

키워드: page fault, event, exception, minor fault, major fault, invalid fault, page fault handler, victim page, dirty page, swap space, disk I/O, PTE update, restart instruction

## 연결 키워드

- [Page Fault Handler](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#5-커널의-역할--누가-이-모든-걸-관리하는가)
- [Swap Space](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#1-물리-세계--실제로-존재하는-것)
