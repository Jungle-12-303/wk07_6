# Q1. VA와 PA의 정체 — '번역'이지 '저장'이 아니다

> CSAPP 9.1 | 1부. 주소와 공간 | 기본

## 질문

1. VA(가상주소)와 PA(물리주소)의 차이를 설명하시오.
2. "가상 메모리는 저장 공간이 아니라 주소 변환 체계다"를 근거와 함께 서술하시오.
3. [검증] 프로그램이 포인터 `0x4000`을 읽을 때, 이 값이 물리 메모리의 어떤 주소에 대응되는지 결정하는 주체와 과정을 설명하시오.

## 답변

> 1. VA는 VM(가상 메모리)에서 사용하는 논리적 주소이고, PA는 실제 데이터가 저장되어 있는 RAM의 주소이다.

> 2. VM은 메인 메모리의 추상화로 일종의 시스템이다(p.772).

> 3. 프로그램이 VA인 `0x4000`를 읽으면, MMU가 PTE 위치를 계산해 os에서 관리하는 Page Table에서 PTE의 위치에 저장되어 있는 값을 읽고 PA를 생성한다.

## 연결 키워드

- [Address Space](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#3-주소-공간--프로세스가-보는-세계)
- [MMU](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#1-물리-세계--실제로-존재하는-것)
- [Page Table & Translation](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
