/*
 * planner.c — 규칙 기반 쿼리 플래너
 *
 * 역할:
 *   파싱된 statement_t를 분석하여 실행 계획(plan_t)을 생성한다.
 *   접근 경로(access path)를 결정하는 것이 핵심이다.
 *
 * 접근 경로 결정 규칙:
 *
 *   SQL 유형 + 조건            → 접근 경로              → 시간 복잡도
 *   ─────────────────────────────────────────────────────────────────
 *   CREATE TABLE               → CREATE_TABLE           → O(1)
 *   INSERT                     → INSERT                 → O(log n)
 *   SELECT + WHERE id=N        → INDEX_LOOKUP           → O(log n)
 *   SELECT + WHERE name='X'    → TABLE_SCAN             → O(n)
 *   SELECT + WHERE 없음        → TABLE_SCAN             → O(n)
 *   DELETE + WHERE id=N        → INDEX_DELETE            → O(log n)
 *   DELETE + WHERE name='X'    → TABLE_SCAN             → O(n)
 *
 * INDEX_LOOKUP vs TABLE_SCAN:
 *   - INDEX_LOOKUP: B+ tree로 O(log n) 검색. 100만 건에서도 3번의 페이지 접근.
 *   - TABLE_SCAN: 힙 전체를 순회. 100만 건이면 100만 행을 모두 읽어야 함.
 *   - id 필드에만 인덱스가 있으므로 WHERE id=N만 인덱스를 사용할 수 있다.
 *
 * EXPLAIN 처리:
 *   EXPLAIN SELECT * FROM users WHERE id=3
 *   → inner_type=STMT_SELECT, inner_predicate=PREDICATE_ID_EQ
 *   → 재귀적으로 planner_create_plan() 호출 → INDEX_LOOKUP
 */

#include "sql/planner.h"

/*
 * planner_create_plan - statement_t를 분석하여 실행 계획을 생성한다.
 *
 * 예시 1: SELECT * FROM users WHERE id = 3
 *   stmt->type = STMT_SELECT
 *   stmt->predicate_kind = PREDICATE_ID_EQ
 *   → plan.access_path = ACCESS_PATH_INDEX_LOOKUP
 *
 * 예시 2: SELECT * FROM users WHERE name = 'Alice'
 *   stmt->type = STMT_SELECT
 *   stmt->predicate_kind = PREDICATE_FIELD_EQ
 *   → plan.access_path = ACCESS_PATH_TABLE_SCAN
 *
 * 예시 3: DELETE FROM users WHERE id = 5
 *   stmt->type = STMT_DELETE
 *   stmt->predicate_kind = PREDICATE_ID_EQ
 *   → plan.access_path = ACCESS_PATH_INDEX_DELETE
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
