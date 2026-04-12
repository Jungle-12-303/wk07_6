# Q16. sbrk vs mmap — malloc이 두 경로를 쓰는 이유

> CSAPP 9.8-9.9 | 6부. mmap·sbrk | 심화

## 질문

1. sbrk()와 mmap()의 동작 차이를 설명하시오.
2. malloc이 작은 할당에 sbrk, 큰 할당(≥128KB)에 mmap을 쓰는 이유를 서술하시오.
3. [검증] `malloc(256*1024)` 호출 시 내부에서 어떤 시스템콜이 호출되고, free() 후 메모리가 OS에 반환되는지 설명하시오.

## 답변

> 여기에 작성...

## 연결 키워드

- [sbrk vs mmap](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#6-메모리-매핑--프로그램이-메모리를-얻는-방법)
- [Heap](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#3-주소-공간--프로세스가-보는-세계)
