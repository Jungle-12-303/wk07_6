/*
 * executor.c — SQL 실행기
 *
 * 역할:
 *   플래너가 결정한 접근 경로(access path)에 따라 실제 데이터 조작을 수행한다.
 *
 * 전체 실행 흐름 예시 (INSERT INTO users VALUES ('Alice', 25)):
 *
 *   1. parse() → stmt = {type=INSERT, table_name="users", values=["Alice","25"]}
 *   2. execute() → planner_create_plan() → ACCESS_PATH_INSERT
 *   3. exec_insert() 호출:
 *      a. id = next_id = 1 (자동 할당)
 *      b. values[0].bigint_val = 1     (id)
 *         values[1].str_val = "Alice"  (name)
 *         values[2].int_val = 25       (age)
 *      c. row_serialize() → 44바이트 버퍼 생성
 *      d. heap_insert() → row_ref_t {page_id=1, slot_id=0} 획득
 *      e. bptree_insert(key=1, ref={1,0}) → B+ tree에 등록
 *      f. next_id=2, row_count=1 갱신
 *
 * 접근 경로별 실행 함수:
 *   CREATE_TABLE  → exec_create_table()  : 스키마 정의
 *   INSERT        → exec_insert()        : 힙 삽입 + B+ tree 삽입
 *   INDEX_LOOKUP  → exec_index_lookup()  : B+ tree O(log n) 조회
 *   TABLE_SCAN    → exec_table_scan()    : 힙 O(n) 전체 스캔
 *   INDEX_DELETE  → exec_index_delete()  : B+ tree로 찾아 삭제
 *   TABLE_SCAN(삭제) → exec_delete_scan(): 스캔 후 2-pass 일괄 삭제
 */

#include "sql/executor.h"
#include "storage/schema.h"
#include "storage/table.h"
#include "storage/bptree.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

/*
 * print_row - 행 데이터를 컬럼별로 포맷하여 출력한다.
 *
 * 예시: columns = [id BIGINT, name VARCHAR(32), age INT]
 *       values = [1, "Alice", 25]
 *       출력: "1 | Alice | 25"
 */
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

/*
 * print_header - 컬럼 이름과 구분선을 출력한다.
 *
 * 예시: columns = [id, name, age]
 *       출력:
 *         id | name | age
 *         ----------+-----------+----------
 */
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

/* ══════════════════════════════════════════════════════════════════════
 *  CREATE TABLE
 *
 *  예시: CREATE TABLE users (name VARCHAR(32), age INT)
 *
 *  결과 (DB 헤더에 저장):
 *    columns[0] = {name="id",   type=BIGINT,     size=8,  offset=0,  is_system=1}
 *    columns[1] = {name="name", type=VARCHAR(32), size=32, offset=8,  is_system=0}
 *    columns[2] = {name="age",  type=INT,         size=4,  offset=40, is_system=0}
 *    column_count = 3, row_size = 44
 *
 *  id는 시스템 컬럼으로 자동 추가된다 (사용자가 명시해도 건너뜀).
 *  현재 단일 테이블만 지원하며, 이미 테이블이 있으면 오류를 반환한다.
 * ══════════════════════════════════════════════════════════════════════ */
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

    /* id를 첫 번째 시스템 컬럼으로 추가 (BIGINT 8바이트) */
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

    /*
     * 컬럼 오프셋 계산
     * schema_compute_layout()이 각 컬럼의 offset을 누적 계산하고 row_size를 설정한다.
     * 예: [8, 32, 4] → offsets=[0, 8, 40], row_size=44
     */
    schema_compute_layout(hdr);
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message),
             "'%s' 테이블 생성 완료 (row_size=%u, columns=%u)",
             stmt->table_name, hdr->row_size, hdr->column_count);
    return res;
}

/* ══════════════════════════════════════════════════════════════════════
 *  INSERT
 *
 *  예시: INSERT INTO users VALUES ('Alice', 25)
 *
 *  실행 과정:
 *    1. id = next_id = 1 (자동 할당)
 *    2. values[0] = id(1), values[1] = "Alice", values[2] = 25
 *    3. row_serialize() → 44바이트 버퍼: [01 00...][Alice\0...][19 00 00 00]
 *    4. heap_insert(row_buf) → ref = {page_id=1, slot_id=0}
 *    5. bptree_insert(key=1, ref={1,0})
 *    6. next_id = 2, row_count = 1
 * ══════════════════════════════════════════════════════════════════════ */
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

    /*
     * 사용자 입력 값을 비시스템 컬럼에 매핑한다.
     * 컬럼 0(id)은 건너뛰고, 컬럼 1부터 사용자 값을 순서대로 넣는다.
     *
     * 예시: columns = [id, name, age], values_input = ["Alice", "25"]
     *   values[1].str_val = "Alice"  (VARCHAR → strncpy)
     *   values[2].int_val = 25       (INT → atoi)
     */
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

