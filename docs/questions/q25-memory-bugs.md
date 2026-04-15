# Q25. 메모리 버그 총정리 — 왜 C가 위험한가?

> CSAPP 9.11 | 8부. malloc | 검증

## 질문

1. 대표적인 메모리 버그 5가지를 코드 예시와 함께 설명하시오.
2. dangling pointer와 memory leak의 차이를 설명하시오.
3. [검증] `int **A = malloc(n * sizeof(int)); ... A[n] = NULL;` 에서 버그 2개를 찾고 수정하시오.

## 답변

### 최우녕

> 대표적인 메모리 버그 5가지를 코드 예시와 함께 설명하시오.

```text
━━━ 1. Buffer Overflow (버퍼 오버플로우) ━━━━━━━━━━

  char buf[8];
  strcpy(buf, "Hello, World!");  // 13B를 8B 버퍼에 복사

  ㄴ buf 뒤에 있는 메모리(다른 변수, 리턴 주소 등)가 덮어씌워진다
  ㄴ 힙이면 다음 블록의 헤더가 파괴 → allocator 오동작
  ㄴ 스택이면 리턴 주소 변조 → 공격자가 코드 실행 가능

━━━ 2. Use-After-Free (해제 후 사용) ━━━━━━━━━━━━━

  char *p = malloc(100);
  free(p);
  p[0] = 'A';  // 이미 해제된 메모리에 쓰기

  ㄴ free 후 그 블록은 free list에 들어가고 다른 malloc이 재사용할 수 있다
  ㄴ p로 쓰면 새로 할당된 다른 데이터를 오염시킨다
  ㄴ 읽기도 위험 — 쓰레기 값이거나 다른 사용자의 데이터일 수 있음

━━━ 3. Double Free (이중 해제) ━━━━━━━━━━━━━━━━━━━

  char *p = malloc(100);
  free(p);
  free(p);  // 같은 블록을 두 번 해제

  ㄴ free list에 같은 블록이 두 번 등록됨
  ㄴ 이후 malloc 두 번이 같은 주소를 반환 → 두 포인터가 같은 메모리 공유
  ㄴ Q24에서 상세히 다룸

━━━ 4. Memory Leak (메모리 누수) ━━━━━━━━━━━━━━━━━

  void foo() {
      char *p = malloc(100);
      return;  // free 안 하고 함수 종료
  }

  ㄴ p는 스택에 있던 지역 변수 → 함수 종료 시 사라짐
  ㄴ malloc한 힙 메모리는 여전히 할당 상태 → 접근할 방법도, 해제할 방법도 없음
  ㄴ 장시간 실행되는 서버에서 leak이 쌓이면 OOM(Out of Memory)으로 크래시

━━━ 5. Uninitialized Read (초기화 안 된 메모리 읽기) ━

  int *p = malloc(sizeof(int));
  printf("%d\n", *p);  // 초기화 안 하고 읽기

  ㄴ malloc은 메모리를 0으로 초기화하지 않는다 (calloc은 함)
  ㄴ 이전에 그 주소를 썼던 프로그램의 잔여 데이터가 남아있다
  ㄴ 비결정적 동작 — 디버깅 시 재현이 안 될 수 있음
  ㄴ 보안: 이전 사용자의 비밀번호, 키 등이 노출될 가능성
```

> dangling pointer와 memory leak의 차이를 설명하시오.

```text
━━━ Dangling Pointer (허상 포인터) ━━━━━━━━━━━━━━━

  포인터는 남아있는데 가리키는 메모리가 해제된 상태.
  "열쇠는 있는데 문이 철거된 것"

  char *p = malloc(100);
  free(p);
  // p는 여전히 이전 주소를 가리킴 → dangling pointer
  // p를 통해 읽기/쓰기하면 use-after-free 버그

━━━ Memory Leak (메모리 누수) ━━━━━━━━━━━━━━━━━━━━

  메모리는 할당되어 있는데 가리키는 포인터가 없는 상태.
  "문은 있는데 열쇠를 잃어버린 것"

  char *p = malloc(100);
  p = malloc(200);  // 이전 100B의 주소를 잃어버림
  // 첫 번째 100B는 할당 상태이지만 접근 불가 → leak

━━━ 핵심 차이 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Dangling Pointer: 포인터 O, 메모리 X → 접근하면 위험
  Memory Leak:      포인터 X, 메모리 O → 해제 불가, 자원 낭비

  둘 다 포인터와 메모리의 수명이 불일치할 때 발생한다.
  C에서는 프로그래머가 이 수명을 직접 관리해야 하므로
  두 버그 모두 자연스럽게 발생하기 쉽다.
```

> [검증] `int **A = malloc(n * sizeof(int)); ... A[n] = NULL;` 에서 버그 2개를 찾고 수정하시오.

```text
  원본 코드:
  int **A = malloc(n * sizeof(int));
  // ... 사용 ...
  A[n] = NULL;

━━━ 버그 1: sizeof 타입 불일치 ━━━━━━━━━━━━━━━━━━

  A는 int** (포인터의 포인터)다.
  A의 각 원소는 int* (포인터)다.

  malloc(n * sizeof(int)) → int 크기(4B)로 n개 할당
  실제 필요한 건 int* 크기(8B)로 n개 할당

  x86-64에서:
  ㄴ sizeof(int) = 4바이트
  ㄴ sizeof(int*) = 8바이트
  ㄴ 절반만 할당됨 → 뒤쪽 원소에 접근하면 버퍼 오버플로우

  수정: malloc(n * sizeof(int*))

━━━ 버그 2: 배열 범위 초과 (off-by-one) ━━━━━━━━━

  A[n] = NULL;

  A는 n개 원소를 할당했으므로 인덱스는 0 ~ n-1까지 유효하다.
  A[n]은 할당 범위 바로 다음 메모리에 쓰기 → 버퍼 오버플로우

  ㄴ 다음 블록의 헤더를 NULL(0)로 덮어쓸 수 있음
  ㄴ allocator 메타데이터 파괴 → 이후 malloc/free 비정상 동작

  수정: A[n-1] = NULL;

━━━ 수정된 코드 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  int **A = malloc(n * sizeof(int*));  // int → int*
  // ... 사용 ...
  A[n-1] = NULL;                       // A[n] → A[n-1]
```

## 연결 키워드

- [Memory Bugs](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#9-메모리-버그--c의-대가)
- [free() 설계](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#7-동적-할당--mallocfree의-내부)
