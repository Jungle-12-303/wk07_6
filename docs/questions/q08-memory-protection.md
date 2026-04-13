# Q8. 메모리 보호 — SUP·READ·WRITE 비트의 동작

> CSAPP 9.5 | 3부. 보호와 공유 | 기본

## 질문

1. PTE의 SUP, READ, WRITE 비트 각각의 역할을 설명하시오.
2. 유저 프로세스가 커널 영역 주소를 읽으려 하면 어떤 일이 일어나는지 서술하시오.
3. [검증] READ-only 페이지에 write 시도 → 어떤 예외가 발생하고 커널이 어떻게 처리하는지 설명하시오.

## 답변

SUP 비트는 user/supervisor 접근 가능 여부를 결정하고, READ 비트는 읽기 허용 여부, WRITE 비트는 쓰기 허용 여부를 결정함.
유저 프로세스가 커널 영역 주소를 읽으려 하면, 해당 PTE의 SUP 설정과 현재 user mode가 충돌하여 protection fault가 발생함.

READ-only 페이지에 write를 시도하면, 해당 접근이 PTE의 WRITE 권한 검사에서 거부되어 general protection fault / segmentation fault 계열 예외로 처리됨.
커널은 이 예외를 받으면 단순 page-in이 아니라 불법 접근으로 판단하고, 보통 해당 프로세스에 SIGSEGV를 전달해 종료 방향으로 처리함.
즉 메모리 보호는 “주소가 존재하느냐”보다 그 주소에 대해 현재 모드에서 어떤 연산이 허용되느냐를 PTE permission bits로 통제하는 구조임.

키워드: SUP bit, READ bit, WRITE bit, PTE permission bits, user mode, kernel mode, protection, protection fault, general protection fault, segmentation fault, SIGSEGV

## 연결 키워드

- [Protection](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#2-가상화의-이유--vm이-해결하는-3가지-문제)
- [PTE Permission Bits](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#4-변환-메커니즘--va--pa가-실제로-일어나는-과정)
