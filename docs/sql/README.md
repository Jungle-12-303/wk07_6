# SQL 문서

이번 주 과제인 디스크 기반 SQL 처리기 + `B+` 트리 인덱스 구현을 위한
설계 문서 모음이다.

## 문서 목록

- [b-plus-tree-sql-plan.md](./b-plus-tree-sql-plan.md)
  - 과제 요구사항을 디스크 기반 SQL 엔진 관점으로 정리한 메인 문서
  - `.db` 바이너리 파일, page format, pager, heap table, on-disk `B+` 트리 구조 포함
  - SQL 실행 구조, 구현 단계, 학습 항목, 검증 계획 포함
- [system-perspective-guide.md](./system-perspective-guide.md)
  - 가상메모리, OS 파일 I/O, 메모리, CPU, 버스 관점을 이번 SQL 과제 설계 판단으로 연결한 가이드
  - 왜 page, pager, fixed-size row, row_ref, B+ 트리 fan-out이 중요한지 설명
- [sql-feature-scope-roadmap.md](./sql-feature-scope-roadmap.md)
  - 이번 프로젝트에서 고를 수 있는 SQL / DBMS 기능을 범위 선택 관점으로 정리한 문서
  - 각 기능의 의존성, 학습 가치, 실무 연결점, 일정 리스크를 비교
  - `Package 1/2/3` 추천 범위를 통해 구현 범위를 선택할 수 있게 돕는 문서

## 권장 읽는 순서

1. `b-plus-tree-sql-plan.md`
2. `system-perspective-guide.md`
3. `sql-feature-scope-roadmap.md`

## 이 문서들의 역할

- 하루 안에 구현 가능한 디스크 기반 최소 스펙을 정한다.
- 이번 프로젝트에서 `B+` 트리와 pager를 어떤 범위로 구현할지 결정한다.
- 발표와 README 데모에서 설명할 핵심 개념과 검증 포인트를 정리한다.
- 구현 전에 어떤 기능을 이번 주 범위로 선택할지 판단할 수 있게 돕는다.
