/*
 * executor.c — SQL 실행기
 *
 * 역할:
 *   플래너가 결정한 접근 경로(access path)에 따라 실제 데이터 조작을 수행한다.
 *   각 접근 경로별 전용 실행 함수가 있으며, execute()가 디스패치한다.
 *
 * 실행 흐름:
 *   execute() → planner_create_plan() → 접근 경로에 따라 분기
 *     CREATE_TABLE  → exec_create_table()  : 스키마 정의
 *     INSERT        → exec_insert()        : 행 직렬화 → 힙 삽입 → B+ tree 삽입
 *     INDEX_LOOKUP  → exec_index_lookup()  : B+ tree 검색 → 힙 조회
 *     TABLE_SCAN    → exec_table_scan()    : 힙 전체 스캔
 *     INDEX_DELETE  → exec_index_delete()  : B+ tree 검색 → 힙 삭제 → 인덱스 삭제
 *     TABLE_SCAN(삭제) → exec_delete_scan(): 힙 스캔 → id 수집 → 일괄 삭제
 */

#include "sql/executor.h"
#include "storage/schema.h"
#include "storage/table.h"
#include "storage/bptree.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

/* 행 데이터를 컬럼별로 포맷하여 출력한다 */
static void print_row(const db_header_t *hdr, const row_value_t *values)
{
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (i > 0) {
            printf(" | ");
        }
        const column_meta_t *col = &hdr->columns[i];
        switch (col->type) {
            case COL_TYPE_INT:
                printf("%d", values[i].int_val);
                break;
            case COL_TYPE_BIGINT:
                printf("%" PRId64, values[i].bigint_val);
                break;
            case COL_TYPE_VARCHAR:
                printf("%s", values[i].str_val);
                break;
        }
    }
    printf("\n");
}

/* 컬럼 이름과 구분선을 출력한다 */
static void print_header(const db_header_t *hdr)
{
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (i > 0) {
            printf(" | ");
        }
        printf("%s", hdr->columns[i].name);
    }
    printf("\n");
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (i > 0) {
            printf("-+-");
        }
        for (uint16_t j = 0; j < 10; j++) {
            printf("-");
        }
    }
    printf("\n");
}

/* ── CREATE TABLE ── */

/*
 * 테이블을 생성한다.
 *
 * id BIGINT 컬럼을 시스템 컬럼으로 자동 추가한 뒤,
 * 사용자가 정의한 컬럼들을 순서대로 등록한다.
 * schema_compute_layout()으로 각 컬럼의 바이트 오프셋을 계산한다.
 *
 * 현재 단일 테이블만 지원하며, 이미 테이블이 있으면 오류를 반환한다.
 */
static exec_result_t exec_create_table(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};
    db_header_t *hdr = &pager->header;

    if (hdr->column_count > 0) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message),
                 "오류: '%s' 테이블이 이미 존재합니다", stmt->table_name);
        return res;
    }

    /* id를 첫 번째 시스템 컬럼으로 추가 */
    hdr->column_count = 0;
    column_meta_t *id_col = &hdr->columns[hdr->column_count++];
    memset(id_col, 0, sizeof(*id_col));
    strncpy(id_col->name, "id", 31);
    id_col->type = COL_TYPE_BIGINT;
    id_col->size = 8;
    id_col->is_system = 1;

    /* 사용자 정의 컬럼 등록 (id가 명시적으로 정의되었으면 건너뜀) */
    for (uint16_t i = 0; i < stmt->col_count; i++) {
        column_def_t *cd = &stmt->col_defs[i];
        if (strncmp(cd->name, "id", 32) == 0) {
            continue;
        }

        column_meta_t *col = &hdr->columns[hdr->column_count++];
        memset(col, 0, sizeof(*col));
        strncpy(col->name, cd->name, 31);
        col->type = cd->type;
        col->size = cd->size;
        col->is_system = 0;
    }

    /* 컬럼 오프셋 계산 및 row_size 결정 */
    schema_compute_layout(hdr);
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message),
             "'%s' 테이블 생성 완료 (row_size=%u, columns=%u)",
             stmt->table_name, hdr->row_size, hdr->column_count);
    return res;
}

