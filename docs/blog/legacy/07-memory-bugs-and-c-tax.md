# 07. C가 프로그래머에게 떠넘긴 것 — 메모리 버그와 Undefined Behavior

> 키워드 트리에서 이 글의 위치: `Undefined Behavior → memory bug` 전체
>
> 앞에서 allocator 가 얼마나 정교한 자료구조인지를 봤다. 이번 글은 그 자료구조 위에서 **프로그래머가 계약을 어겼을 때** 어떤 버그가 생기는지를 다룬다.

## "C는 low-level control 이 강한 대신 위험도 크다"

CSAPP 9장 키워드 트리가 이 절을 두 줄로 요약한다.

> C는 잘못된 memory access 를 항상 예외로 막아주지 않는다.
> 그래서 low-level control 이 강한 대신 위험도 크다.

실행 중 프로그램이 잘못된 주소에 접근했을 때 **무슨 일이 일어날지 정해져 있지 않다**.
이게 C 의 `Undefined Behavior` 다. 크래시가 날 수도 있고, 아무 일도 안 일어날 수도 있고,
**가장 나쁜 경우 조용히 잘못된 값을 반환** 한다.

이번 주에 ASan(AddressSanitizer) 를 붙여서 minidb 를 빌드한 이유가 이거였다.
Undefined 인 동작을 **관찰 가능한 크래시** 로 바꿔 주는 도구 없이는 UB 를 잡아내기 너무 어렵다.

## 메모리 버그 전집

키워드 트리의 `memory bug` 아래 있는 8가지를 하나씩 본다.

### 1) out-of-bounds access — 경계를 넘은 읽기/쓰기

```c
int arr[10];
arr[10] = 42;   // 0~9 만 유효. 10은 밖.
```

C 는 배열 경계를 검사하지 않는다. `arr[10]` 은 `arr` 의 base 에 `10 * 4` 를 더한 주소에 접근한다.
거기에 뭐가 있든 상관 안 한다. 만약 그게 다른 변수의 자리라면 그 변수 값이 조용히 바뀐다.

**Heap 에서의 out-of-bounds 는 특히 치명적** 이다.
allocator 의 헤더/footer 가 heap 블록 사이사이에 있기 때문에, 경계를 넘은 write 는
**allocator 의 메타데이터를 건드린다.** free list 가 깨지고, 이후 malloc / free 가 오동작한다.

### 2) use-after-free (UAF)

```c
char *p = malloc(64);
strcpy(p, "hello");
free(p);
printf("%s\n", p);   // UAF — 이미 해제된 메모리를 읽음
```

`free(p)` 직후 그 메모리는 allocator 의 free list 에 들어간다.
allocator 는 그 영역의 앞 부분을 **next pointer** 로 재활용하고 있다. 그러니 p 가 가리키는 값이 더 이상 "hello" 가 아닐 수 있다.

UAF 의 진짜 무서움은 **"가끔" 만 문제가 생긴다** 는 것이다.
그 메모리가 아직 재사용되지 않았다면 옛 값이 그대로 남아 있어서 프로그램이 정상 동작하는 것처럼 보인다.
그러다가 다른 말록 호출이 우연히 같은 영역을 받아 가면, 전혀 관련 없는 코드가 이상하게 동작하기 시작한다.
**재현이 어렵고 추적이 어렵다.**

### 3) double free / invalid free

```c
char *p = malloc(64);
free(p);
free(p);   // double free
```

앞선 포스트 06 에서 봤듯이 `free(p)` 는 p 앞에 있는 header 를 읽어서 해당 블록을 free list 에 넣는다.
두 번 부르면 **이미 free list 에 있는 블록의 header 를 또 건드린다**.
리스트가 손상되고, 이후 어떤 malloc 이 리스트를 따라가다 엉뚱한 주소를 반환할 수 있다.

이것이 **heap exploit** 의 대표 진입점이다. 공격자가 free list 를 조작해서
다음 malloc 이 공격자가 원하는 주소를 반환하도록 유도할 수 있다.

