# Q1. VA와 PA의 정체 — '번역'이지 '저장'이 아니다

> CSAPP 9.1 | 1부. 주소와 공간 | 기본

## 질문

1. VA(가상주소)와 PA(물리주소)의 차이를 설명하시오.
2. "가상 메모리는 저장 공간이 아니라 주소 변환 체계다"를 근거와 함께 서술하시오.
3. [검증] 프로그램이 포인터 `0x4000`을 읽을 때, 이 값이 물리 메모리의 어떤 주소에 대응되는지 결정하는 주체와 과정을 설명하시오.

## 답변

VA는 프로그램이 보는 주소임.
PA는 실제 DRAM의 주소임.
가상 메모리는 메모리를 “더 저장”하는 장치가 아니라, VA를 PA로 바꾸는 주소 변환 체계임.

포인터 0x4000을 읽을 때 실제 PA를 정하는 주체는 프로그램이 아니라 MMU와 현재 프로세스의 Page Table임.
MMU가 VA를 해석하고, 해당 PTE를 찾아 PPN을 얻은 뒤 offset을 붙여 PA를 만듦.

키워드: Address Space, private address space, MMU, VPN/VPO, PPN/PPO, PTE, Page Table, Translation, page fault, protection

## 연결 키워드

- [Address Space](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#3-주소-공간--프로세스가-보는-세계)
- [MMU](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#1-물리-세계--실제로-존재하는-것)
- [Page Table & Translation](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
