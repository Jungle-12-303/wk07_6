# Q7. vm_area_struct vs 페이지 테이블 — '의미' vs '번역'

> CSAPP 9.7 | 3부. 보호와 공유 | 기본

## 질문

1. vm_area_struct(VMA)와 페이지 테이블의 역할 차이를 설명하시오.
2. 같은 VA에 대해 VMA는 있는데 PTE가 없을 수 있는 상황을 서술하시오.
3. [검증] 프로세스가 특정 VA에 접근했을 때, 커널이 VMA와 페이지 테이블을 각각 언제 참조하는지 설명하시오.

## 답변

### 최우녕

> vm_area_struct(VMA)와 페이지 테이블의 역할 차이를 설명하시오.

VMA(vm_area_struct)는 커널이 "이 프로세스의 VA 어디부터 어디까지가 무슨 용도인가"를
기록해둔 자료구조다. 권한(읽기/쓰기/실행), 매핑 대상(파일 or 익명), 범위 등
**의미(semantic)** 정보를 담고 있다.

페이지 테이블은 MMU가 VA → PA 변환을 수행하기 위한 자료구조다.
VPN을 PPN으로 **번역(translation)** 하는 게 유일한 목적이다.

```text
  VMA (커널 자료구조, SW)
  ㄴ "VA 0x4000~0x8000은 read-write, 힙 영역이다"
  ㄴ 의미, 권한, 범위를 기록

  페이지 테이블 (MMU가 읽는 HW 구조)
  ㄴ "VPN 4 → PPN 0xA3"
  ㄴ 주소 변환만 수행
```

VMA는 커널만 참조하고, 페이지 테이블은 MMU(하드웨어)가 직접 읽는다.

> 같은 VA에 대해 VMA는 있는데 PTE가 없을 수 있는 상황을 서술하시오.

Demand Paging이 바로 이 상황이다.
프로세스가 malloc이나 mmap으로 메모리를 요청하면
커널은 VMA를 만들어서 "이 VA 범위는 유효하다"고 기록한다.
하지만 실제로 DRAM에 올리지는 않기 때문에 PTE는 아직 없다(혹은 invalid).

프로세스가 해당 VA에 처음 접근하면 Page Fault가 발생하고,
커널이 폴트 핸들러에서 VMA를 확인해 "아, 이 영역은 유효하니까 페이지를 할당하자"라고
판단한 뒤 DRAM 프레임을 배정하고 PTE를 만든다.

> [검증] 프로세스가 특정 VA에 접근했을 때, 커널이 VMA와 페이지 테이블을 각각 언제 참조하는지 설명하시오.

```text
━━━ 정상 접근 (PTE 있음) ━━━━━━━━━━━━━━━━━━━━━━━

  CPU: VA 접근 → MMU
  |
  ㄴ TLB or 페이지 테이블 워크 → PPN 획득 → PA → 데이터 반환
  ㄴ VMA는 참조하지 않음 (HW만으로 완료)

━━━ Page Fault 발생 (PTE 없음) ━━━━━━━━━━━━━━━━━

  CPU: VA 접근 → MMU → PTE invalid → Page Fault
  |
  커널 폴트 핸들러 진입
  |
  1단계: VMA 참조
  |
  ㄴ 해당 VA가 속하는 VMA가 있는가?
     |
     ㄴ VMA 없음 → 할당된 적 없는 영역 → Segfault (종료)
     ㄴ VMA 있음 → 2단계로
  |
  2단계: 권한 확인
  |
  ㄴ VMA의 권한과 접근 유형이 일치하는가?
     |
     ㄴ 읽기 전용 영역에 쓰기 시도 → Protection Fault (종료)
     ㄴ 권한 일치 → 3단계로
  |
  3단계: 페이지 할당 + PTE 생성
  |
  ㄴ DRAM 프레임 할당
  ㄴ 필요시 디스크에서 데이터 로드
  ㄴ 페이지 테이블에 PTE 기록 (VPN → PPN)
  ㄴ 명령어 재실행 → 이번엔 PTE가 있으므로 정상 접근

━━━ 정리 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  페이지 테이블: 항상 먼저 참조 (MMU가 HW로)
  VMA: Page Fault가 발생했을 때만 참조 (커널이 SW로)

  ㄴ 페이지 테이블 = "번역 사전" (빠르게 VA→PA 변환)
  ㄴ VMA = "토지 대장" (이 땅이 누구 것이고 뭘 할 수 있는지)
```

## 연결 키워드

- [vm_area_struct](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#5-커널의-역할--누가-이-모든-걸-관리하는가)
- [Page Table](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
