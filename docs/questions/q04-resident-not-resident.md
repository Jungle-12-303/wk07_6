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

## 연결 키워드

- [PTE](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
- [Demand Paging](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#2-가상화의-이유--vm이-해결하는-3가지-문제)
