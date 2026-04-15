/*
 * planner.c — 규칙 기반 쿼리 플래너
 *
 * 역할:
 *   파싱된 statement_t를 분석하여 실행 계획(plan_t)을 생성한다.
 *   접근 경로(access path)를 결정하는 것이 핵심이다.
 *
 * 접근 경로 결정 규칙:
 *   CREATE TABLE → ACCESS_PATH_CREATE_TABLE
 *   INSERT       → ACCESS_PATH_INSERT
 *   SELECT + id 조건  → ACCESS_PATH_INDEX_LOOKUP  (B+ tree 인덱스 검색)
 *   SELECT + 기타/없음 → ACCESS_PATH_TABLE_SCAN    (힙 전체 스캔)
 *   DELETE + id 조건  → ACCESS_PATH_INDEX_DELETE   (인덱스로 위치 찾아 삭제)
 *   DELETE + 기타/없음 → ACCESS_PATH_TABLE_SCAN    (힙 스캔 후 일괄 삭제)
 *
 * EXPLAIN 문의 경우 내부 SQL의 타입과 조건을 기반으로 재귀적으로 계획을 생성한다.
 */

#include "sql/planner.h"

/*
 * statement_t를 분석하여 실행 계획을 생성한다.
 *
 * id 필드에 대한 등가 조건(WHERE id = N)이 있으면
 * B+ tree 인덱스를 활용하는 경로를 선택하고,
 * 그 외에는 힙 전체 스캔 경로를 선택한다.
 */
plan_t planner_create_plan(const statement_t *stmt)
{
    plan_t plan;

    switch (stmt->type) {
        case STMT_CREATE_TABLE:
            plan.access_path = ACCESS_PATH_CREATE_TABLE;
            break;
        case STMT_INSERT:
            plan.access_path = ACCESS_PATH_INSERT;
            break;
        case STMT_SELECT:
            if (stmt->predicate_kind == PREDICATE_ID_EQ)
                plan.access_path = ACCESS_PATH_INDEX_LOOKUP;
            else
                plan.access_path = ACCESS_PATH_TABLE_SCAN;
            break;
        case STMT_DELETE:
            if (stmt->predicate_kind == PREDICATE_ID_EQ)
                plan.access_path = ACCESS_PATH_INDEX_DELETE;
            else
                plan.access_path = ACCESS_PATH_TABLE_SCAN;
            break;
        case STMT_EXPLAIN: {
            /* EXPLAIN: 내부 SQL을 기반으로 재귀적으로 계획 생성 */
            statement_t inner;
            inner.type = stmt->inner_type;
            inner.predicate_kind = stmt->inner_predicate;
            return planner_create_plan(&inner);
        }
    }

    return plan;
}

/* 접근 경로의 이름 문자열을 반환한다 (EXPLAIN 출력용) */
const char *access_path_name(access_path_t ap)
{
    switch (ap) {
        case ACCESS_PATH_TABLE_SCAN:    return "TABLE_SCAN";
        case ACCESS_PATH_INDEX_LOOKUP:  return "INDEX_LOOKUP";
        case ACCESS_PATH_INDEX_DELETE:  return "INDEX_DELETE";
        case ACCESS_PATH_INSERT:        return "INSERT";
        case ACCESS_PATH_CREATE_TABLE:  return "CREATE_TABLE";
    }
    return "UNKNOWN";
}
