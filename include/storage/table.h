#ifndef TABLE_H
#define TABLE_H

#include "pager.h"
#include "page_format.h"
#include <stdint.h>
#include <stdbool.h>

/* Insert a row, returning its ref */
row_ref_t heap_insert(pager_t *pager, const uint8_t *row_data, uint16_t row_size);

/* Fetch row bytes by ref.  Returns pointer into cached page (valid until unpin). */
const uint8_t *heap_fetch(pager_t *pager, row_ref_t ref, uint16_t row_size);

/* Mark slot as free */
int heap_delete(pager_t *pager, row_ref_t ref);

/* Scan callback: return false to stop */
typedef bool (*scan_cb)(const uint8_t *row_data, row_ref_t ref, void *ctx);
void heap_scan(pager_t *pager, uint16_t row_size, scan_cb cb, void *ctx);

#endif /* TABLE_H */
