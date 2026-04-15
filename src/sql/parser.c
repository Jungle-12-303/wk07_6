/*
 * parser.c — 재귀 하강 SQL 파서
 *
 * 역할:
 *   사용자가 입력한 SQL 문자열을 분석하여 statement_t 구조체로 변환한다.
 *   지원하는 SQL 구문: CREATE TABLE, INSERT, SELECT, DELETE, EXPLAIN
 *
 * 파싱 방식:
 *   if-else 기반 키워드 비교로 SQL 유형을 판별한 뒤,
 *   각 유형별 전용 파서 함수를 호출하는 재귀 하강(recursive descent) 방식이다.
 *   토크나이저를 별도로 두지 않고, 포인터를 직접 이동시키며 토큰을 추출한다.
 *   (switch문이 아닌 if-else 비교를 사용한다)
 *
 * 파싱 흐름 예시: "INSERT INTO users VALUES ('Alice', 25)"
 *   1. trim() → 세미콜론/공백 제거
 *   2. skip_ws() → 앞쪽 공백 건너뛰기
 *   3. strcasecmp_n(p, "INSERT", 6) → 매치!
 *   4. stmt->type = STMT_INSERT
 *   5. parse_insert() 호출:
 *      - "INTO" 확인 → "users" 추출 → stmt->table_name = "users"
 *      - "VALUES" 확인 → "(" 확인
 *      - 'Alice' → stmt->insert_values[0] = "Alice"
 *      - 25 → stmt->insert_values[1] = "25"
 *      - insert_value_count = 2
 *
 * 키워드 비교:
 *   대소문자를 구분하지 않는 strcasecmp_n()을 사용한다.
 *   예: "select", "SELECT", "Select" 모두 동일하게 처리된다.
 */

#include "sql/parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/*
 * trim - 문자열 끝의 공백과 세미콜론을 제거한다.
 *
 * 예시: "SELECT * FROM users;\n" → "SELECT * FROM users"
 * 예시: "INSERT INTO t VALUES (1)  " → "INSERT INTO t VALUES (1)"
 */
static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (isspace((unsigned char)s[len-1]) || s[len-1] == ';')) {
        s[--len] = '\0';
    }
}

/*
 * strcasecmp_n - n바이트까지 대소문자를 무시하고 문자열을 비교한다.
 *
 * 예시: strcasecmp_n("Select", "SELECT", 6) → 0 (일치)
 * 예시: strcasecmp_n("INSERT", "INTO", 4) → 음수 (불일치)
 */