/* ── INSERT ── */

/*
 * 행을 삽입한다.
 *
 * 과정:
 *   1. id를 next_id로 자동 할당
 *   2. 사용자 값을 컬럼 타입에 맞게 row_value_t에 저장
 *   3. row_serialize()로 바이트 버퍼 생성
 *   4. heap_insert()로 힙에 저장 → row_ref_t 획득
 *   5. bptree_insert()로 B+ tree에 (id → row_ref_t) 매핑 등록
 *   6. next_id 및 row_count 갱신
 */
static exec_result_t exec_insert(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};
    db_header_t *hdr = &pager->header;

    if (hdr->column_count == 0) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message), "오류: 생성된 테이블이 없습니다");
        return res;
    }

    row_value_t values[MAX_COLUMNS];
    memset(values, 0, sizeof(values));

    /* id는 자동 증가 값으로 설정 */
    values[0].bigint_val = (int64_t)hdr->next_id;

    /* 사용자 입력 값을 비시스템 컬럼에 매핑 */
    uint16_t val_idx = 0;
    for (uint16_t i = 1; i < hdr->column_count && val_idx < stmt->insert_value_count; i++) {
        const column_meta_t *col = &hdr->columns[i];
        const char *sv = stmt->insert_values[val_idx++];
        switch (col->type) {
            case COL_TYPE_INT:
                values[i].int_val = atoi(sv);
                break;
            case COL_TYPE_BIGINT:
                values[i].bigint_val = atoll(sv);
                break;
            case COL_TYPE_VARCHAR:
                strncpy(values[i].str_val, sv, 255);
                break;
        }
    }

    /* 행 직렬화 → 힙 삽입 → B+ tree 등록 */
    uint8_t *row_buf = (uint8_t *)calloc(1, hdr->row_size);
    row_serialize(hdr, values, row_buf);

    row_ref_t ref = heap_insert(pager, row_buf, hdr->row_size);
    int rc = bptree_insert(pager, hdr->next_id, ref);
    free(row_buf);

    if (rc != 0) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message), "오류: 중복된 키입니다");
        return res;
    }

    hdr->next_id++;
    hdr->row_count++;
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message),
             "1행 삽입 완료 (id=%" PRIu64 ")", hdr->next_id - 1);
    return res;
}

/* ── SELECT ── */

/* 테이블 스캔 콜백에서 사용하는 컨텍스트 */
typedef struct {
    pager_t *pager;
    const statement_t *stmt;
    uint32_t count;
} scan_ctx_t;

/*
 * SELECT의 테이블 스캔 콜백.
 * 각 행에 대해 조건(predicate)을 검사하고, 일치하면 출력한다.
 */
static bool select_scan_cb(const uint8_t *row_data, row_ref_t ref, void *ctx)
{
    (void)ref;
    scan_ctx_t *sc = (scan_ctx_t *)ctx;
    const db_header_t *hdr = &sc->pager->header;

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);

    /* WHERE 절이 있으면 조건 검사 */
    if (sc->stmt->predicate_kind == PREDICATE_FIELD_EQ) {
        for (uint16_t i = 0; i < hdr->column_count; i++) {
            if (strncmp(hdr->columns[i].name, sc->stmt->pred_field, 32) == 0) {
                bool match = false;
                switch (hdr->columns[i].type) {
                    case COL_TYPE_INT:
                        match = (values[i].int_val == atoi(sc->stmt->pred_value));
                        break;
                    case COL_TYPE_BIGINT:
                        match = (values[i].bigint_val == atoll(sc->stmt->pred_value));
                        break;
                    case COL_TYPE_VARCHAR:
                        match = (strcmp(values[i].str_val, sc->stmt->pred_value) == 0);
                        break;
                }
                if (match == false) {
                    return true; /* 조건 불일치, 다음 행으로 계속 */
                }
                break;
            }
        }
    }

    /* 첫 번째 결과 행 출력 전에 헤더를 출력 */
    if (sc->count == 0) {
        print_header(hdr);
    }
    print_row(hdr, values);
    sc->count++;
    return true;
}