`invalid free` (allocator 가 준 포인터가 아닌 걸 `free` 에 넘기는 것) 도 같은 범주다.
`free(stack_array)` 나 `free(p + 4)` 같은 건 `free` 가 잘못된 위치를 header 로 읽게 만든다.

### 4) memory leak

```c
while (1) {
    char *buf = malloc(1024);
    process(buf);
    // free 를 빼먹음
}
```

allocator 가 관리하는 블록인데 프로그램이 포인터를 잃어버린 상태.
블록은 살아 있고 재사용도 안 된다. 시간이 지날수록 heap 이 커지다가 `malloc` 이 실패한다.

leak 은 크래시를 일으키지 않기 때문에 테스트에서 잡히지 않는 경우가 많다.
장시간 서비스가 돌아간 후에야 OOM 으로 발현되는 게 보통이다.

### 5) dangling pointer

free 된 객체를 가리키는 포인터가 여러 개 있는데, 한 쪽에서 free 를 한 뒤 나머지 포인터를 그대로 쓰는 상황.
사실상 UAF 의 일반화.

```c
Node *node = malloc(sizeof(Node));
Node *alias = node;
free(node);
alias->value = 10;   // dangling pointer 사용
```

### 6) uninitialized read

```c
int x;
printf("%d\n", x);   // x 는 초기화되지 않음. 스택의 쓰레기 값.
```

지역 변수는 자동 초기화되지 않는다. `int x;` 는 스택 프레임에 x 의 자리만 잡을 뿐, 거기 있던 값을 그대로 쓴다.
heap 도 마찬가지다. `malloc` 은 내용을 보존하지 않는다 (반대로 `calloc` 은 0 으로 초기화).

### 7) stack overflow

```c
void recurse() { int buf[1000]; recurse(); }
```

스택은 보통 8MB. 깊은 재귀 또는 큰 지역 배열이 쌓이면 스택이 VMA 경계를 넘어간다.
거기에 protection page(guard page) 가 있으면 protection fault → SIGSEGV.
없으면 힙이나 다른 VMA 를 덮어쓰면서 더 조용하고 무서운 버그가 된다.

### 8) `sizeof(pointer)` vs `sizeof(object)` 혼동

```c
void func(int arr[]) {
    int n = sizeof(arr) / sizeof(int);   // 버그
    // arr 는 함수 인자에서 int* 로 decay → sizeof(arr) == 8
}
```

배열이 함수 인자로 넘어가면 포인터로 "decay" 된다. `sizeof` 가 포인터 크기(8B)를 돌려준다.
초심자가 가장 자주 당하는 함정 중 하나.

## 이 모든 버그의 공통 원인 — "포인터에는 메타데이터가 없다"

C 포인터는 그냥 주소 숫자다.
- 이 포인터가 가리키는 영역의 **크기** 를 모른다 → OOB
- 이 포인터가 가리키는 영역이 아직 **유효한지** 모른다 → UAF, dangling
- 이 포인터가 이미 free 됐는지 **모른다** → double free
- 이 포인터가 **정말로 allocator 가 준 것인지** 모른다 → invalid free
- 이 포인터가 초기화된 영역을 가리키는지 **모른다** → uninitialized read

Rust 가 ownership / lifetime / borrow checker 로 해결하는 게 이 리스트 전체다.
Rust 는 포인터에 메타데이터를 붙이지 않지만, **컴파일 타임에 포인터의 수명을 추적** 해서 위 규칙을 강제한다.

C++ 도 `std::unique_ptr`, `std::shared_ptr`, RAII, `std::string_view` 같은 타입으로 이 버그들을 줄인다.
하지만 **컴파일러가 강제하진 않는다.** 여전히 raw pointer 는 있다.

이 지점이 CSAPP 키워드 트리가 C 아래에 이렇게 적어둔 이유다.

> 이들은 대부분 C의 자유와 hardware 근접성이 만든 대가다.

## 도구들 — UB 를 관찰 가능하게 만들기

