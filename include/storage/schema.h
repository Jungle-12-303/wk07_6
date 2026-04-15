#ifndef SCHEMA_H
#define SCHEMA_H

#include "page_format.h"
#include <stdint.h>

typedef union {
    int32_t  int_val;
    int64_t  bigint_val;
    char     str_val[256];
} row_value_t;

/* Build row_layout from column defs already stored in db_header */
uint16_t schema_compute_layout(db_header_t *hdr);

/* serialize/deserialize a single row */
void row_serialize(const db_header_t *hdr, const row_value_t *values, uint8_t *buf);
void row_deserialize(const db_header_t *hdr, const uint8_t *buf, row_value_t *values);

#endif /* SCHEMA_H */