static int strcasecmp_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int d = tolower((unsigned char)a[i]) - tolower((unsigned char)b[i]);
        if (d != 0) {
            return d;
        }
        if (a[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

/* 공백 문자를 건너뛴다 */
static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/* ══════════════════════════════════════════════════════════════════════
 *  CREATE TABLE 파서
 *
 *  형식: CREATE TABLE <테이블명> (컬럼1 타입, 컬럼2 타입(크기), ...)
 *
 *  예시: CREATE TABLE users (name VARCHAR(32), age INT)
 *    → table_name = "users"
 *    → col_defs[0] = {name="name", type=VARCHAR, size=32}
 *    → col_defs[1] = {name="age", type=INT, size=4}
 *    → col_count = 2
 *
 *  지원 타입과 크기:
 *    INT        → COL_TYPE_INT,     4바이트
 *    BIGINT     → COL_TYPE_BIGINT,  8바이트
 *    VARCHAR    → COL_TYPE_VARCHAR, 기본 32바이트
 *    VARCHAR(N) → COL_TYPE_VARCHAR, N바이트
 *
 *  id 컬럼은 executor에서 자동 추가되므로 여기서는 처리하지 않는다.
 * ══════════════════════════════════════════════════════════════════════ */
static int parse_create_table(const char *input, statement_t *stmt)
{
    const char *p = skip_ws(input);

    /* 테이블 이름 추출 */
    char tname[32] = {0};
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != '(' && i < 31) {
        tname[i++] = *p++;
    }
    tname[i] = '\0';
    strncpy(stmt->table_name, tname, 31);

    p = skip_ws(p);
    if (*p != '(') {
        return -1;
    }
    p++; /* '(' 건너뛰기 */

    stmt->col_count = 0;

    /* 컬럼 정의를 하나씩 파싱 (쉼표로 구분) */
    while (*p && *p != ')') {
        p = skip_ws(p);
        if (*p == ')') {
            break;
        }

        /* 컬럼 이름 추출: "name" 부분 */
        column_def_t *cd = &stmt->col_defs[stmt->col_count];
        memset(cd, 0, sizeof(*cd));
        i = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ')' && i < 31) {
            cd->name[i++] = *p++;
        }
        cd->name[i] = '\0';

        p = skip_ws(p);

        /* 타입 문자열 추출: "VARCHAR", "INT", "BIGINT" */
        char type_str[32] = {0};
        i = 0;
        while (*p && *p != ',' && *p != ')' && *p != '(' && !isspace((unsigned char)*p) && i < 31) {
            type_str[i++] = *p++;
        }
        type_str[i] = '\0';

        /* 타입 판별 및 크기 설정 */
        if (strcasecmp_n(type_str, "INT", 3) == 0 && strlen(type_str) == 3) {
            cd->type = COL_TYPE_INT;
            cd->size = 4;   /* int32_t = 4바이트 */
        } else if (strcasecmp_n(type_str, "BIGINT", 6) == 0) {
            cd->type = COL_TYPE_BIGINT;
            cd->size = 8;   /* int64_t = 8바이트 */
        } else if (strcasecmp_n(type_str, "VARCHAR", 7) == 0) {
            cd->type = COL_TYPE_VARCHAR;
            cd->size = 32;  /* 기본값 32바이트 */
            p = skip_ws(p);
            if (*p == '(') {
                /* VARCHAR(64) → size=64 */
                p++;
                cd->size = (uint16_t)atoi(p);
                while (*p && *p != ')') {
                    p++;
                }
                if (*p == ')') {
                    p++;
                }
            }
        } else {
            return -1; /* 알 수 없는 타입 */
        }

        stmt->col_count++;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  INSERT 파서
 *
 *  형식: INSERT INTO <테이블명> VALUES (값1, 값2, ...)
 *
 *  예시: INSERT INTO users VALUES ('Alice', 25)
 *    → table_name = "users"
 *    → insert_values[0] = "Alice"  (작은따옴표 제거됨)
 *    → insert_values[1] = "25"     (문자열로 저장, executor에서 변환)
 *    → insert_value_count = 2
 *
 *  id 값은 executor에서 자동 할당하므로 VALUES에 포함하지 않는다.
 * ══════════════════════════════════════════════════════════════════════ */
static int parse_insert(const char *input, statement_t *stmt)
{
    const char *p = skip_ws(input);
    if (strcasecmp_n(p, "INTO", 4) != 0) {
        return -1;
    }
    p = skip_ws(p + 4);

    /* 테이블 이름 추출 */
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 31) {
        stmt->table_name[i++] = *p++;
    }
    stmt->table_name[i] = '\0';

    p = skip_ws(p);
    if (strcasecmp_n(p, "VALUES", 6) != 0) {
        return -1;
    }
    p = skip_ws(p + 6);

    if (*p != '(') {
        return -1;
    }
    p++;

    /* 값 목록 파싱 */
    stmt->insert_value_count = 0;
    while (*p && *p != ')') {
        p = skip_ws(p);
        if (*p == ')') {
            break;
        }

        char *val = stmt->insert_values[stmt->insert_value_count];
        i = 0;
        if (*p == '\'') {
            /*
             * 작은따옴표로 감싼 문자열 값
             * 예: 'Alice' → val = "Alice" (따옴표 제거)
             */
            p++;
            while (*p && *p != '\'' && i < 255) {
                val[i++] = *p++;
            }
            if (*p == '\'') {
                p++;
            }
        } else {
            /*
             * 숫자 또는 기타 값
             * 예: 25 → val = "25"
             */
            while (*p && *p != ',' && *p != ')' && !isspace((unsigned char)*p) && i < 255) {
                val[i++] = *p++;
            }
        }
        val[i] = '\0';
        stmt->insert_value_count++;

        p = skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  WHERE 절 파서
 *
 *  형식: WHERE <필드> = <값>
 *
 *  조건 분류 결과:
 *    WHERE 없음       → PREDICATE_NONE     (전체 스캔)
 *    WHERE id = 42    → PREDICATE_ID_EQ    (B+ tree 인덱스 사용, O(log n))
 *    WHERE name='Bob' → PREDICATE_FIELD_EQ (테이블 풀 스캔, O(n))
 *
 *  예시: "WHERE id = 42"
 *    → pred_field = "id"
 *    → pred_value = "42"
 *    → predicate_kind = PREDICATE_ID_EQ
 *    → pred_id = 42 (atoll로 변환)
 * ══════════════════════════════════════════════════════════════════════ */
static int parse_where(const char *p, statement_t *stmt)
{
    p = skip_ws(p);
    if (*p == '\0' || *p == ';') {
        stmt->predicate_kind = PREDICATE_NONE;
        return 0;
    }

    if (strcasecmp_n(p, "WHERE", 5) != 0) {
        return -1;
    }
    p = skip_ws(p + 5);

    /* 필드 이름 추출: "id" 또는 "name" */
    int i = 0;
    while (*p && *p != '=' && !isspace((unsigned char)*p) && i < 31) {
        stmt->pred_field[i++] = *p++;
    }
    stmt->pred_field[i] = '\0';

    p = skip_ws(p);
    if (*p != '=') {
        return -1;
    }
    p = skip_ws(p + 1);

    /* 비교 값 추출 */
    i = 0;
    if (*p == '\'') {
        /* 문자열 값: 'Alice' → "Alice" */
        p++;
        while (*p && *p != '\'' && i < 255) {
            stmt->pred_value[i++] = *p++;
        }
        if (*p == '\'') {
            p++;
        }
    } else {
        /* 숫자 값: 42 → "42" */
        while (*p && !isspace((unsigned char)*p) && *p != ';' && i < 255) {
            stmt->pred_value[i++] = *p++;
        }
    }
    stmt->pred_value[i] = '\0';

    /*
     * id 필드이면 인덱스 조건, 그 외이면 필드 비교 조건으로 분류
     * 이 분류가 플래너에서 INDEX_LOOKUP vs TABLE_SCAN 결정의 핵심이다.
     */
    if (strcasecmp_n(stmt->pred_field, "id", 2) == 0 && strlen(stmt->pred_field) == 2) {
        stmt->predicate_kind = PREDICATE_ID_EQ;
        stmt->pred_id = (uint64_t)atoll(stmt->pred_value);
    } else {
        stmt->predicate_kind = PREDICATE_FIELD_EQ;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  SELECT / DELETE 파서
 *
 *  SELECT 형식: SELECT * FROM <테이블명> [WHERE <필드> = <값>]
 *  DELETE 형식: DELETE FROM <테이블명> [WHERE <필드> = <값>]
 *
 *  예시: SELECT * FROM users WHERE id = 3
 *    → select_all = true, table_name = "users"
 *    → pred_field = "id", pred_id = 3, predicate_kind = PREDICATE_ID_EQ
 *
 *  예시: DELETE FROM users WHERE name = 'Alice'
 *    → table_name = "users"
 *    → pred_field = "name", pred_value = "Alice"
 *    → predicate_kind = PREDICATE_FIELD_EQ
 * ══════════════════════════════════════════════════════════════════════ */
static int parse_select(const char *input, statement_t *stmt)
{
    const char *p = skip_ws(input);
    if (*p == '*') {
        stmt->select_all = true;
        p = skip_ws(p + 1);
    }
    if (strcasecmp_n(p, "FROM", 4) != 0) {
        return -1;
    }
    p = skip_ws(p + 4);

    /* 테이블 이름 추출 */
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != ';' && i < 31) {
        stmt->table_name[i++] = *p++;
    }
    stmt->table_name[i] = '\0';

    return parse_where(skip_ws(p), stmt);
}

static int parse_delete(const char *input, statement_t *stmt)
{
    const char *p = skip_ws(input);
    if (strcasecmp_n(p, "FROM", 4) != 0) {
        return -1;
    }
    p = skip_ws(p + 4);

    /* 테이블 이름 추출 */
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != ';' && i < 31) {
        stmt->table_name[i++] = *p++;
    }
    stmt->table_name[i] = '\0';

    return parse_where(skip_ws(p), stmt);
}

/* ══════════════════════════════════════════════════════════════════════
 *  메인 파서 (진입점)
 *
 *  SQL 문자열의 첫 키워드로 유형을 판별하고 전용 파서를 호출한다.
 *
 *  키워드 판별:
 *    "CREATE" → parse_create_table()
 *    "INSERT" → parse_insert()
 *    "SELECT" → parse_select()
 *    "DELETE" → parse_delete()
 *    "EXPLAIN" → 재귀적으로 내부 SQL을 파싱
 *
 *  EXPLAIN 예시: "EXPLAIN SELECT * FROM users WHERE id=3"
 *    → stmt->type = STMT_EXPLAIN
 *    → inner_type = STMT_SELECT
 *    → inner_predicate = PREDICATE_ID_EQ
 *    → table_name = "users", pred_id = 3
 * ══════════════════════════════════════════════════════════════════════ */
int parse(const char *input, statement_t *stmt)
{
    memset(stmt, 0, sizeof(*stmt));

    /* 입력 문자열을 복사하고 끝의 공백/세미콜론 제거 */
    char buf[1024];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim(buf);

    const char *p = skip_ws(buf);
    if (*p == '\0') {
        return -1;
    }

    /* 키워드 기반 SQL 유형 판별 */
    if (strcasecmp_n(p, "CREATE", 6) == 0) {
        p = skip_ws(p + 6);
        if (strcasecmp_n(p, "TABLE", 5) != 0) {
            return -1;
        }
        stmt->type = STMT_CREATE_TABLE;
        return parse_create_table(skip_ws(p + 5), stmt);
    }
    if (strcasecmp_n(p, "INSERT", 6) == 0) {
        stmt->type = STMT_INSERT;
        return parse_insert(skip_ws(p + 6), stmt);
    }
    if (strcasecmp_n(p, "SELECT", 6) == 0) {
        stmt->type = STMT_SELECT;
        return parse_select(skip_ws(p + 6), stmt);
    }
    if (strcasecmp_n(p, "DELETE", 6) == 0) {
        stmt->type = STMT_DELETE;
        return parse_delete(skip_ws(p + 6), stmt);
    }
    if (strcasecmp_n(p, "EXPLAIN", 7) == 0) {
        stmt->type = STMT_EXPLAIN;
        /*
         * EXPLAIN의 내부 SQL을 재귀적으로 파싱한다.
         * 예: "SELECT * FROM users WHERE id=3" 부분을 parse()에 다시 전달
         */
        p = skip_ws(p + 7);
        statement_t inner;
        if (parse(p, &inner) != 0) {
            return -1;
        }
        /* 내부 파싱 결과를 EXPLAIN 문에 복사 */
        stmt->inner_type = inner.type;
        stmt->inner_predicate = inner.predicate_kind;
        memcpy(stmt->table_name, inner.table_name, 32);
        memcpy(stmt->pred_field, inner.pred_field, 32);
        memcpy(stmt->pred_value, inner.pred_value, 256);
        stmt->pred_id = inner.pred_id;
        stmt->predicate_kind = inner.predicate_kind;
        return 0;
    }

    return -1; /* 알 수 없는 SQL 구문 */
}