/*
 * B+ tree 인덱스를 사용한 단건 조회.
 * WHERE id = N 조건에서 B+ tree로 row_ref_t를 찾고, 힙에서 행을 읽는다.
 */
static exec_result_t exec_index_lookup(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};
    db_header_t *hdr = &pager->header;

    row_ref_t ref;
    if (bptree_search(pager, stmt->pred_id, &ref) == false) {
        snprintf(res.message, sizeof(res.message), "오류: id=%" PRIu64 "인 행을 찾을 수 없습니다", stmt->pred_id);
        return res;
    }

    const uint8_t *row_data = heap_fetch(pager, ref, hdr->row_size);
    if (row_data == NULL) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message), "오류: 행 데이터를 읽지 못했습니다");
        return res;
    }

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);
    pager_unpin(pager, ref.page_id);

    print_header(hdr);
    print_row(hdr, values);
    snprintf(res.message, sizeof(res.message), "1행 조회 (INDEX_LOOKUP)");
    return res;
}

/* 힙 전체 스캔으로 조건에 맞는 행을 조회한다 */
static exec_result_t exec_table_scan(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};
    scan_ctx_t sc = { .pager = pager, .stmt = stmt, .count = 0 };
    heap_scan(pager, pager->header.row_size, select_scan_cb, &sc);
    snprintf(res.message, sizeof(res.message), "%u행 조회 (TABLE_SCAN)", sc.count);
    return res;
}

/* ── DELETE ── */

/* DELETE 테이블 스캔 콜백에서 사용하는 컨텍스트 */
typedef struct {
    pager_t *pager;
    const statement_t *stmt;
    uint32_t count;
    uint64_t *ids_to_delete;  /* 삭제할 id 배열 (동적 할당) */
    uint32_t ids_cap;          /* 배열 용량 */
    uint32_t ids_len;          /* 현재 수집된 id 수 */
} delete_scan_ctx_t;

/*
 * DELETE의 테이블 스캔 콜백.
 * 조건에 일치하는 행의 id를 수집한다 (스캔 중에는 삭제하지 않음).
 * 스캔 완료 후 일괄 삭제하는 2-pass 방식이다.
 */
static bool delete_scan_cb(const uint8_t *row_data, row_ref_t ref, void *ctx)
{
    (void)ref;
    delete_scan_ctx_t *dc = (delete_scan_ctx_t *)ctx;
    const db_header_t *hdr = &dc->pager->header;

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);

    /* 조건에 일치하는 행의 id를 수집 */
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (strncmp(hdr->columns[i].name, dc->stmt->pred_field, 32) == 0) {
            bool match = false;
            switch (hdr->columns[i].type) {
                case COL_TYPE_INT:
                    match = (values[i].int_val == atoi(dc->stmt->pred_value));
                    break;
                case COL_TYPE_BIGINT:
                    match = (values[i].bigint_val == atoll(dc->stmt->pred_value));
                    break;
                case COL_TYPE_VARCHAR:
                    match = (strcmp(values[i].str_val, dc->stmt->pred_value) == 0);
                    break;
            }
            if (match) {
                /* 배열 용량 부족 시 2배로 확장 */
                if (dc->ids_len >= dc->ids_cap) {
                    dc->ids_cap = dc->ids_cap ? dc->ids_cap * 2 : 64;
                    dc->ids_to_delete = realloc(dc->ids_to_delete,
                                                dc->ids_cap * sizeof(uint64_t));
                }
                dc->ids_to_delete[dc->ids_len++] = values[0].bigint_val;
            }
            break;
        }
    }
    return true;
}

/*
 * B+ tree 인덱스를 사용한 단건 삭제.
 * WHERE id = N 조건에서 B+ tree로 위치를 찾고, 힙과 인덱스에서 모두 삭제한다.
 */
static exec_result_t exec_index_delete(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};

    row_ref_t ref;
    if (bptree_search(pager, stmt->pred_id, &ref) == false) {
        snprintf(res.message, sizeof(res.message), "오류: id=%" PRIu64 "인 행을 찾을 수 없습니다", stmt->pred_id);
        return res;
    }

    heap_delete(pager, ref);
    bptree_delete(pager, stmt->pred_id);
    pager->header.row_count--;
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message), "1행 삭제 완료 (id=%" PRIu64 ")", stmt->pred_id);
    return res;
}

