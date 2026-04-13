# 발표 3. Process, Kernel, Mapping, 메모리 획득 경로

## 이 발표의 역할

이 파트는 앞선 두 발표의 paging 메커니즘을 실제 OS 실행 모델 위에 올려놓는 역할이다.
즉 프로세스와 커널이 어떤 관계인지, 커널은 언제 개입하는지, 프로그램은 어떤 경로로 메모리를 받는지를 설명한다.

## 한 줄 흐름

`OS / kernel / process / thread` -> `kernel mode entry` -> `process별 page table` -> `global variable의 경로` -> `mmap` -> `sbrk vs mmap`

## 담당 질문 순서

1. [Q17. OS·커널·스케줄러·프로세스·스레드 한번에 정리](../q17-os-kernel-process-thread.md)
   전체 실행 주체를 먼저 정리한다. 이 질문이 있어야 Q18과 Q12의 설명이 흔들리지 않는다.

2. [Q18. 커널 모드 진입 경로 3가지와 커널 스레드](../q18-kernel-mode-entry.md)
   커널이 언제 사용자 코드 흐름에 개입하는지 설명한다. syscall, interrupt, exception이 모두 메모리 관리와 연결된다.

3. [Q12. CR3·프로세스별 페이지 테이블·물리 페이지 공유](../q12-cr3-page-sharing.md)
   각 프로세스가 자기 page table을 가진다는 사실과, 그런데도 물리 page는 공유될 수 있다는 점을 정리한다.

4. [Q6. 전역 변수의 일생 — 소스 → 디스크 → 가상공간 → 물리프레임](../q06-global-variable-lifecycle.md)
   메모리 개념을 실제 프로그램 객체 하나에 적용하는 질문이다. 컴파일러, 링커, 로더, 커널, MMU가 한 번에 연결된다.

5. [Q15. mmap — file-backed vs anonymous, shared vs private](../q15-mmap.md)
   프로그램이 메모리를 받는 또 하나의 핵심 경로를 설명한다. 파일과 메모리의 경계가 흐려지는 지점이다.

6. [Q16. sbrk vs mmap — malloc이 두 경로를 쓰는 이유](../q16-sbrk-vs-mmap.md)
   사용자 공간 allocator가 결국 어떤 커널 인터페이스를 통해 heap을 확장하는지 설명하면서 다음 발표로 넘긴다.

## 발표 마무리 문장

이 발표가 끝나면 청중은 "메모리 관리가 단지 MMU 이야기만이 아니라, 프로세스 구조와 커널 진입, 파일 매핑, allocator의 시스템 호출 경로까지 포함하는 시스템 전반의 이야기"라는 점을 이해하게 된다.
다음 발표는 그 위에서 사용자 공간 allocator 내부와 메모리 버그를 설명하면 된다.
