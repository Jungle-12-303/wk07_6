# 메모리 버그 재현과 탐지 도구 비교

C로 minidb를 짜면서 가장 두려웠던 건 메모리 버그였다.
세그멘테이션 폴트는 차라리 고맙다.
즉시 크래시하고 위치가 찍히니까.
진짜 무서운 건 조용히 깨진 상태로 계속 돌아가는 프로그램이다.
몇 시간 뒤 전혀 다른 위치에서 엉뚱하게 죽거나, 결과만 슬쩍 틀리는.

이런 버그들은 "발생 안 하게 조심한다"로는 잘 배워지지 않았다.
그래서 한 번 반대 방향으로 가 보기로 했다.
의도적으로 버그를 재현하고, 어떻게 망가지는지, 어떤 도구가 어떤 타이밍에 잡아내는지를 손으로 확인해 본 기록이다.

## 실험 환경

- 언어: C (`-std=c11`)
- OS: Linux x86-64
- 컴파일러: gcc 13
- 탐지 도구: `valgrind (memcheck)`, `AddressSanitizer (ASan)`, `-fsanitize=undefined (UBSan)`
- 최적화: `-O0 -g` (증상을 관찰하기 위함)

각 버그마다 `main()` 수준의 최소 재현 코드를 쓰고, 네 가지 설정으로 돌렸다.

1. 그냥 컴파일 (`gcc file.c`)
2. ASan (`gcc -fsanitize=address file.c`)
3. UBSan (`gcc -fsanitize=undefined file.c`)
4. valgrind (`valgrind --tool=memcheck ./a.out`)

## 1) Out-of-Bounds write

```c
int main(void) {
    int buf[4];
    buf[5] = 42;           // index 5는 배열 경계 밖
    printf("buf[0]=%d\n", buf[0]);
    return 0;
}
```

결과:

| 도구 | 결과 |
| --- | --- |
| 그냥 컴파일 | 아무 일도 안 일어남. `buf[0]=0` 출력. |
| ASan | 즉시 abort + "stack-buffer-overflow" 리포트 |
| UBSan | 잡지 못함 (UBSan은 타입·정수 영역이 주력) |
| valgrind | 잡지 못함. `buf`가 스택이라 valgrind가 경계를 모른다 |

두 가지를 확인했다.
스택 오버플로우를 잡는 유일한 옵션은 ASan이었고, 그냥 실행하면 버그가 전혀 안 보인다.
이 "전혀 안 보이는" 성질이 실제 버그의 80%를 어렵게 만든다.
내가 안 본 자리에 조용히 스택을 망가뜨리고 있을 뿐.

`buf[5] = 42`는 실제로는 스택 프레임의 옆 변수를 덮어썼다.
ASan을 끄고 다른 변수를 함께 놓아 보면 그 옆 변수의 값이 의도 없이 42로 바뀌는 걸 관찰할 수 있었다.
같은 이유로 "최적화 수준을 바꾸니 버그가 사라졌다"는 신비 현상도 스택 레이아웃 변화로 설명됐다.

## 2) Heap Out-of-Bounds

```c
int main(void) {
    int *buf = malloc(4 * sizeof(int));
    buf[5] = 42;           // 동일한 OOB, 이번엔 힙
    free(buf);
    return 0;
}
```

| 도구 | 결과 |
| --- | --- |
| 그냥 컴파일 | 일반적으로 아무 일 없음. glibc의 내부 청크 헤더를 건드리면 가끔 죽음 |
| ASan | 즉시 "heap-buffer-overflow" 리포트 |
| UBSan | 잡지 못함 |
| valgrind | 잡음. "Invalid write of size 4" 정확한 리포트 |

힙의 OOB는 스택보다 약간 더 쉽게 잡힌다.
특히 valgrind가 힙 경계를 알고 있어서 확실하다.
다만 valgrind는 느리다 (실측 20-40배 감속).
유닛 테스트에선 쓸만하지만 벤치마크 용도로는 무겁다.

ASan은 보통 2-3배 감속에 그쳐서, minidb의 테스트 스위트에는 ASan이 기본이 됐다.

## 3) Use-After-Free (UAF)

```c
int main(void) {
    int *p = malloc(sizeof(int));
    *p = 7;
    free(p);
    printf("%d\n", *p);   // 이미 free된 메모리 접근
    return 0;
}
```