/*
 * 테이블 스캔으로 조건에 맞는 행을 일괄 삭제한다.
 *
 * 2-pass 방식:
 *   1차: 힙 스캔으로 조건 일치하는 행의 id를 수집
 *   2차: 수집된 id를 순회하며 B+ tree 검색 → 힙 삭제 → 인덱스 삭제
 *
 * 스캔 중에 직접 삭제하면 이터레이터가 깨질 수 있으므로 분리한다.
 */
static exec_result_t exec_delete_scan(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};
    delete_scan_ctx_t dc = {
        .pager = pager, .stmt = stmt, .count = 0,
        .ids_to_delete = NULL, .ids_cap = 0, .ids_len = 0
    };

    /* 1차: 조건에 맞는 id 수집 */
    heap_scan(pager, pager->header.row_size, delete_scan_cb, &dc);

    /* 2차: 수집된 id에 대해 일괄 삭제 */
    for (uint32_t i = 0; i < dc.ids_len; i++) {
        row_ref_t ref;
        if (bptree_search(pager, dc.ids_to_delete[i], &ref)) {
            heap_delete(pager, ref);
            bptree_delete(pager, dc.ids_to_delete[i]);
            pager->header.row_count--;
            dc.count++;
        }
    }
    free(dc.ids_to_delete);
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message), "%u행 삭제 완료 (TABLE_SCAN)", dc.count);
    return res;
}

/* ── EXPLAIN ── */

/*
 * EXPLAIN 명령을 실행한다.
 * 실제 데이터 조작 없이 실행 계획(접근 경로)만 출력한다.
 */
static exec_result_t exec_explain(pager_t *pager, statement_t *stmt)
{
    (void)pager;
    exec_result_t res = {0, ""};
    plan_t plan = planner_create_plan(stmt);
    printf("Access Path: %s\n", access_path_name(plan.access_path));

    if (plan.access_path == ACCESS_PATH_INDEX_LOOKUP) {
        printf("  Index: B+ Tree (id)\n");
        printf("  Target: id = %" PRIu64 "\n", stmt->pred_id);
    } else if (plan.access_path == ACCESS_PATH_INDEX_DELETE) {
        printf("  Index: B+ Tree (id)\n");
        printf("  Delete: id = %" PRIu64 "\n", stmt->pred_id);
    } else if (plan.access_path == ACCESS_PATH_TABLE_SCAN) {
        if (stmt->predicate_kind == PREDICATE_FIELD_EQ) {
            printf("  Filter: %s = '%s'\n", stmt->pred_field, stmt->pred_value);
        }
        printf("  Scan: all heap pages\n");
    }
    snprintf(res.message, sizeof(res.message), "실행 계획 출력 완료");
    return res;
}

/* ── 메인 디스패치 ── */

/*
 * SQL 문을 실행한다.
 * 플래너에서 접근 경로를 결정하고, 해당 실행 함수로 분기한다.
 */
exec_result_t execute(pager_t *pager, statement_t *stmt)
{
    /* EXPLAIN은 별도 처리 */
    if (stmt->type == STMT_EXPLAIN) {
        return exec_explain(pager, stmt);
    }

    plan_t plan = planner_create_plan(stmt);

    switch (plan.access_path) {
        case ACCESS_PATH_CREATE_TABLE:
            return exec_create_table(pager, stmt);
        case ACCESS_PATH_INSERT:
            return exec_insert(pager, stmt);
        case ACCESS_PATH_INDEX_LOOKUP:
            return exec_index_lookup(pager, stmt);
        case ACCESS_PATH_TABLE_SCAN:
            if (stmt->type == STMT_DELETE) {
                return exec_delete_scan(pager, stmt);
            }
            return exec_table_scan(pager, stmt);
        case ACCESS_PATH_INDEX_DELETE:
            return exec_index_delete(pager, stmt);
    }

    exec_result_t res = { -1, "오류: 알 수 없는 접근 경로입니다" };
    return res;
}
