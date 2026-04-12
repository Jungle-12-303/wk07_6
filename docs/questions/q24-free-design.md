# Q24. free()의 설계 철학 — 왜 에러를 알려주지 않는가?

> CSAPP 9.9 | 8부. malloc | 심화

## 질문

1. C 표준이 free(invalid)를 UB로 정의한 이유를 설명하시오.
2. 잘못된 포인터를 free 했을 때 일어날 수 있는 일을 3가지 이상 서술하시오.
3. [검증] 다음 각 경우에 어떤 문제가 발생하는지 설명하시오: (a) double free (b) free(p+1) (c) int x; free(&x)

## 답변

> 여기에 작성...

## 연결 키워드

- [free() 설계](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#7-동적-할당--mallocfree의-내부)
- [Memory Bugs](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#9-메모리-버그--c의-대가)
