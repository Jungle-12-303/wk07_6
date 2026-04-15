#ifndef STATEMENT_H
#define STATEMENT_H

#include "../storage/page_format.h"
#include "../storage/schema.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    STMT_CREATE_TABLE,
    STMT_INSERT,
    STMT_SELECT,
    STMT_DELETE,
    STMT_EXPLAIN
} statement_type_t;

typedef enum {
    PREDICATE_NONE,
    PREDICATE_ID_EQ,
    PREDICATE_FIELD_EQ
} predicate_kind_t;

typedef struct {
    char     name[32];
    uint8_t  type;       /* column_type_t */
    uint16_t size;       /* for VARCHAR(N) */
} column_def_t;

typedef struct {
    statement_type_t  type;
    predicate_kind_t  predicate_kind;
    char              table_name[32];
    /* INSERT values (string form) */
    char              insert_values[MAX_COLUMNS][256];
    uint16_t          insert_value_count;
    /* WHERE */
    char              pred_field[32];
    char              pred_value[256];
    uint64_t          pred_id;
    /* CREATE TABLE */
    column_def_t      col_defs[MAX_COLUMNS];
    uint16_t          col_count;
    /* SELECT */
    bool              select_all;
    /* inner EXPLAIN wraps another statement type */
    statement_type_t  inner_type;
    predicate_kind_t  inner_predicate;
} statement_t;

#endif /* STATEMENT_H */
