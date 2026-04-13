# Q6. 전역 변수의 일생 — 소스 → 디스크 → 가상공간 → 물리프레임

> CSAPP 9.3-9.4 | 2부. 페이지 폴트 | 심화

## 질문

1. 전역 변수 `int g = 42;`가 소스코드에서 물리 메모리에 올라가기까지의 전체 경로를 서술하시오.
2. 각 단계에서 관여하는 주체(컴파일러, 링커, 로더, 커널, MMU)를 구분하시오.
3. [검증] `g`에 처음 접근하는 순간 demand paging 관점에서 무슨 일이 일어나는지 설명하시오.

## 답변

int g = 42; 는 소스코드 단계에서는 전역 초기화 변수이고, 컴파일/어셈블을 거치며 실행파일의 .data section 내용으로 들어감.
링커는 이 전역 변수의 심볼과 section 배치를 정리하고, 로더/커널은 execve 시 이 실행파일 구간을 file-backed private VMA로 주소공간에 memory mapping 함.
이 시점에는 g가 이미 “가상주소공간에는 존재”하지만, 아직 DRAM에 올라와 있지 않을 수 있으므로 할당됨 ≠ 상주함 임.

프로그램이 처음 g에 접근하면, 해당 VA에 대한 PTE가 non-resident 상태라 page fault가 나고, 커널이 실행파일의 .data가 들어 있는 page를 disk에서 읽어 물리 프레임에 적재한 뒤 PTE를 갱신함.
그 후 MMU가 g의 VA를 그 물리 프레임의 PA로 번역하고, 이후 접근은 hit 경로로 처리됨.

키워드: global variable, .data, compiler, assembler, linker, loader, execve, file-backed mapping, VMA, demand paging, page fault, PTE, resident, non-resident, MMU, VA→PA, physical frame


## 연결 키워드

- [Demand Paging](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#2-가상화의-이유--vm이-해결하는-3가지-문제)
- [Memory Mapping](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#6-메모리-매핑--프로그램이-메모리를-얻는-방법)
