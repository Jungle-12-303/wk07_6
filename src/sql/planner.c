#include "sql/planner.h"

plan_t planner_create_plan(const statement_t *stmt) {
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
            /* plan based on inner statement */
            statement_t inner;
            inner.type = stmt->inner_type;
            inner.predicate_kind = stmt->inner_predicate;
            return planner_create_plan(&inner);
        }
    }

    return plan;
}

const char *access_path_name(access_path_t ap) {
    switch (ap) {
        case ACCESS_PATH_TABLE_SCAN:    return "TABLE_SCAN";
        case ACCESS_PATH_INDEX_LOOKUP:  return "INDEX_LOOKUP";
        case ACCESS_PATH_INDEX_DELETE:  return "INDEX_DELETE";
        case ACCESS_PATH_INSERT:        return "INSERT";
        case ACCESS_PATH_CREATE_TABLE:  return "CREATE_TABLE";
    }
    return "UNKNOWN";
}
