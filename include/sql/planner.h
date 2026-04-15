#ifndef PLANNER_H
#define PLANNER_H

#include "statement.h"

typedef enum {
    ACCESS_PATH_TABLE_SCAN,
    ACCESS_PATH_INDEX_LOOKUP,
    ACCESS_PATH_INDEX_DELETE,
    ACCESS_PATH_INSERT,
    ACCESS_PATH_CREATE_TABLE
} access_path_t;

typedef struct {
    access_path_t access_path;
} plan_t;

plan_t planner_create_plan(const statement_t *stmt);
const char *access_path_name(access_path_t ap);

#endif /* PLANNER_H */
