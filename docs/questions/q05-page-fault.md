# Q5. Page fault의 분류 — '에러'가 아니라 '이벤트'

> CSAPP 9.3 | 2부. 페이지 폴트 | 기본

## 질문

1. Page fault가 "에러"가 아니라 "이벤트"인 이유를 설명하시오.
2. Minor / Major / Invalid fault를 각각 구분하시오.
3. [검증] Major fault 발생 시 커널의 처리 순서를 victim 선택부터 명령어 재실행까지 나열하시오.

## 답변

### 최우녕

> Page fault가 "에러"가 아니라 "이벤트"인 이유를 설명하시오.

Page Fault는 CPU가 VA에 접근했는데 해당 페이지가 DRAM에 없을 때 발생하는 예외다.
이름에 "fault"가 들어있지만, 대부분의 경우 커널이 디스크에서 페이지를 가져오면
정상적으로 처리가 끝나고 명령어가 재실행된다.

Demand Paging 자체가 "필요할 때 DRAM에 올린다"는 설계이므로
Page Fault는 이 설계의 정상적인 동작 과정이다.
프로그램 버그가 아니라 운영체제가 의도한 메커니즘인 것이다.

진짜 에러는 Invalid fault(할당되지 않은 영역 접근)뿐이고,
이때는 Segmentation Fault로 프로세스가 종료된다.

> Minor / Major / Invalid fault를 각각 구분하시오.

Minor Fault: 페이지가 이미 DRAM 어딘가에 있지만 현재 프로세스의 PTE에 매핑이 안 된 경우.
디스크 I/O 없이 PTE만 갱신하면 된다.
예: fork() 후 COW(Copy-on-Write) 페이지에 쓰기 시도,
또는 다른 프로세스가 이미 올려놓은 공유 라이브러리 페이지.

Major Fault: 페이지가 DRAM에 없고 디스크(스왑)에서 읽어와야 하는 경우.
디스크 I/O가 발생하므로 수 ms가 걸려서 비용이 비싸다.

Invalid Fault: 할당되지 않은 VA에 접근한 경우.
커널이 복구할 수 없으므로 Segmentation Fault(SIGSEGV)를 보내 프로세스를 종료한다.
이것만 진짜 "에러"다.

> [검증] Major fault 발생 시 커널의 처리 순서를 victim 선택부터 명령어 재실행까지 나열하시오.

```text
전제: 프로세스가 VA 0x8000에 접근, 해당 페이지가 디스크에만 존재

━━━ Major Page Fault 처리 순서 ━━━━━━━━━━━━━━━━━

  CPU: mov 명령 → VA 0x8000 → MMU 전달
  |
  MMU: TLB MISS → 페이지 테이블 워크 → PTE 확인
  |
  ㄴ PTE: 할당됨 + DRAM에 없음 (커널 자료구조에 디스크 주소 있음)
     |
     ㄴ Page Fault 예외 발생 → 커널 모드 전환

━━━ 커널 폴트 핸들러 ━━━━━━━━━━━━━━━━━━━━━━━━━━━

  1. 빈 프레임 확인
     |
     ㄴ 빈 프레임 있음 → 3으로
     ㄴ 빈 프레임 없음 → 2로

  2. Victim 선택 (페이지 교체)
     |
     ㄴ 교체 알고리즘(LRU 등)으로 evict할 페이지 선택
     ㄴ victim이 dirty(수정됨)면 → 디스크에 write-back
     ㄴ victim의 PTE 무효화 + TLB에서 flush
     ㄴ 이제 빈 프레임 확보됨

  3. 디스크 → DRAM 로드
     |
     ㄴ 커널 자료구조에서 디스크 주소 확인
     ㄴ 해당 위치에서 페이지 데이터 읽기
     ㄴ 확보한 프레임에 복사 (~수 ms, 가장 비싼 단계)

  4. PTE 갱신
     |
     ㄴ PPN = 새로 할당한 프레임 번호
     ㄴ valid bit = 1
     ㄴ TLB에 새 엔트리 캐싱

  5. 트랩 복귀 → 명령어 재실행
     |
     ㄴ CPU가 같은 mov 명령을 다시 실행
     ㄴ 이번엔 PTE valid + DRAM에 있음 → 정상 접근

━━━ 비용 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Minor Fault: ~수 μs (디스크 I/O 없음, PTE 갱신만)
  Major Fault: ~수 ms  (디스크 I/O 포함, 수백만 사이클)
  Normal 접근: ~수 ns  (TLB HIT + 캐시 HIT)
```

## 연결 키워드

- [Page Fault Handler](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#5-커널의-역할--누가-이-모든-걸-관리하는가)
- [Swap Space](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#1-물리-세계--실제로-존재하는-것)
