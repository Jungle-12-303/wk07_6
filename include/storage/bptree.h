#ifndef BPTREE_H
#define BPTREE_H

#include "pager.h"
#include "page_format.h"
#include <stdbool.h>

/* Search: returns true if found, fills ref */
bool bptree_search(pager_t *pager, uint64_t key, row_ref_t *out_ref);

/* Insert key→ref.  Returns 0 on success, -1 on duplicate */
int bptree_insert(pager_t *pager, uint64_t key, row_ref_t ref);

/* Delete key.  Returns 0 on success, -1 if not found */
int bptree_delete(pager_t *pager, uint64_t key);

/* Debug: print tree structure */
void bptree_print(pager_t *pager);

/* Get tree height */
int bptree_height(pager_t *pager);

#endif /* BPTREE_H */
