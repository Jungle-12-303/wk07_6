# Q4. Resident vs Not Resident — '유효하지만 없다'

> CSAPP 9.3 | 2부. 페이지 폴트 | 기본

## 질문

1. PTE에서 valid bit = 1인데 물리 메모리에 없는 상태를 설명하시오.
2. "할당됨 ≠ 상주함"의 의미를 Demand Paging과 연결하여 서술하시오.
3. [검증] PTE 상태 3가지(valid+memory, valid+disk, invalid)를 각각의 의미와 함께 설명하시오.

## 답변

valid = 1 인데 물리 메모리에 없다는 상태는, 그 페이지가 논리적으로는 유효한 페이지이지만 현재는 DRAM에 상주하지 않고 disk 쪽 backing store에 있는 상태임.
할당됨 ≠ 상주함은, 어떤 가상 페이지가 프로세스 주소공간에 속해 접근 가능한 상태여도 처음부터 DRAM에 올라와 있을 필요는 없다는 뜻이며, Demand Paging은 실제 참조 시점에만 page를 메모리로 가져옴.

valid + memory는 유효하고 현재 DRAM에 resident인 상태임.
valid + disk는 유효하지만 현재는 non-resident이며 page fault 시 disk에서 가져올 수 있는 상태임.
invalid는 아예 할당되지 않았거나 접근 자체가 허용되지 않는 상태이며, 접근하면 정상 page-in이 아니라 예외/segfault 쪽으로 감.

키워드: PTE, valid bit, resident, non-resident, backing store, demand paging, page fault, legal address, invalid page, protection fault

### 최우녕

> PTE에서 valid bit = 1인데 물리 메모리에 없는 상태를 설명하시오.

가상 주소 공간에서 해당 페이지가 "할당됨" 상태인 건 맞지만,
실제 DRAM에는 올라와 있지 않고 디스크(스왑)에만 존재하는 상태다.
커널은 이 페이지의 디스크 위치를 커널 자료구조(vm_area_struct 등)에 기록해두고 있으므로
접근하는 순간 Page Fault가 발생하면 커널이 그 위치를 보고 디스크에서 DRAM으로 가져온다.

CSAPP 기준으로 이 상태의 PTE는 valid bit = 0이지만,
커널이 관리하는 자료구조에는 해당 페이지의 디스크 주소가 남아있다.
즉 "할당은 됐지만 상주하지 않는" 상태는 커널은 이 페이지를 알고 있으되
DRAM에는 없는 것이다.

> "할당됨 ≠ 상주함"의 의미를 Demand Paging과 연결하여 서술하시오.

Demand Paging은 실제로 접근할 때까지 DRAM에 올리지 않는 전략이다.
프로세스가 시작되면 커널은 가상 주소 공간을 할당하고
커널 자료구조에 디스크 위치를 기록하지만, 물리 프레임은 아직 배정하지 않는다.
이게 "할당됨" 상태다.

프로세스가 해당 VA에 실제로 접근하면 그때 Page Fault가 발생하고,
커널이 디스크에서 DRAM으로 데이터를 로드해서 "상주함" 상태로 만든다.

이렇게 하는 이유는 프로세스가 할당받은 주소 공간 전체를 실제로 쓰는 경우가 드물기 때문이다.
안 쓰는 페이지까지 미리 DRAM에 올리면 메모리 낭비가 심해진다.

> [검증] PTE 상태 3가지(valid+memory, valid+disk, invalid)를 각각의 의미와 함께 설명하시오.

```text
━━━ PTE 상태 3가지 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  1. Valid + Memory (할당됨 + 상주함)
  |
  ㄴ PTE에 PPN이 기록되어 있고 DRAM에 실제 데이터가 있음
  ㄴ TLB HIT이면 바로 접근, MISS여도 테이블 워크로 PPN 획득
  ㄴ 정상적인 메모리 접근 → Page Fault 없음

  2. Valid + Disk (할당됨 + 상주하지 않음)
  |
  ㄴ 커널이 이 VA 영역을 할당했고, 디스크 위치를 커널 자료구조에 기록해둔 상태
  ㄴ DRAM에는 없음. PTE의 valid bit = 0
  ㄴ 접근 시 Major Page Fault 발생
     |
     커널 폴트 핸들러:
     ㄴ 커널 자료구조에서 디스크 주소 확인
     ㄴ 빈 DRAM 프레임 할당 (없으면 victim evict)
     ㄴ 디스크에서 해당 페이지 로드
     ㄴ PTE 갱신 (PPN 기록 + valid = 1)
     ㄴ 명령어 재실행 → 이번엔 상태 1로 정상 접근

  3. Invalid (할당되지 않음)
  |
  ㄴ 커널이 이 VA 영역을 아예 할당한 적 없음
  ㄴ PTE가 비어있고, 커널 자료구조에도 정보 없음
  ㄴ 접근 시 → Segmentation Fault (SIGSEGV) → 프로세스 종료
  ㄴ 이건 진짜 "에러"

━━━ 요약 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  상태              DRAM에?  커널이 알고있나?  접근 시
  ─────────────────  ──────  ──────────────  ──────────────
  Valid + Memory     O       O               정상 접근
  Valid + Disk       X       O (디스크 위치)  Major Fault → 로드
  Invalid            X       X               Segfault → 종료
```

## 연결 키워드

- [PTE](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
- [Demand Paging](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#2-가상화의-이유--vm이-해결하는-3가지-문제)
