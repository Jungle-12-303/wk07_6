# 헤더 페이지, 힙 페이지, 루트 리프 페이지 정리

이 문서는 이 프로젝트의 데이터베이스 파일에서 처음 만들어지는 3개의 핵심 페이지를 한글로 정리한 문서다.

- 0번 페이지: DB 헤더 페이지
- 1번 페이지: 첫 힙 페이지
- 2번 페이지: 루트 리프 페이지

관련 코드는 다음 파일에 있다.

- `src/storage/pager.c`
- `src/storage/table.c`
- `src/storage/bptree.c`
- `include/storage/page_format.h`

## 전체 그림

새 데이터베이스를 만들면 `pager_open(..., create=true)`가 아래 3개 페이지를 바로 초기화한다.

```text
page 0 : DB 전체 메타데이터
page 1 : 실제 row(행) 데이터를 담는 첫 heap page
page 2 : id -> row 위치를 찾기 위한 첫 leaf root page
```

각 페이지의 역할은 다음처럼 구분할 수 있다.

- 헤더 페이지: 데이터베이스 전체 설정과 상태
- 힙 페이지: 실제 행 데이터 저장
- 루트 리프 페이지: 행을 빨리 찾기 위한 인덱스 시작점

## 0번 페이지: DB 헤더 페이지

0번 페이지에는 `db_header_t` 구조체가 저장된다.

이 페이지는 데이터베이스의 "설명서" 역할을 한다. 즉 이 파일이 어떤 DB인지, 페이지 크기는 얼마인지, 다음에 어떤 페이지를 써야 하는지, 현재 테이블 구조가 어떤지 같은 전역 정보를 담는다.

대표적으로 들어가는 정보는 다음과 같다.

- `magic`
  이 파일이 우리 DB 파일이 맞는지 확인하는 식별 문자열
- `version`
  DB 포맷 버전
- `page_size`
  한 페이지의 바이트 크기
- `root_index_page_id`
  B+트리 루트 페이지 번호
- `first_heap_page_id`
  첫 번째 힙 페이지 번호
- `next_page_id`
  새 페이지를 할당할 때 사용할 다음 페이지 번호
- `free_page_head`
  재사용 가능한 free page 리스트의 시작 페이지 번호
- `next_id`
  다음 INSERT 때 자동으로 부여할 id 값
- `row_count`
  현재 저장된 row 수
- `column_count`
  테이블 컬럼 수
- `row_size`
  row 하나가 직렬화되었을 때 차지하는 총 바이트 수
- `columns[]`
  각 컬럼의 이름, 타입, 크기, offset 같은 메타정보

### 생성 직후 예시

새 DB를 처음 만들면 0번 페이지는 대략 아래와 같은 값을 가진다.

```text
magic = "MINIDB\0"
version = 1
page_size = 4096
first_heap_page_id = 1
root_index_page_id = 2
next_page_id = 3
free_page_head = 0
next_id = 1
row_count = 0
column_count = 0
row_size = 0
columns = 비어 있음
```

이 상태는 "아직 테이블도 없고 데이터도 없지만, 첫 힙 페이지는 1번이고 첫 인덱스 루트는 2번이다"라는 뜻이다.

## 1번 페이지: 첫 힙 페이지

1번 페이지는 `heap page`다. 힙 페이지에는 실제 테이블 row 데이터가 저장된다.

예를 들어 아래 SQL을 실행하면:

```sql
CREATE TABLE users (name VARCHAR(32), age INT);
INSERT INTO users VALUES ('kim', 20);
```

실제로 `(id=1, name="kim", age=20)` 같은 row 본문은 힙 페이지에 저장된다.

### 힙 페이지 내부 구조

힙 페이지는 다음처럼 생긴다.

```text
[heap page header][slot 배열 ...][빈 공간][row 데이터들 ...]
```

앞쪽과 뒤쪽이 서로 반대 방향으로 자란다.

- 앞쪽: 헤더와 슬롯 배열이 앞에서 뒤로 증가
- 뒤쪽: 실제 row 데이터가 뒤에서 앞으로 증가

### 힙 페이지에 저장되는 정보

#### 1. `heap_page_header_t`

페이지 전체를 관리하는 헤더다.

- `page_type`
  이 페이지가 힙 페이지임을 나타냄
- `next_heap_page_id`
  다음 힙 페이지 번호
- `slot_count`
  현재 슬롯 개수
- `free_slot_head`
  삭제 후 재사용 가능한 슬롯 리스트의 시작 슬롯 번호
- `free_space_offset`
  페이지 뒤쪽에서부터 row 데이터가 얼마나 사용되었는지 나타내는 값

#### 2. 슬롯 배열 (`slot_t`)

슬롯은 "몇 번째 row가 페이지 안의 어디에 저장돼 있는지" 알려주는 작은 메타데이터다.

각 슬롯에는 보통 이런 정보가 들어간다.

- `offset`
  실제 row 데이터가 시작하는 위치
- `status`
  현재 슬롯 상태
  `SLOT_ALIVE`, `SLOT_FREE` 같은 값
- `next_free`
  빈 슬롯 체인 연결 정보

