# 발표 4. Heap allocator와 메모리 버그

## 이 발표의 역할

이 파트는 앞선 발표들이 만든 하드웨어, 가상 메모리, 커널의 토대 위에서, 개발자가 직접 맞닥뜨리는 사용자 공간 allocator와 메모리 버그를 설명하는 역할이다.
즉 "프로그램이 heap을 어떻게 쓰고, 왜 느려지고, 왜 위험해지는가"를 다룬다.

## 한 줄 흐름

`fragmentation` -> `segregated free list` -> `block header bit trick` -> `split / coalesce` -> `placement policy` -> `free() 설계` -> `memory bugs`

## 담당 질문 순서

1. [Q19. 내부 단편화 vs 외부 단편화 — 숫자로 체감하기](../q19-fragmentation.md)
   allocator가 왜 어려운지 보여 주는 출발점이다. 이후 모든 정책 논의의 평가 기준이 된다.

2. [Q20. Segregated Free List — 왜 bin을 나누는가?](../q20-segregated-free-list.md)
   allocator가 성능과 단편화를 동시에 잡기 위해 어떤 구조를 쓰는지 설명한다.

3. [Q21. 헤더의 하위 3비트 트릭 — 정렬이 만든 공짜 비트](../q21-header-lower-3bits.md)
   block metadata를 어떻게 압축해서 저장하는지 설명한다. allocator 구현 디테일의 핵심이다.

4. [Q22. Split과 Coalesce — 경계 태그의 O(1) 마법](../q22-split-coalesce.md)
   free block을 어떻게 쪼개고 다시 합치는지 설명한다. allocator의 동작 원리 자체를 보여 준다.

5. [Q23. 배치 정책 — First/Next/Best Fit 실전 트레이드오프](../q23-placement-policy.md)
   어떤 free block을 선택할지가 실제 성능과 단편화에 어떤 차이를 만드는지 정리한다.

6. [Q24. free()의 설계 철학 — 왜 에러를 알려주지 않는가?](../q24-free-design.md)
   allocator API가 왜 위험하지만 그렇게 설계되었는지를 설명한다. 개발자 관점의 현실 문제와 연결된다.

7. [Q25. 메모리 버그 총정리 — 왜 C가 위험한가?](../q25-memory-bugs.md)
   마지막 정리 질문이다. 지금까지 다룬 구조가 왜 leak, double free, use-after-free, overflow 같은 버그로 이어지는지를 묶어 준다.

## 발표 마무리 문장

이 발표가 끝나면 청중은 "메모리 버그는 단순 부주의가 아니라, allocator 구조와 수동 메모리 관리 모델에서 자연스럽게 발생하는 위험"이라는 점을 이해하게 된다.
이 파트가 전체 발표의 현실적인 착지점이 된다.
