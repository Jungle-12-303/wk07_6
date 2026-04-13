# Q5. Page fault의 분류 — '에러'가 아니라 '이벤트'

> CSAPP 9.3 | 2부. 페이지 폴트 | 기본

## 질문

1. Page fault가 "에러"가 아니라 "이벤트"인 이유를 설명하시오.
2. Minor / Major / Invalid fault를 각각 구분하시오.
3. [검증] Major fault 발생 시 커널의 처리 순서를 victim 선택부터 명령어 재실행까지 나열하시오.

## 답변

### 이호준

1. Page fault는 발생하면 프로세스를 종료시켜야 하는 에러가 아니라, 발생하면 알맞게 핸들링해줘야하는 예외 이벤트이기 때문이다.
   1. 한정된 메모리 용량을 활용하기 위해서 (Demand Paging)방식을 사용하고, 페이지 폴트는 자주 발생하도록 설계 되어있다.
   2. 프로그램에서 발생하는 문제가 아니다. 프로그램은 제대로된 가상 메모리 주소로 요청했고, 코드를 잘못짜서 발생한 문제가 아니기 때문에, 에러를 띄워 강제로 종료할 이유가 없다.

2. Minor / Major / Invalid fault를 각각 구분하시오

| 종류          | 설명                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Minor         | 페이지가 물리 메모리 어딘가에는 있지만 페이지 테이블에 매핑이 안 된 경우 (디스크 I/O 불필요)<br>ex) COW(Copy-on-Write) 부모 / 자식 프로세스가 있는 경우, 메모리 효율을 위해 자식 프로세스와 부모 프로세스는 공유 메모리를 사용함, 자식 프로세스가 공유 메모리 데이터를 Write 할려는 순간 Page fault가 발생하고, 공유 메모리에 있던 데이터를 Copy 하고 난 뒤 다시 Write를 실행. COW 뿐만 아니라 여러 프로세스가 같은 라이브러리를 공유하거나 할 때도 비슷한 원리로 minor fault가 발생할 수 있음 |
| Major         | 페이지가 디스크(swap space등)에 있어서 실제로 디스크 I/O가 필요한 경우                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Invalid Fault | 접근 자체가 불법 (잘못된 주소, 권한 위반 등) -> `SIGSEGV` 발생 (진짜 에러)                                                                                                                                                                                                                                                                                                                                                                                                                     |

3. \[검증] Major fault 발생 시 커널의 처리 순서를 victim 선택부터 명령어 재실행까지 나열하시오.
   1. Page Fault 발생:프로세스가 현재 물리 메모리에 없는 페이지에 접근하면 MMU가 하드웨어 트랩을 발생시켜 제어권을 커널로 이동
   2. 커널 페이지 폴트 헨들러 진입: 커널은 faulting address를 확인하고 Virtual Memory Area를 검색해서 유요한 접근인지 검사. 잘못된 접근이면 `SIGEGV`를 발생
   3. 빈 물리 프레임 탐색: Free frame list를 확인하여 즉시 사용 가능한 프레임이 있는지 조사
   4. Victim 페이지 선택 (빈 프레임이 없을때): 페이지 교체 알고리즘 (LRU, Clock, NRU 등)을 실행하여 교체할 Victim 페이지를 선택
   5. Victim 페이지 처리 및 PTE 무효화: Dirty bit가 1이면 해당 페이지를 스왑 영역(디스크)에 기록. 이후 Victim 페이지의 PTE(Page Table Entry)의 present bit를 0으로 설정하고 TLB에서 해당 항목을 무효화.
   6. 요청 페이지를 디스크에서 읽기: 확보된 물리 프레임에 필요한 페이지를 디스크 (스왑 영역 또는 파일)에서 I/O로 로드. 프로세스는 `sleep` 상태
   7. 페이지 테이블 갱신: I/O가 완료되면 해당 페이지의 PTE를 새 물리 프레임 주소로 갱신하고 present bit를 1로 설정
   8. TLB 갱신: 새로 매핑된 가상->물리 주소 쌍을 TLB에 적재
   9. 폴트 발생 명령어 재실행: 프로세스를 `ready` 상태로 전환하고, 폴트를 일으켰던 명령어를 처음부터 재실행. TLB/

- [](./imgs/Pasted%20image%2020260413134339.png)

### TLB(Translation Lookaside Buffer)

가상 주소 -> 물리 주소 변환 결과를 캐싱하는 하드웨어 캐시

- [](./imgs/Pasted%20image%2020260413135609.png)

### 최우녕

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
