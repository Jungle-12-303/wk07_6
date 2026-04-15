/*
 * statement.h — SQL 문 파싱 결과 구조체
 *
 * 파서가 SQL 문자열을 분석한 결과를 statement_t에 저장한다.
 * 실행기(executor)와 플래너(planner)가 이 구조체를 입력으로 사용한다.
 */

#ifndef STATEMENT_H
#define STATEMENT_H

#include "../storage/page_format.h"
#include "../storage/schema.h"
#include <stdint.h>
#include <stdbool.h>

/* SQL 문 유형 */
typedef enum {
    STMT_CREATE_TABLE,  /* CREATE TABLE */
    STMT_INSERT,        /* INSERT INTO */
    STMT_SELECT,        /* SELECT */
    STMT_DELETE,        /* DELETE FROM */
    STMT_EXPLAIN        /* EXPLAIN (내부 SQL을 감싸는 래퍼) */
} statement_type_t;

/* WHERE 절 조건 종류 */
typedef enum {
    PREDICATE_NONE,     /* WHERE 절 없음 → 전체 스캔 */
    PREDICATE_ID_EQ,    /* WHERE id = N → B+ tree 인덱스 사용 */
    PREDICATE_FIELD_EQ  /* WHERE <필드> = <값> → 테이블 스캔 */
} predicate_kind_t;

/* CREATE TABLE에서 파싱된 컬럼 정의 */
typedef struct {
    char     name[32];   /* 컬럼 이름 */
    uint8_t  type;       /* 컬럼 타입 (column_type_t) */
    uint16_t size;       /* 바이트 크기 (VARCHAR(N)의 N) */
} column_def_t;

/*
 * 파싱된 SQL 문을 나타내는 구조체.
 * 모든 SQL 유형의 필드를 하나의 구조체에 통합한다.
 */
typedef struct {
    statement_type_t  type;              /* SQL 문 유형 */
    predicate_kind_t  predicate_kind;    /* WHERE 절 조건 종류 */
    char              table_name[32];    /* 대상 테이블 이름 */

    /* INSERT: 삽입할 값 (문자열 형태) */
    char              insert_values[MAX_COLUMNS][256];
    uint16_t          insert_value_count; /* 값 개수 */

    /* WHERE 절 */
    char              pred_field[32];    /* 조건 필드 이름 */
    char              pred_value[256];   /* 조건 비교 값 (문자열) */
    uint64_t          pred_id;           /* id 조건일 때의 정수 값 */

    /* CREATE TABLE: 컬럼 정의 */
    column_def_t      col_defs[MAX_COLUMNS];
    uint16_t          col_count;         /* 정의된 컬럼 수 */

    /* SELECT */
    bool              select_all;        /* SELECT * 여부 */

    /* EXPLAIN: 내부 SQL 정보 */
    statement_type_t  inner_type;        /* 내부 SQL의 유형 */
    predicate_kind_t  inner_predicate;   /* 내부 SQL의 조건 종류 */
} statement_t;

#endif /* STATEMENT_H */