/* ══════════════════════════════════════════════════════════════════════
 *  SELECT — INDEX_LOOKUP (B+ tree O(log n) 단건 조회)
 *
 *  예시: SELECT * FROM users WHERE id = 3
 *
 *  실행 과정:
 *    1. bptree_search(key=3) → ref = {page_id=1, slot_id=2}
 *    2. heap_fetch(ref) → 44바이트 행 데이터 포인터
 *    3. row_deserialize() → values = [3, "Charlie", 30]
 *    4. print_header() + print_row()
 *
 *  시간 복잡도: O(log n) — 100만 건에서도 3~4번의 페이지 접근
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * match_predicate - WHERE 절 조건과 행 값을 비교한다.
 *
 * PREDICATE_FIELD_EQ일 때 pred_field 컬럼의 값이 pred_value와 일치하면 true.
 * PREDICATE_NONE이면 항상 true (무조건 일치).
 *
 * 예시: WHERE name = 'Alice'
 *   pred_field="name", pred_value="Alice"
 *   → columns[1].name == "name" → strcmp(values[1].str_val, "Alice") → match
 */
static bool match_predicate(const db_header_t *hdr, const row_value_t *values,
                            const statement_t *stmt)
{
    if (stmt->predicate_kind != PREDICATE_FIELD_EQ) {
        return true; /* WHERE 없음 → 모든 행 일치 */
    }
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (strncmp(hdr->columns[i].name, stmt->pred_field, 32) != 0) {
            continue;
        }
        switch (hdr->columns[i].type) {
            case COL_TYPE_INT:
                return values[i].int_val == atoi(stmt->pred_value);
            case COL_TYPE_BIGINT:
                return values[i].bigint_val == atoll(stmt->pred_value);
            case COL_TYPE_VARCHAR:
                return strcmp(values[i].str_val, stmt->pred_value) == 0;
        }
    }
    return false; /* 해당 컬럼이 없으면 불일치 */
}

/* SELECT의 테이블 스캔 콜백에서 사용하는 컨텍스트 */
typedef struct {
    pager_t *pager;
    const statement_t *stmt;
    uint32_t count;
} scan_ctx_t;

/*
 * select_scan_cb - SELECT의 테이블 스캔 콜백
 *
 * 각 행에 대해 match_predicate()로 조건 검사 후 일치하면 출력한다.
 */
static bool select_scan_cb(const uint8_t *row_data, row_ref_t ref, void *ctx)
{
    (void)ref;
    scan_ctx_t *sc = (scan_ctx_t *)ctx;
    const db_header_t *hdr = &sc->pager->header;

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);

    if (!match_predicate(hdr, values, sc->stmt)) {
        return true; /* 조건 불일치, 다음 행으로 계속 */
    }

    /* 첫 번째 결과 행 출력 전에 헤더를 출력 */
    if (sc->count == 0) {
        print_header(hdr);
    }
    print_row(hdr, values);
    sc->count++;
    return true;
}

static exec_result_t exec_index_lookup(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};
    db_header_t *hdr = &pager->header;

    /* B+ tree에서 id로 검색 → 힙 위치(row_ref_t) 획득 */
    row_ref_t ref;
    if (bptree_search(pager, stmt->pred_id, &ref) == false) {
        snprintf(res.message, sizeof(res.message), "오류: id=%" PRIu64 "인 행을 찾을 수 없습니다", stmt->pred_id);
        return res;
    }

    /* 힙에서 행 데이터 읽기 */
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

/* ══════════════════════════════════════════════════════════════════════
 *  SELECT — TABLE_SCAN (힙 O(n) 전체 스캔)
 *
 *  예시: SELECT * FROM users WHERE name = 'Alice'
 *
 *  실행 과정:
 *    1. heap_scan() 호출 → 모든 힙 페이지를 순회
 *    2. 각 행에 대해 select_scan_cb() 호출
 *    3. name 컬럼에서 'Alice'와 일치하는 행만 출력
 *
 *  시간 복잡도: O(n) — 전체 행을 읽어야 하므로 느림
 * ══════════════════════════════════════════════════════════════════════ */
static exec_result_t exec_table_scan(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};
    scan_ctx_t sc = { .pager = pager, .stmt = stmt, .count = 0 };
    heap_scan(pager, pager->header.row_size, select_scan_cb, &sc);
    snprintf(res.message, sizeof(res.message), "%u행 조회 (TABLE_SCAN)", sc.count);
    return res;
}

