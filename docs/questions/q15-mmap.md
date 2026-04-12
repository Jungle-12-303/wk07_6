# Q15. mmap — file-backed vs anonymous, shared vs private

> CSAPP 9.8 | 6부. mmap·sbrk | 검증

## 질문

1. mmap의 4가지 조합(file-backed/anonymous × shared/private)을 각각 설명하시오.
2. 공유 라이브러리(libc.so)가 어떤 mmap 유형으로 매핑되는지, 그 이유와 함께 서술하시오.
3. [검증] `mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0)` 호출 시 커널이 하는 일을 설명하시오.

## 답변

> 여기에 작성...

## 연결 키워드

- [mmap](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#6-메모리-매핑--프로그램이-메모리를-얻는-방법)
- [VMA](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#5-커널의-역할--누가-이-모든-걸-관리하는가)
