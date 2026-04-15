#include "sql/executor.h"
#include "storage/schema.h"
#include "storage/table.h"
#include "storage/bptree.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static void print_row(const db_header_t *hdr, const row_value_t *values) {
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (i > 0) printf(" | ");
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

static void print_header(const db_header_t *hdr) {
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (i > 0) printf(" | ");
        printf("%s", hdr->columns[i].name);
    }
    printf("\n");
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        if (i > 0) printf("-+-");
        for (uint16_t j = 0; j < 10; j++) printf("-");
    }
    printf("\n");
}

/* ── CREATE TABLE ── */

static exec_result_t exec_create_table(pager_t *pager, statement_t *stmt) {
    exec_result_t res = {0, ""};
    db_header_t *hdr = &pager->header;

    if (hdr->column_count > 0) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message), "Error: table already exists");
        return res;
    }

    /* add id as first column */
    hdr->column_count = 0;
    column_meta_t *id_col = &hdr->columns[hdr->column_count++];
    memset(id_col, 0, sizeof(*id_col));
    strncpy(id_col->name, "id", 31);
    id_col->type = COL_TYPE_BIGINT;
    id_col->size = 8;
    id_col->is_system = 1;

    for (uint16_t i = 0; i < stmt->col_count; i++) {
        column_def_t *cd = &stmt->col_defs[i];
        /* skip if user explicitly defined id */
        if (strncmp(cd->name, "id", 32) == 0) continue;

        column_meta_t *col = &hdr->columns[hdr->column_count++];
        memset(col, 0, sizeof(*col));
        strncpy(col->name, cd->name, 31);
        col->type = cd->type;
        col->size = cd->size;
        col->is_system = 0;
    }

    schema_compute_layout(hdr);
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message),
             "Table '%s' created (row_size=%u, columns=%u)",
             stmt->table_name, hdr->row_size, hdr->column_count);
    return res;
}

/* ── INSERT ── */

static exec_result_t exec_insert(pager_t *pager, statement_t *stmt) {
    exec_result_t res = {0, ""};
    db_header_t *hdr = &pager->header;

    if (hdr->column_count == 0) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message), "Error: no table created");
        return res;
    }

    row_value_t values[MAX_COLUMNS];
    memset(values, 0, sizeof(values));

    /* id is auto-assigned */
    values[0].bigint_val = (int64_t)hdr->next_id;

    /* map user-provided values to non-system columns */
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

    uint8_t *row_buf = (uint8_t *)calloc(1, hdr->row_size);
    row_serialize(hdr, values, row_buf);

    row_ref_t ref = heap_insert(pager, row_buf, hdr->row_size);
    int rc = bptree_insert(pager, hdr->next_id, ref);
    free(row_buf);

    if (rc != 0) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message), "Error: duplicate key");
        return res;
    }

    hdr->next_id++;
    hdr->row_count++;
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message),
             "Inserted 1 row (id=%" PRIu64 ")", hdr->next_id - 1);
    return res;
}

/* ── SELECT ── */

typedef struct {
    pager_t *pager;
    const statement_t *stmt;
    uint32_t count;
} scan_ctx_t;

static bool select_scan_cb(const uint8_t *row_data, row_ref_t ref, void *ctx) {
    (void)ref;
    scan_ctx_t *sc = (scan_ctx_t *)ctx;
    const db_header_t *hdr = &sc->pager->header;

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);

    if (sc->stmt->predicate_kind == PREDICATE_FIELD_EQ) {
        /* find matching column */
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
                if (!match) return true; /* continue scan */
                break;
            }
        }
    }

    if (sc->count == 0) print_header(hdr);
    print_row(hdr, values);
    sc->count++;
    return true;
}

static exec_result_t exec_index_lookup(pager_t *pager, statement_t *stmt) {
    exec_result_t res = {0, ""};
    db_header_t *hdr = &pager->header;

    row_ref_t ref;
    if (!bptree_search(pager, stmt->pred_id, &ref)) {
        snprintf(res.message, sizeof(res.message), "No row found with id=%" PRIu64, stmt->pred_id);
        return res;
    }

    const uint8_t *row_data = heap_fetch(pager, ref, hdr->row_size);
    if (!row_data) {
        res.status = -1;
        snprintf(res.message, sizeof(res.message), "Error: row fetch failed");
        return res;
    }

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);
    pager_unpin(pager, ref.page_id);

    print_header(hdr);
    print_row(hdr, values);
    snprintf(res.message, sizeof(res.message), "1 row (INDEX_LOOKUP)");
    return res;
}

static exec_result_t exec_table_scan(pager_t *pager, statement_t *stmt) {
    exec_result_t res = {0, ""};
    scan_ctx_t sc = { .pager = pager, .stmt = stmt, .count = 0 };
    heap_scan(pager, pager->header.row_size, select_scan_cb, &sc);
    snprintf(res.message, sizeof(res.message), "%u rows (TABLE_SCAN)", sc.count);
    return res;
}

/* ── DELETE ── */

typedef struct {
    pager_t *pager;
    const statement_t *stmt;
    uint32_t count;
    uint64_t *ids_to_delete;
    uint32_t ids_cap;
    uint32_t ids_len;
} delete_scan_ctx_t;

static bool delete_scan_cb(const uint8_t *row_data, row_ref_t ref, void *ctx) {
    (void)ref;
    delete_scan_ctx_t *dc = (delete_scan_ctx_t *)ctx;
    const db_header_t *hdr = &dc->pager->header;

    row_value_t values[MAX_COLUMNS];
    row_deserialize(hdr, row_data, values);

    /* check predicate */
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
                /* collect id for deletion */
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

static exec_result_t exec_index_delete(pager_t *pager, statement_t *stmt) {
    exec_result_t res = {0, ""};

    row_ref_t ref;
    if (!bptree_search(pager, stmt->pred_id, &ref)) {
        snprintf(res.message, sizeof(res.message), "No row found with id=%" PRIu64, stmt->pred_id);
        return res;
    }

    heap_delete(pager, ref);
    bptree_delete(pager, stmt->pred_id);
    pager->header.row_count--;
    pager->header_dirty = true;

    snprintf(res.message, sizeof(res.message), "Deleted 1 row (id=%" PRIu64 ")", stmt->pred_id);
    return res;
}

static exec_result_t exec_delete_scan(pager_t *pager, statement_t *stmt) {
    exec_result_t res = {0, ""};
    delete_scan_ctx_t dc = {
        .pager = pager, .stmt = stmt, .count = 0,
        .ids_to_delete = NULL, .ids_cap = 0, .ids_len = 0
    };

    heap_scan(pager, pager->header.row_size, delete_scan_cb, &dc);

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

    snprintf(res.message, sizeof(res.message), "Deleted %u rows (TABLE_SCAN)", dc.count);
    return res;
}

/* ── EXPLAIN ── */

static exec_result_t exec_explain(pager_t *pager, statement_t *stmt) {
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
    snprintf(res.message, sizeof(res.message), "EXPLAIN done");
    return res;
}

/* ── main dispatch ── */

exec_result_t execute(pager_t *pager, statement_t *stmt) {
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
            if (stmt->type == STMT_DELETE)
                return exec_delete_scan(pager, stmt);
            return exec_table_scan(pager, stmt);
        case ACCESS_PATH_INDEX_DELETE:
            return exec_index_delete(pager, stmt);
    }

    exec_result_t res = { -1, "Unknown access path" };
    return res;
}