| 도구 | 결과 |
| --- | --- |
| 그냥 컴파일 | 종종 7이 그대로 출력됨. glibc가 free 직후 메모리를 덮지 않기 때문 |
| ASan | "heap-use-after-free" + free된 위치와 현재 접근 위치 둘 다 스택트레이스로 표시 |
| valgrind | "Invalid read of size 4"로 잡음 |

UAF는 그냥 돌리면 정상처럼 보인다는 게 가장 큰 교훈이었다.
실제로 값이 7로 찍힌다.
프로그램이 계속 돌아간다.
같은 메모리가 나중에 다른 `malloc`에 재활용되는 순간 비로소 버그가 드러난다.

minidb에서 initial prototype에 UAF가 있던 자리는 B+ tree split 중 부모 노드 프레임이었다.
split 로직이 자식 프레임을 pin한 채 부모 프레임을 unpin했고, 그 사이 다른 코드가 부모를 eviction했다.
부모 포인터가 이미 free된 메모리를 가리키고 있었지만, 50%의 확률로 여전히 올바른 값이라 테스트가 통과했다.
pin_count를 도입하며 이 버그가 구조적으로 사라졌다 (application/02에서 다룬 바).

## 4) Double Free

```c
int main(void) {
    int *p = malloc(sizeof(int));
    free(p);
    free(p);              // 이미 free된 포인터 두 번째 free
    return 0;
}
```

| 도구 | 결과 |
| --- | --- |
| 그냥 컴파일 | 현대 glibc는 종종 "double free or corruption" 메시지와 함께 abort. 하지만 환경에 따라 조용히 지나가기도 함 |
| ASan | 즉시 "attempting double-free" 리포트 |

Double free는 glibc가 방어해 주는 몇 안 되는 버그다.
`malloc` 내부에 tcache / unsorted bin 같은 자료구조가 있고, 같은 청크가 free list에 두 번 들어가면 검사에 걸린다.
그렇다고 방심할 수 없는 것이 "검사를 우회하는 정확한 타이밍"이 공격자들이 쓰는 exploit 기법의 기초이기도 하다.

예방법은 단순하다.
free 후 즉시 `p = NULL`.
free된 포인터를 두 번 free해도 `free(NULL)`은 안전한 no-op이므로 버그가 닿지 않는다.

```c
#define FREE(p) do { free(p); (p) = NULL; } while (0)
```

C++의 `unique_ptr`, Rust의 ownership이 해결하는 문제의 가장 원초적인 버전이 바로 이것이다.

## 5) Memory Leak

```c
int main(void) {
    for (int i = 0; i < 100; i++) {
        int *p = malloc(1024);
        // free를 안 함
    }
    return 0;
}
```

| 도구 | 결과 |
| --- | --- |
| 그냥 컴파일 | 크래시 안 남. 프로세스 종료 시 OS가 회수 |
| ASan (LSan 내장) | 프로그램 종료 시 "Direct leak of 102400 byte(s)" 리포트 |
| valgrind | `--leak-check=full`로 자세한 리포트 |

Leak은 크래시하지 않는다.
그래서 가장 방치되기 쉽다.
단기 실행 프로그램은 어차피 종료 시 OS가 다 회수하니 실용상 문제 없어 보이고, 서버처럼 장기 실행되는 프로그램에서야 비로소 증상이 드러난다.

minidb 같은 장기 실행 프로세스에서는 ASan의 leak 리포트를 CI에서 강제하도록 했다.
테스트가 끝나고 leak이 한 바이트라도 발견되면 빌드가 실패한다.
이 방어선이 없다면 leak은 한없이 자라며 "어느 순간 OOM killer에게 죽는" 형태로만 드러난다.

## 6) Uninitialized Read

```c
int main(void) {
    int x;
    if (x == 42) {        // x는 초기화되지 않음
        printf("matched\n");
    }
    return 0;
}
```

| 도구 | 결과 |
| --- | --- |
| 그냥 컴파일 | 경고는 나오지만 실행은 됨. 스택 쓰레기값에 따라 결과가 달라짐 |
| ASan | 잡지 못함 |
| MSan (Memory Sanitizer) | 잡음. 하지만 libc 전체를 다시 컴파일해야 해서 실용성 낮음 |
| valgrind | 잡음. "Conditional jump or move depends on uninitialised value(s)" |