#### 3. 실제 row 데이터

이 영역에는 `row_serialize()`된 바이트가 저장된다.

예를 들어 `users(id, name, age)` 테이블이라면 row 하나는 대략 아래 데이터를 포함한다.

- `id`
- `name`
- `age`

즉 힙 페이지는 "실제 데이터 창고"다.

### row 1개가 들어간 뒤 예시

`page_size = 4096`, `row_size = 44`라고 가정하면 page 1은 대략 이렇게 보인다.

```text
page 1
+---------------------------+  offset 0
| heap_page_header_t        |
| page_type = HEAP          |
| next_heap_page_id = 0     |
| slot_count = 1            |
| free_slot_head = NONE     |
| free_space_offset = 44    |
+---------------------------+
| slot[0]                   |
| offset = 4052             |
| status = SLOT_ALIVE       |
| next_free = SLOT_NONE     |
+---------------------------+
|         빈 공간           |
+---------------------------+
| row 0                     |
| id   = 1                  |
| name = "kim"              |
| age  = 20                 |
+---------------------------+  offset 4096
```

## 2번 페이지: 루트 리프 페이지

2번 페이지는 B+트리의 첫 루트 리프 페이지다.

이 페이지는 실제 row 데이터를 저장하지 않는다. 대신 "어떤 key를 찾으면 어느 힙 페이지의 어느 슬롯으로 가야 하는지"를 저장한다.

즉 인덱스 역할을 한다.

### 루트 리프 페이지에 저장되는 정보

#### 1. `leaf_page_header_t`

- `page_type`
  이 페이지가 leaf page임을 나타냄
- `parent_page_id`
  부모 페이지 번호
  루트라면 0
- `key_count`
  현재 저장된 key 개수
- `next_leaf_page_id`
  오른쪽 이웃 leaf 페이지 번호
- `prev_leaf_page_id`
  왼쪽 이웃 leaf 페이지 번호

#### 2. `leaf_entry_t` 배열

각 엔트리는 다음 정보를 가진다.

- `key`
  보통 row의 id 값
- `row_ref`
  실제 row가 있는 위치
  `page_id`, `slot_id`로 구성됨

즉 leaf page는 "검색용 주소록"에 가깝다.

### row 1개가 들어간 뒤 예시

첫 row가 들어가면 루트 리프 페이지에는 대략 이런 정보가 저장된다.

```text
page 2
+-----------------------------+
| leaf_page_header_t          |
| page_type = LEAF            |
| parent_page_id = 0          |
| key_count = 1               |
| next_leaf_page_id = 0       |
| prev_leaf_page_id = 0       |
+-----------------------------+
| leaf_entry[0]               |
| key = 1                     |
| row_ref.page_id = 1         |
| row_ref.slot_id = 0         |
+-----------------------------+
| 나머지 빈 공간              |
```

이 뜻은 다음과 같다.

- `id = 1`인 row를 찾고 싶으면
- 1번 힙 페이지의 0번 슬롯으로 가라

## 세 페이지의 역할 비교

- 0번 페이지
  DB 전체 상태와 스키마 정보 저장
- 1번 페이지
  실제 row 데이터 저장
- 2번 페이지
  row를 빠르게 찾기 위한 첫 인덱스 페이지

한 문장으로 줄이면 이렇다.

- 헤더 페이지는 "DB 설명서"
- 힙 페이지는 "실제 데이터 창고"
- 루트 리프 페이지는 "데이터 위치를 찾는 인덱스"

## 데이터 조회 흐름 예시

예를 들어 `id = 1`을 조회하면 흐름은 아래와 같다.

```text
1. page 2에서 key=1 검색
2. row_ref = (page_id=1, slot_id=0) 획득
3. page 1로 이동
4. slot 0이 가리키는 offset 위치에서 실제 row 읽기
5. 결과: (1, "kim", 20)
```

즉 leaf page는 "어디로 가야 하는지" 알려주고, heap page는 "실제 데이터"를 제공한다.

## 힙 페이지가 가득 차면

page 1에 더 이상 새 row를 넣을 공간이 없으면 시스템은 기존 힙 페이지들을 먼저 확인한다.

- 삭제된 row 때문에 재사용 가능한 슬롯이 있는지 확인
- 새 슬롯과 row를 넣을 만큼 빈 공간이 있는지 확인

둘 다 안 되면 새 힙 페이지를 하나 할당해서 힙 체인 뒤에 연결한다.

예를 들면:

```text
처음:
first_heap_page_id -> page 1 -> NULL

page 1이 가득 참:
first_heap_page_id -> page 1 -> page 3 -> NULL
```

이후 새 row는 새 힙 페이지에 저장된다.

## 정리

이 프로젝트에서 새 DB 생성 직후 만들어지는 3개 페이지는 서로 역할이 다르다.

- 0번 페이지는 데이터베이스 전체 메타데이터
- 1번 페이지는 실제 행 데이터 저장
- 2번 페이지는 인덱스의 시작점

이 구조를 이해하면 `pager_open()`, `heap_insert()`, `bptree_insert()`가 각각 무슨 책임을 가지는지 훨씬 쉽게 따라갈 수 있다.
