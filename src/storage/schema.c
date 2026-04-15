#include "storage/schema.h"
#include <string.h>

uint16_t schema_compute_layout(db_header_t *hdr) {
    uint16_t offset = 0;
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        hdr->columns[i].offset = offset;
        offset += hdr->columns[i].size;
    }
    hdr->row_size = offset;
    return offset;
}

void row_serialize(const db_header_t *hdr, const row_value_t *values, uint8_t *buf) {
    memset(buf, 0, hdr->row_size);
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        const column_meta_t *col = &hdr->columns[i];
        uint8_t *dst = buf + col->offset;
        switch (col->type) {
            case COL_TYPE_INT: {
                int32_t v = values[i].int_val;
                memcpy(dst, &v, 4);
                break;
            }
            case COL_TYPE_BIGINT: {
                int64_t v = values[i].bigint_val;
                memcpy(dst, &v, 8);
                break;
            }
            case COL_TYPE_VARCHAR: {
                size_t len = strlen(values[i].str_val);
                if (len >= col->size) len = col->size - 1;
                memcpy(dst, values[i].str_val, len);
                break;
            }
        }
    }
}

void row_deserialize(const db_header_t *hdr, const uint8_t *buf, row_value_t *values) {
    memset(values, 0, sizeof(row_value_t) * hdr->column_count);
    for (uint16_t i = 0; i < hdr->column_count; i++) {
        const column_meta_t *col = &hdr->columns[i];
        const uint8_t *src = buf + col->offset;
        switch (col->type) {
            case COL_TYPE_INT:
                memcpy(&values[i].int_val, src, 4);
                break;
            case COL_TYPE_BIGINT:
                memcpy(&values[i].bigint_val, src, 8);
                break;
            case COL_TYPE_VARCHAR:
                memcpy(values[i].str_val, src, col->size);
                values[i].str_val[col->size] = '\0';
                break;
        }
    }
}
