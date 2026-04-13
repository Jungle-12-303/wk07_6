# 발표 2. Paging의 실제 동작: page size, fault, protection, sharing

## 이 발표의 역할

이 파트는 발표 1에서 만든 주소 번역 기초 위에, page가 실제 시스템에서 어떻게 "존재하고", "사라지고", "보호되고", "공유되는지"를 설명하는 역할이다.
즉 paging을 정적인 자료구조가 아니라 동적인 운영체제 메커니즘으로 보여 주는 묶음이다.

## 한 줄 흐름

`page size 선택` -> `resident / non-resident` -> `page fault` -> `VMA와 PTE의 역할 분리` -> `protection bits` -> `Copy-on-Write`

## 담당 질문 순서

1. [Q3. 페이지 크기 4KB의 근거 — Goldilocks 원리](../q03-page-size-4kb.md)
   왜 page라는 단위가 지금 크기로 잡혔는지부터 설명한다. 이후 resident, page fault 비용 설명의 전제가 된다.

2. [Q4. Resident vs Not Resident — '유효하지만 없다'](../q04-resident-not-resident.md)
   "할당됨"과 "메모리에 올라와 있음"이 다르다는 점을 설명한다. demand paging의 핵심 개념이다.

3. [Q5. Page fault의 분류 — '에러'가 아니라 '이벤트'](../q05-page-fault.md)
   page fault를 단순 오류가 아니라 page-in 메커니즘으로 설명한다. 발표 2의 중심 질문이다.

4. [Q7. vm_area_struct vs 페이지 테이블 — '의미' vs '번역'](../q07-vma-vs-page-table.md)
   page fault 처리 시 커널이 왜 VMA와 PTE를 다르게 보는지 설명한다. protection과 mapping의 의미가 여기서 정리된다.

5. [Q8. 메모리 보호 — SUP·READ·WRITE 비트의 동작](../q08-memory-protection.md)
   page가 존재하더라도 아무나 아무 연산을 할 수는 없다는 점을 권한 비트로 설명한다.

6. [Q9. Copy-on-Write — fork()가 빠른 진짜 이유](../q09-cow.md)
   protection fault와 page sharing이 결합된 대표 사례다. "보호"가 성능 최적화에도 쓰일 수 있음을 보여 준다.

## 발표 마무리 문장

이 발표가 끝나면 청중은 "page는 항상 메모리에 있는 것이 아니며, fault와 protection이 운영체제가 메모리를 lazy하게 관리하는 핵심 장치"라는 점을 이해하게 된다.
다음 발표는 이 메커니즘이 실제 프로세스, 커널, 파일 매핑, 메모리 획득 방식과 어떻게 연결되는지로 이어지면 된다.
