/*
 * executor.h — SQL 실행기 인터페이스
 *
 * 플래너가 결정한 접근 경로에 따라 실제 데이터 조작을 수행한다.
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "statement.h"
#include "planner.h"
#include "../storage/pager.h"

/* 실행 결과 */
typedef struct {
    int   status;        /* 실행 상태 (0 = 성공, -1 = 오류) */
    char  message[512];  /* 결과 메시지 (사용자에게 출력) */
} exec_result_t;

/* SQL 문을 실행하고 결과를 반환한다 */
exec_result_t execute(pager_t *pager, statement_t *stmt);

#endif /* EXECUTOR_H */