이번 주에 minidb 를 빌드할 때 `-fsanitize=address,undefined` 를 켰다.
각 도구가 잡아주는 버그의 범위가 다르다.

| 도구 | 잡는 버그 |
|------|----------|
| ASan | OOB, UAF, double free, invalid free, leak (+ `LeakSanitizer`) |
| UBSan | 정수 오버플로우, 잘못된 포인터 변환, null deref 등 UB 전반 |
| MSan | 초기화되지 않은 읽기 |
| TSan | 데이터 레이스 |
| Valgrind Memcheck | 위 전부 (ASan 보다 느리지만 더 디테일) |

minidb 는 기본 빌드에 ASan + UBSan 을 포함한다. 실제 실행이 느려지지만,
개발 중 UB 를 조용히 넘어가지 않게 만들어 주기 때문에 이번 주 내내 이 옵션이 여러 번 나를 구해 줬다.

운영 환경에서도 가능한 추가 도구로는 `/proc/<pid>/maps`, `/proc/<pid>/smaps`, `pmap`, `perf`,
그리고 **ARM MTE** (Memory Tagging Extension) 같은 하드웨어 기반 방어도 있다.

## minidb 의 경험 — "C 에서도 계층을 나누면 버그가 줄어든다"

minidb 를 짜면서 가장 많이 부딪친 버그는 **pin leak** 이었다.
`pager_get_page()` 가 pin_count 를 +1 하는데, 쌍이 되는 `pager_unpin()` 을 빼먹으면
해당 frame 이 영원히 교체되지 않고 남는다. 결과적으로 frame 고갈.

이 버그는 UAF 와 반대의 성격을 갖는다.
- UAF = 수명이 끝난 자원을 계속 쓴다
- pin leak = 쓰고 나서 수명을 반환하지 않는다

해결은 "get_page → 사용 → unpin" 의 쌍을 **함수 경계에서만** 처리하는 것.
내부에서 잠깐 get_page 한 건 그 함수 안에서 반드시 unpin. 호출자에게 반환해야 하는 경우는 드물게 제한.
이것은 사실상 **RAII 의 수동 구현** 이다. C 에선 언어가 해 주지 않으니 사람이 규율로 지켜야 한다.

## 이번 주 인사이트 — C 에 대한 내 태도가 바뀌었다

C++ / C# / Java 로 객체지향을 먼저 배우고 C 를 쓰기 시작했을 때,
나는 C 가 "추상화가 부족한 원시 언어" 처럼 느껴졌다. 클래스도 없고, 인터페이스도 없고,
이미 알고 있던 추상화 도구가 전부 빠져 있는 느낌이었다.

그런데 이번 주에 malloc / free 의 내부와 그 위의 메모리 버그들을 다 정리하고 나니,
**C 는 추상화가 적은 게 아니라, 추상화가 투명한 언어** 라는 감각이 생겼다.
포인터는 주소 자체를 보여주고, 구조체는 메모리 레이아웃을 그대로 보여주고,
free 는 계약이 명시적이어서 위반하면 바로 UB 가 된다.

이 "투명성" 때문에 버그가 많지만, 동시에 이 "투명성" 이 있어야 OS / DB 같은 시스템 소프트웨어가 가능하다.
C++ / Rust 는 그 투명성을 유지하면서 안전 장치를 언어 차원에서 추가하는 방향이고,
관리 언어(Java, Python, Go) 는 투명성을 포기하는 대가로 안전을 얻는 방향이다.

세 방향이 같은 문제를 다르게 푼다는 게 이제 눈에 들어온다.

## 다음 글로의 연결

그러면 C 가 아닌 다른 언어들은 이 문제를 어떻게 푸는가?
GC, RAII, ownership, ... 그리고 "C 로도 OOP 에 준하는 추상화가 가능한가?" 라는 질문까지.

→ [08. GC와 런타임, 그리고 C에서의 추상화 — 클래스 없이 타입을 만드는 법](./08-runtime-gc-and-c-abstraction.md)
