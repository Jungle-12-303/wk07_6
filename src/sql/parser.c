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

/* 문자열 끝의 공백과 세미콜론을 제거한다 */
static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (isspace((unsigned char)s[len-1]) || s[len-1] == ';')) {
        s[--len] = '\0';
    }
}

/* n바이트까지 대소문자를 무시하고 문자열을 비교한다 */
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

/*
 * CREATE TABLE 구문을 파싱한다.
 *
 * 형식: CREATE TABLE <테이블명> (컬럼1 타입, 컬럼2 타입(크기), ...)
 *
 * 지원 타입:
 *   INT        → 4바이트 정수
 *   BIGINT     → 8바이트 정수
 *   VARCHAR(N) → N바이트 가변 문자열 (기본값 32)
 *
 * id 컬럼은 executor에서 자동 추가되므로 여기서는 처리하지 않는다.
 */
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

    /* 컬럼 정의를 하나씩 파싱 */
    while (*p && *p != ')') {
        p = skip_ws(p);
        if (*p == ')') {
            break;
        }

        /* 컬럼 이름 추출 */
        column_def_t *cd = &stmt->col_defs[stmt->col_count];
        memset(cd, 0, sizeof(*cd));
        i = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ')' && i < 31) {
            cd->name[i++] = *p++;
        }
        cd->name[i] = '\0';

        p = skip_ws(p);

        /* 타입 문자열 추출 */
        char type_str[32] = {0};
        i = 0;
        while (*p && *p != ',' && *p != ')' && *p != '(' && !isspace((unsigned char)*p) && i < 31) {
            type_str[i++] = *p++;
        }
        type_str[i] = '\0';

        /* 타입 판별 및 크기 설정 */
        if (strcasecmp_n(type_str, "INT", 3) == 0 && strlen(type_str) == 3) {
            cd->type = COL_TYPE_INT;
            cd->size = 4;
        } else if (strcasecmp_n(type_str, "BIGINT", 6) == 0) {
            cd->type = COL_TYPE_BIGINT;
            cd->size = 8;
        } else if (strcasecmp_n(type_str, "VARCHAR", 7) == 0) {
            cd->type = COL_TYPE_VARCHAR;
            cd->size = 32; /* VARCHAR의 기본 크기 */
            p = skip_ws(p);
            if (*p == '(') {
                /* VARCHAR(N): 괄호 안의 숫자를 크기로 사용 */
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

/*
 * INSERT 구문을 파싱한다.
 *
 * 형식: INSERT INTO <테이블명> VALUES (값1, 값2, ...)
 *
 * 문자열 값은 작은따옴표로 감싸야 한다: 'hello'
 * 숫자 값은 그대로 사용한다: 42
 * id 값은 자동 할당되므로 VALUES에 포함하지 않는다.
 */
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
            /* 작은따옴표로 감싼 문자열 값 */
            p++;
            while (*p && *p != '\'' && i < 255) {
                val[i++] = *p++;
            }
            if (*p == '\'') {
                p++;
            }
        } else {
            /* 숫자 또는 기타 값 */
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

/*
 * WHERE 절을 파싱한다.
 *
 * 형식: WHERE <필드> = <값>
 *
 * 결과:
 *   필드가 "id"이면 → PREDICATE_ID_EQ (B+ tree 인덱스 검색 가능)
 *   그 외 필드이면  → PREDICATE_FIELD_EQ (테이블 풀 스캔 필요)
 *   WHERE 절 없으면 → PREDICATE_NONE
 */
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

    /* 필드 이름 추출 */
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
        /* 문자열 값 */
        p++;
        while (*p && *p != '\'' && i < 255) {
            stmt->pred_value[i++] = *p++;
        }
        if (*p == '\'') {
            p++;
        }
    } else {
        /* 숫자 값 */
        while (*p && !isspace((unsigned char)*p) && *p != ';' && i < 255) {
            stmt->pred_value[i++] = *p++;
        }
    }
    stmt->pred_value[i] = '\0';

    /* id 필드이면 인덱스 조건, 그 외이면 필드 비교 조건으로 분류 */
    if (strcasecmp_n(stmt->pred_field, "id", 2) == 0 && strlen(stmt->pred_field) == 2) {
        stmt->predicate_kind = PREDICATE_ID_EQ;
        stmt->pred_id = (uint64_t)atoll(stmt->pred_value);
    } else {
        stmt->predicate_kind = PREDICATE_FIELD_EQ;
    }
    return 0;
}

/*
 * SELECT 구문을 파싱한다.
 *
 * 형식: SELECT * FROM <테이블명> [WHERE <필드> = <값>]
 *
 * 현재는 SELECT * 만 지원하며, 개별 컬럼 선택은 미지원이다.
 */
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

/*
 * DELETE 구문을 파싱한다.
 *
 * 형식: DELETE FROM <테이블명> WHERE <필드> = <값>
 *
 * WHERE 절이 없으면 전체 행에 대해 스캔 후 삭제한다.
 */
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

/*
 * SQL 문자열을 파싱하여 statement_t에 결과를 저장한다.
 *
 * 파싱 흐름:
 *   1. 입력 문자열을 복사하고 trim() 처리
 *   2. 첫 번째 키워드로 SQL 유형 판별
 *   3. 유형별 전용 파서 함수 호출
 *
 * EXPLAIN의 경우 내부 SQL을 재귀적으로 파싱한다.
 * 예: "EXPLAIN SELECT * FROM users" → inner_type = STMT_SELECT
 */
int parse(const char *input, statement_t *stmt)
{
    memset(stmt, 0, sizeof(*stmt));

    /* 입력 문자열 복사 및 정리 */
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
        /* 내부 SQL을 재귀적으로 파싱 */
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
