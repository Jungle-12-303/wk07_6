# Q9. Copy-on-Write — fork()가 빠른 진짜 이유

> CSAPP 9.8 | 3부. 보호와 공유 | 검증

## 질문

1. COW의 동작 원리를 fork() 호출 시점부터 설명하시오.
2. COW가 없다면 fork()의 비용이 어떻게 달라지는지 서술하시오.
3. [검증] 부모가 fork() 후 자식이 변수 `x`에 write할 때, PTE와 물리 페이지에 일어나는 변화를 단계별로 설명하시오.

## 답변

fork() 시점의 COW는 부모와 자식이 물리 페이지를 즉시 복사하지 않고, 둘의 PTE를 같은 물리 페이지로 가리키게 만든 뒤 둘 다 read-only로 바꾸는 것에서 시작함.
이후 어느 한쪽이 write를 시도하면, write가 Protection fault를 일으키고 커널이 그 시점에만 새 물리 페이지를 하나 복사해 해당 프로세스의 PTE를 그 새 페이지로 바꾸고 write 가능하게 만듦.
COW가 없다면 fork()는 부모의 주소공간 전체를 즉시 복사해야 하므로, 메모리 사용량과 복사 비용이 커지고 execve()로 곧바로 덮어쓸 경우에도 불필요한 복사가 발생함.

부모가 fork() 후 자식이 변수 x에 write하면, 처음엔 부모/자식 PTE가 같은 물리 페이지를 read-only로 공유하다가, 자식의 write 시도에서 fault가 나고 커널이 새 물리 페이지를 복사한 뒤 자식 PTE만 새 페이지를 가리키도록 갱신하며 write bit를 허용함.
결과적으로 부모는 기존 물리 페이지를 계속 보고, 자식만 수정된 private copy를 보게 되며, 이것이 fork()가 빠른 진짜 이유임.

키워드: COW, fork, shared physical page, read-only PTE, protection fault, private copy, PTE update, lazy copy, execve, memory overhead

## 연결 키워드

- [COW](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#5-커널의-역할--누가-이-모든-걸-관리하는가)
- [Protection](../csapp-ch9-virtual-memory/csapp-ch9-keyword-tree.md#2-가상화의-이유--vm이-해결하는-3가지-문제)
