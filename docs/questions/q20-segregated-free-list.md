# Q20. Segregated Free List — 왜 bin을 나누는가?

> CSAPP 9.9 | 8부. malloc | 심화

## 질문

1. Implicit → Explicit → Segregated Free List로 발전한 이유를 시간 복잡도 관점에서 설명하시오.
2. Segregated list에서 size class(bin) 설계 기준을 서술하시오.
3. [검증] bin이 {1-8B}, {9-16B}, {17-32B}일 때, malloc(12) 요청이 처리되는 과정을 설명하시오.

## 답변

### 최우녕

> Implicit → Explicit → Segregated Free List로 발전한 이유를 시간 복잡도 관점에서 설명하시오.

Implicit Free List:
모든 블록(할당 + 가용)을 순서대로 탐색한다. 할당된 블록도 건너뛰면서 봐야 하므로
malloc 호출 시 O(전체 블록 수)가 걸린다. 블록이 많아지면 매우 느리다.

Explicit Free List:
가용 블록만 연결 리스트로 묶는다. 할당된 블록은 건너뛴다.
malloc 시 O(가용 블록 수)만 탐색하면 된다.
하지만 가용 블록이 많으면 여전히 느릴 수 있다.

Segregated Free List:
가용 블록을 크기별로 분류한 여러 개의 리스트(bin)로 나눈다.
malloc(n) 요청이 오면 n에 맞는 bin만 보면 되므로
탐색 범위가 크게 줄어든다. 적절한 bin 설계 시 거의 O(1)에 가까워진다.

```text
  발전 방향:
  ㄴ Implicit : 전체 블록 순회      O(전체 블록 수)
  ㄴ Explicit : 가용 블록만 순회    O(가용 블록 수)
  ㄴ Segregated: 해당 bin만 탐색    ~O(1)
```

> Segregated list에서 size class(bin) 설계 기준을 서술하시오.

작은 크기는 정확하게, 큰 크기는 범위를 넓게 잡는다.
작은 할당이 빈번하므로 정확한 크기로 bin을 만들면 낭비가 적고,
큰 할당은 드물므로 넓은 범위를 하나의 bin으로 묶어도 괜찮다.

일반적인 설계:
ㄴ 작은 크기: 8B 단위로 정확히 (8, 16, 24, 32, ...)
ㄴ 중간 크기: 2의 거듭제곱 범위 (33~64, 65~128, 129~256, ...)
ㄴ 큰 크기: 하나의 큰 bin에 몰아넣기

glibc malloc(ptmalloc)은 fastbin(작은 크기, LIFO), smallbin, largebin으로
3단계로 나눠서 관리한다.

> [검증] bin이 {1-8B}, {9-16B}, {17-32B}일 때, malloc(12) 요청이 처리되는 과정을 설명하시오.

```text
전제: bin 구성
  bin[0]: {1-8B}   가용 블록들
  bin[1]: {9-16B}  가용 블록들
  bin[2]: {17-32B} 가용 블록들

━━━ malloc(12) 처리 과정 ━━━━━━━━━━━━━━━━━━━━━━━

  1. 요청 크기 결정
     |
     ㄴ 사용자 요청 = 12B
     ㄴ 헤더(8B) + 정렬 패딩 고려 → 실제 필요한 블록 크기 = 16B~24B
        (구현마다 다르지만, 여기선 요청 크기 12B 기준으로 bin 선택)

  2. bin 선택
     |
     ㄴ 12B → {9-16B} bin에 해당 → bin[1] 탐색

  3. bin[1]에서 블록 찾기
     |
     ㄴ 가용 블록 있음 → 그 블록을 할당, 리스트에서 제거
     ㄴ 가용 블록 없음 → bin[2] (더 큰 bin)에서 블록을 가져와 split
        |
        ㄴ bin[2]에서 32B 블록을 꺼냄
        ㄴ 16B로 split → 16B 할당, 나머지 16B는 bin[1]에 삽입
        |
        ㄴ bin[2]도 비어있으면 → sbrk/mmap으로 OS에 새 메모리 요청

  4. 할당 완료
     |
     ㄴ 16B 블록의 헤더에 할당 비트 설정
     ㄴ 헤더 다음 주소를 사용자에게 반환

━━━ 내부 단편화 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  요청 12B인데 16B 블록을 할당 → 4B 패딩 낭비
  이것이 내부 단편화다. bin을 더 세밀하게 나누면 줄일 수 있지만
  bin 수가 많아지면 관리 오버헤드가 커진다.
```

## 연결 키워드

- [Free List 구성](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#7-동적-할당--mallocfree의-내부)
- [Placement Policy](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#7-동적-할당--mallocfree의-내부)