/* ══════════════════════════════════════════════════════════════════════
 *  DELETE — INDEX_DELETE (B+ tree O(log n) 단건 삭제)
 *
 *  예시: DELETE FROM users WHERE id = 3
 *
 *  실행 과정:
 *    1. bptree_search(key=3) → ref = {page_id=1, slot_id=2}
 *    2. heap_delete(ref) → slot_2.status = FREE (톰스톤)
 *    3. bptree_delete(key=3) → B+ tree에서 엔트리 제거
 *    4. row_count-- 갱신
 * ══════════════════════════════════════════════════════════════════════ */

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
 * delete_scan_cb - DELETE의 테이블 스캔 콜백
 *
 * match_predicate()로 조건 검사 후 일치하는 행의 id를 수집한다.
 * 스캔 중 직접 삭제하면 이터레이터가 깨질 수 있으므로 2-pass 방식이다.
 *
 * 1차 (이 콜백): id 수집
 * 2차 (exec_delete_scan): 수집된 id로 일괄 삭제
 */
static bool delete_scan_cb(const uint8_t *row_data, row_ref_t ref, void *ctx)
{
    (void)ref;
    delete_scan_ctx_t *dc = (delete_scan_ctx_t *)ctx;
    const db_header_t *hdr = &dc->pager->header;

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);

    if (!match_predicate(hdr, values, dc->stmt)) {
        return true; /* 조건 불일치, 다음 행으로 계속 */
    }

    /* 배열 용량 부족 시 2배로 확장 (초기 64개) */
    if (dc->ids_len >= dc->ids_cap) {
        dc->ids_cap = dc->ids_cap ? dc->ids_cap * 2 : 64;
        dc->ids_to_delete = realloc(dc->ids_to_delete,
                                    dc->ids_cap * sizeof(uint64_t));
    }
    /* id는 항상 columns[0] (BIGINT) */
    dc->ids_to_delete[dc->ids_len++] = values[0].bigint_val;
    return true;
}

static exec_result_t exec_index_delete(pager_t *pager, statement_t *stmt)
{
    exec_result_t res = {0, ""};

    row_ref_t ref;
    if (bptree_search(pager, stmt->pred_id, &ref) == false) {
        snprintf(res.message, sizeof(res.message), "오류: id=%" PRIu64 "인 행을 찾을 수 없습니다", stmt->pred_id);
        return res;
    }

    /* 힙 삭제 (톰스톤) + B+ tree 삭제 */
    heap_delete(pager, ref);
    bptree_delete(pager, stmt->pred_id);
    pager->header.row_count--;
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message), "1행 삭제 완료 (id=%" PRIu64 ")", stmt->pred_id);
    return res;
}

/* ══════════════════════════════════════════════════════════════════════
 *  DELETE — TABLE_SCAN (2-pass 일괄 삭제)
 *
 *  예시: DELETE FROM users WHERE name = 'Alice'
 *
 *  실행 과정 (2-pass):
 *    1차: heap_scan → 조건 일치하는 행의 id를 배열에 수집
 *         → ids_to_delete = [1, 5, 12]
 *    2차: 수집된 id를 순회하며:
 *         bptree_search(id) → ref 획득
 *         heap_delete(ref)  → 톰스톤 삭제
 *         bptree_delete(id) → 인덱스 삭제
 *
 *  스캔 중 직접 삭제하면 힙 체인 순회가 깨질 수 있으므로 분리한다.
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 *  EXPLAIN
 *
 *  예시: EXPLAIN SELECT * FROM users WHERE id = 3
 *
 *  출력:
 *    Access Path: INDEX_LOOKUP
 *      Index: B+ Tree (id)
 *      Target: id = 3
 *
 *  실제 데이터 조작은 수행하지 않고 실행 계획만 출력한다.
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 *  메인 디스패치
 *
 *  execute()는 플래너에서 접근 경로를 결정하고, 해당 실행 함수로 분기한다.
 *
 *  흐름:
 *    execute(stmt)
 *      → EXPLAIN이면 exec_explain()
 *      → 아니면 planner_create_plan(stmt)
 *      → switch(access_path):
 *          CREATE_TABLE → exec_create_table()
 *          INSERT       → exec_insert()
 *          INDEX_LOOKUP → exec_index_lookup()
 *          TABLE_SCAN   → exec_table_scan() 또는 exec_delete_scan()
 *          INDEX_DELETE → exec_index_delete()
 * ══════════════════════════════════════════════════════════════════════ */
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
