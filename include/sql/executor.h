#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "statement.h"
#include "planner.h"
#include "../storage/pager.h"

typedef struct {
    int   status;   /* 0=ok, -1=error */
    char  message[512];
} exec_result_t;

exec_result_t execute(pager_t *pager, statement_t *stmt);

#endif /* EXECUTOR_H */
