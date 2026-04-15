#include "sql/parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static void trim(char *s)
{
    /* trim trailing whitespace and semicolons */
    size_t len = strlen(s);
    while (len > 0 && (isspace((unsigned char)s[len-1]) || s[len-1] == ';')) {
        s[--len] = '\0';
    }
}

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

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int parse_create_table(const char *input, statement_t *stmt)
{
    /* CREATE TABLE <name> (col1 TYPE, col2 TYPE(N), ...) */
    const char *p = skip_ws(input);
    /* extract table name */
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
    p++; /* skip ( */

    stmt->col_count = 0;

    /* always add id BIGINT as system column */
    /* (will be done at executor level to allow explicit definition) */

    while (*p && *p != ')') {
        p = skip_ws(p);
        if (*p == ')') {
            break;
        }

        /* column name */
        column_def_t *cd = &stmt->col_defs[stmt->col_count];
        memset(cd, 0, sizeof(*cd));
        i = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ')' && i < 31) {
            cd->name[i++] = *p++;
        }
        cd->name[i] = '\0';

        p = skip_ws(p);

        /* type */
        char type_str[32] = {0};
        i = 0;
        while (*p && *p != ',' && *p != ')' && *p != '(' && !isspace((unsigned char)*p) && i < 31) {
            type_str[i++] = *p++;
        }
        type_str[i] = '\0';

        if (strcasecmp_n(type_str, "INT", 3) == 0 && strlen(type_str) == 3) {
            cd->type = COL_TYPE_INT;
            cd->size = 4;
        } else if (strcasecmp_n(type_str, "BIGINT", 6) == 0) {
            cd->type = COL_TYPE_BIGINT;
            cd->size = 8;
        } else if (strcasecmp_n(type_str, "VARCHAR", 7) == 0) {
            cd->type = COL_TYPE_VARCHAR;
            cd->size = 32; /* default */
            p = skip_ws(p);
            if (*p == '(') {
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
            return -1;
        }

        stmt->col_count++;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }

    return 0;
}

static int parse_insert(const char *input, statement_t *stmt)
{
    /* INSERT INTO <table> VALUES (v1, v2, ...) */
    const char *p = skip_ws(input);
    if (strcasecmp_n(p, "INTO", 4) != 0) {
        return -1;
    }
    p = skip_ws(p + 4);

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

    stmt->insert_value_count = 0;
    while (*p && *p != ')') {
        p = skip_ws(p);
        if (*p == ')') {
            break;
        }

        char *val = stmt->insert_values[stmt->insert_value_count];
        i = 0;
        if (*p == '\'') {
            p++; /* skip opening quote */
            while (*p && *p != '\'' && i < 255) {
                val[i++] = *p++;
            }
            if (*p == '\'') {
                p++;
            }
        } else {
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

    /* field name */
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

    /* value */
    i = 0;
    if (*p == '\'') {
        p++;
        while (*p && *p != '\'' && i < 255) {
            stmt->pred_value[i++] = *p++;
        }
        if (*p == '\'') {
            p++;
        }
    } else {
        while (*p && !isspace((unsigned char)*p) && *p != ';' && i < 255) {
            stmt->pred_value[i++] = *p++;
        }
    }
    stmt->pred_value[i] = '\0';

    if (strcasecmp_n(stmt->pred_field, "id", 2) == 0 && strlen(stmt->pred_field) == 2) {
        stmt->predicate_kind = PREDICATE_ID_EQ;
        stmt->pred_id = (uint64_t)atoll(stmt->pred_value);
    } else {
        stmt->predicate_kind = PREDICATE_FIELD_EQ;
    }
    return 0;
}

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

    int i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != ';' && i < 31) {
        stmt->table_name[i++] = *p++;
    }
    stmt->table_name[i] = '\0';

    return parse_where(skip_ws(p), stmt);
}

int parse(const char *input, statement_t *stmt)
{
    memset(stmt, 0, sizeof(*stmt));

    char buf[1024];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim(buf);

    const char *p = skip_ws(buf);
    if (*p == '\0') {
        return -1;
    }

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
        /* parse the inner statement */
        p = skip_ws(p + 7);
        statement_t inner;
        if (parse(p, &inner) != 0) {
            return -1;
        }
        stmt->inner_type = inner.type;
        stmt->inner_predicate = inner.predicate_kind;
        memcpy(stmt->table_name, inner.table_name, 32);
        memcpy(stmt->pred_field, inner.pred_field, 32);
        memcpy(stmt->pred_value, inner.pred_value, 256);
        stmt->pred_id = inner.pred_id;
        stmt->predicate_kind = inner.predicate_kind;
        return 0;
    }

    return -1;
}