초기화되지 않은 값은 실행마다 다른 결과를 만든다.
테스트가 100번 통과해도 101번째에 실패할 수 있다.
이 버그는 컴파일러의 경고(`-Wall -Wextra -Wuninitialized`)로 잡는 게 제일 정확하고 빠르다.

minidb는 `-Werror`로 초기화 관련 경고를 에러로 승격시켰다.
구조체는 모두 `= {0}`으로 초기화하는 습관도 같이 들였다.

```c
Frame f = {0};   // pin_count=0, is_dirty=false, pointers=NULL
```

## 7) Stack Overflow (무한 재귀)

```c
void rec(int n) {
    char buf[1024];
    rec(n + 1);            // 무한 재귀
}

int main(void) {
    rec(0);
    return 0;
}
```

| 도구 | 결과 |
| --- | --- |
| 그냥 컴파일 | 수 초 내 segfault. 스택 크기 한계(기본 8 MB)에 도달 |
| ASan | "stack-overflow"로 보고, segfault 대신 명시적 리포트 |
| valgrind | 보고함 |

Stack overflow는 잘 안 일어나는 듯 보이지만, 재귀적 파서, 재귀적 B+ tree 연산, 재귀 DFS 등 DB 엔진에도 자주 숨어 있다.
minidb의 쿼리 파서가 깊은 표현식을 만나면 재귀 깊이가 크다.
파서는 점진적으로 iterative로 전환했다.

## 디버깅 도구 비교

| 버그 종류 | 그냥 | ASan | UBSan | valgrind |
| --- | --- | --- | --- | --- |
| Stack OOB | ✗ | ✓ | ✗ | ✗ |
| Heap OOB | △ | ✓ | ✗ | ✓ |
| Use-after-free | △ | ✓ | ✗ | ✓ |
| Double free | △ | ✓ | ✗ | ✓ |
| Memory leak | ✗ | ✓ | ✗ | ✓ |
| Uninitialized read | △ | ✗ | ✗ | ✓ |
| Stack overflow | ✓ | ✓ | ✗ | ✓ |
| 정수 오버플로우 | ✗ | ✗ | ✓ | ✗ |
| NULL deref | ✓ | ✓ | ✗ | ✓ |
| 속도 | 1× | 2-3× | 1.1× | 20-40× |

`✗` 는 못 잡음, `△` 는 가끔 잡음, `✓` 는 일관되게 잡음.

## 결론

개발 시 ASan을 기본으로 켜고, CI에서 valgrind로 한 번 더 돌리는 것이 현재 내가 쓰는 표준이다.
UBSan은 정수·시프트 버그 잡는 용도로 함께 킨다.
이 세 가지를 결합하면 처음에 내가 두려워하던 "조용히 깨진 상태"의 많은 부분을 빛으로 끌어올 수 있다.

## 실험의 목적

이 실험의 진짜 목적은 버그 카탈로그를 만드는 것이 아니었다.
내 손으로 버그를 보고, 그 증상이 얼마나 교활한지를 감각적으로 익히는 것이었다.
책에서 "UAF는 위험하다"고 읽는 것과 "UAF를 쓰고 프로그램이 정상 종료하는 것을 직접 보는 것"은 다른 경험이다.

C를 쓰면서 배우는 기술 중 70%는 어떻게 쓰는가지만, 나머지 30%는 무엇이 잘못될 수 있는가에 대한 직관이다.
이 직관은 책으로는 잘 안 쌓이고, 실제 크래시를 몇 번 겪으면서만 쌓인다.
의도적으로 크래시를 만들어 보는 것은 이 직관을 압축된 시간에 주입하는 방법이었다.

그리고 그 과정에서 자연스럽게 체감한 것이, Rust나 Go 같은 언어가 왜 존재하는가다.
위 일곱 가지 버그 중 Rust의 타입 시스템이 컴파일 타임에 막는 것이 대부분이다.
C를 이해하기 전에는 "Rust가 너무 깐깐하다"고 느꼈는데, 의도적으로 C의 버그를 찍어 보고 나서는 "이걸 컴파일러가 다 잡아 주는 게 기적이다"로 감각이 바뀌었다.

C를 배우는 가치는 이 대비를 손으로 얻는 데도 있었다.
