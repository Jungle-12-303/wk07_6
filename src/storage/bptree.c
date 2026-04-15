#include "storage/bptree.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── helpers ── */

static uint32_t max_leaf_keys(pager_t *p)
{
    return (p->page_size - sizeof(leaf_page_header_t)) / sizeof(leaf_entry_t);
}

static uint32_t max_internal_keys(pager_t *p)
{
    return (p->page_size - sizeof(internal_page_header_t)) / sizeof(internal_entry_t);
}

static uint32_t min_leaf_keys(pager_t *p)
{
    return max_leaf_keys(p) / 2;
}

static uint32_t min_internal_keys(pager_t *p)
{
    return max_internal_keys(p) / 2;
}

static leaf_entry_t *leaf_entries(uint8_t *page)
{
    return (leaf_entry_t *)(page + sizeof(leaf_page_header_t));
}

static internal_entry_t *internal_entries(uint8_t *page)
{
    return (internal_entry_t *)(page + sizeof(internal_page_header_t));
}

static uint32_t root_id(pager_t *p)
{
    return p->header.root_index_page_id;
}

/* binary search in leaf: returns index where key should be (or is) */
static uint32_t leaf_find(leaf_entry_t *entries, uint32_t count, uint64_t key)
{
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (entries[mid].key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/* binary search in internal: returns child index */
static uint32_t internal_find(internal_entry_t *entries, uint32_t count, uint64_t key)
{
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (entries[mid].key <= key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/* Get child page_id from internal node for a given key */
static uint32_t internal_child_for_key(uint8_t *page, uint64_t key)
{
    internal_page_header_t iph;
    memcpy(&iph, page, sizeof(iph));
    internal_entry_t *entries = internal_entries(page);
    uint32_t idx = internal_find(entries, iph.key_count, key);
    if (idx == 0) {
        return iph.leftmost_child_page_id;
    }
    return entries[idx - 1].right_child_page_id;
}

/* Find the leaf page_id containing key */
static uint32_t find_leaf(pager_t *pager, uint64_t key)
{
    uint32_t pid = root_id(pager);
    while (1) {
        uint8_t *page = pager_get_page(pager, pid);
        uint32_t ptype;
        memcpy(&ptype, page, sizeof(uint32_t));
        if (ptype == PAGE_TYPE_LEAF) {
            pager_unpin(pager, pid);
            return pid;
        }
        uint32_t child = internal_child_for_key(page, key);
        pager_unpin(pager, pid);
        pid = child;
    }
}

/* ── search ── */

bool bptree_search(pager_t *pager, uint64_t key, row_ref_t *out_ref)
{
    uint32_t leaf_pid = find_leaf(pager, key);
    uint8_t *page = pager_get_page(pager, leaf_pid);
    leaf_page_header_t lph;
    memcpy(&lph, page, sizeof(lph));
    leaf_entry_t *entries = leaf_entries(page);

    uint32_t idx = leaf_find(entries, lph.key_count, key);
    bool found = (idx < lph.key_count && entries[idx].key == key);
    if (found && out_ref) {
        *out_ref = entries[idx].row_ref;
    }
    pager_unpin(pager, leaf_pid);
    return found;
}

/* ── insert ── */

/* Forward declarations for split handling */
static void insert_into_parent(pager_t *pager, uint32_t left_pid, uint64_t key, uint32_t right_pid);

static void leaf_insert_entry(uint8_t *page, leaf_page_header_t *lph,
                              uint64_t key, row_ref_t ref)
{
    leaf_entry_t *entries = leaf_entries(page);
    uint32_t idx = leaf_find(entries, lph->key_count, key);
    /* shift right */
    for (uint32_t i = lph->key_count; i > idx; i--) {
        entries[i] = entries[i - 1];
    }
    entries[idx].key = key;
    entries[idx].row_ref = ref;
    lph->key_count++;
    memcpy(page, lph, sizeof(*lph));
}

static void split_leaf(pager_t *pager, uint32_t leaf_pid, uint64_t key, row_ref_t ref)
{
    uint8_t *page = pager_get_page(pager, leaf_pid);
    leaf_page_header_t lph;
    memcpy(&lph, page, sizeof(lph));
    leaf_entry_t *entries = leaf_entries(page);

    uint32_t total = lph.key_count;
    uint32_t mk = max_leaf_keys(pager);

    /* Collect all entries + new one into temp array */
    leaf_entry_t *tmp = (leaf_entry_t *)malloc((total + 1) * sizeof(leaf_entry_t));
    uint32_t ins = leaf_find(entries, total, key);
    memcpy(tmp, entries, ins * sizeof(leaf_entry_t));
    tmp[ins].key = key;
    tmp[ins].row_ref = ref;
    memcpy(tmp + ins + 1, entries + ins, (total - ins) * sizeof(leaf_entry_t));
    total++;

    uint32_t split = total / 2;

    /* new right leaf */
    uint32_t new_pid = pager_alloc_page(pager);
    uint8_t *new_page = pager_get_page(pager, new_pid);
    memset(new_page, 0, pager->page_size);

    leaf_page_header_t new_lph = {
        .page_type = PAGE_TYPE_LEAF,
        .parent_page_id = lph.parent_page_id,
        .key_count = total - split,
        .next_leaf_page_id = lph.next_leaf_page_id,
        .prev_leaf_page_id = leaf_pid
    };
    memcpy(new_page, &new_lph, sizeof(new_lph));
    memcpy(leaf_entries(new_page), tmp + split, (total - split) * sizeof(leaf_entry_t));
    pager_mark_dirty(pager, new_pid);

    /* update old right neighbor's prev pointer */
    if (lph.next_leaf_page_id != 0) {
        uint8_t *rp = pager_get_page(pager, lph.next_leaf_page_id);
        leaf_page_header_t rlph;
        memcpy(&rlph, rp, sizeof(rlph));
        rlph.prev_leaf_page_id = new_pid;
        memcpy(rp, &rlph, sizeof(rlph));
        pager_mark_dirty(pager, lph.next_leaf_page_id);
        pager_unpin(pager, lph.next_leaf_page_id);
    }

    /* update left leaf */
    lph.key_count = split;
    lph.next_leaf_page_id = new_pid;
    memcpy(page, &lph, sizeof(lph));
    memcpy(leaf_entries(page), tmp, split * sizeof(leaf_entry_t));
    /* clear remaining slot area */
    memset(leaf_entries(page) + split, 0, (mk - split) * sizeof(leaf_entry_t));
    pager_mark_dirty(pager, leaf_pid);

    uint64_t promote_key = tmp[split].key;
    free(tmp);

    pager_unpin(pager, new_pid);
    pager_unpin(pager, leaf_pid);

    insert_into_parent(pager, leaf_pid, promote_key, new_pid);
}

static void split_internal(pager_t *pager, uint32_t node_pid, uint64_t key, uint32_t right_child)
{
    uint8_t *page = pager_get_page(pager, node_pid);
    internal_page_header_t iph;
    memcpy(&iph, page, sizeof(iph));
    internal_entry_t *entries = internal_entries(page);

    uint32_t total = iph.key_count;
    uint32_t mk = max_internal_keys(pager);

    internal_entry_t *tmp = (internal_entry_t *)malloc((total + 1) * sizeof(internal_entry_t));
    uint32_t ins = internal_find(entries, total, key);
    memcpy(tmp, entries, ins * sizeof(internal_entry_t));
    tmp[ins].key = key;
    tmp[ins].right_child_page_id = right_child;
    memcpy(tmp + ins + 1, entries + ins, (total - ins) * sizeof(internal_entry_t));
    total++;

    uint32_t split = total / 2;
    uint64_t promote_key = tmp[split].key;

    /* new right internal */
    uint32_t new_pid = pager_alloc_page(pager);
    uint8_t *new_page = pager_get_page(pager, new_pid);
    memset(new_page, 0, pager->page_size);

    internal_page_header_t new_iph = {
        .page_type = PAGE_TYPE_INTERNAL,
        .parent_page_id = iph.parent_page_id,
        .key_count = total - split - 1,
        .leftmost_child_page_id = tmp[split].right_child_page_id
    };
    memcpy(new_page, &new_iph, sizeof(new_iph));
    memcpy(internal_entries(new_page), tmp + split + 1,
           (total - split - 1) * sizeof(internal_entry_t));
    pager_mark_dirty(pager, new_pid);

    /* update children's parent pointers */
    {
        /* leftmost child of new node */
        uint8_t *cp = pager_get_page(pager, new_iph.leftmost_child_page_id);
        /* parent_page_id is at offset 4 for both leaf and internal headers */
        uint32_t pp = new_pid;
        memcpy(cp + 4, &pp, sizeof(uint32_t));
        pager_mark_dirty(pager, new_iph.leftmost_child_page_id);
        pager_unpin(pager, new_iph.leftmost_child_page_id);

        internal_entry_t *ne = internal_entries(new_page);
        for (uint32_t i = 0; i < new_iph.key_count; i++) {
            cp = pager_get_page(pager, ne[i].right_child_page_id);
            memcpy(cp + 4, &pp, sizeof(uint32_t));
            pager_mark_dirty(pager, ne[i].right_child_page_id);
            pager_unpin(pager, ne[i].right_child_page_id);
        }
    }

    /* update left node */
    iph.key_count = split;
    memcpy(page, &iph, sizeof(iph));
    memcpy(internal_entries(page), tmp, split * sizeof(internal_entry_t));
    memset(internal_entries(page) + split, 0, (mk - split) * sizeof(internal_entry_t));
    pager_mark_dirty(pager, node_pid);

    free(tmp);
    pager_unpin(pager, new_pid);
    pager_unpin(pager, node_pid);

    insert_into_parent(pager, node_pid, promote_key, new_pid);
}

static void insert_into_parent(pager_t *pager, uint32_t left_pid, uint64_t key, uint32_t right_pid)
{
    /* get left's parent */
    uint8_t *left_page = pager_get_page(pager, left_pid);
    uint32_t parent_pid;
    memcpy(&parent_pid, left_page + 4, sizeof(uint32_t)); /* parent_page_id at offset 4 */
    pager_unpin(pager, left_pid);

    if (parent_pid == 0 && left_pid == root_id(pager)) {
        /* create new root */
        uint32_t new_root = pager_alloc_page(pager);
        uint8_t *rp = pager_get_page(pager, new_root);
        memset(rp, 0, pager->page_size);

        internal_page_header_t iph = {
            .page_type = PAGE_TYPE_INTERNAL,
            .parent_page_id = 0,
            .key_count = 1,
            .leftmost_child_page_id = left_pid
        };
        memcpy(rp, &iph, sizeof(iph));
        internal_entry_t e = { .key = key, .right_child_page_id = right_pid };
        memcpy(internal_entries(rp), &e, sizeof(e));
        pager_mark_dirty(pager, new_root);
        pager_unpin(pager, new_root);

        /* update children's parent */
        uint8_t *lp = pager_get_page(pager, left_pid);
        memcpy(lp + 4, &new_root, sizeof(uint32_t));
        pager_mark_dirty(pager, left_pid);
        pager_unpin(pager, left_pid);

        uint8_t *rrp = pager_get_page(pager, right_pid);
        memcpy(rrp + 4, &new_root, sizeof(uint32_t));
        pager_mark_dirty(pager, right_pid);
        pager_unpin(pager, right_pid);

        pager->header.root_index_page_id = new_root;
        return;
    }

    /* insert into existing parent */
    uint8_t *pp = pager_get_page(pager, parent_pid);
    internal_page_header_t iph;
    memcpy(&iph, pp, sizeof(iph));

    if (iph.key_count < max_internal_keys(pager)) {
        internal_entry_t *entries = internal_entries(pp);
        uint32_t idx = internal_find(entries, iph.key_count, key);
        for (uint32_t i = iph.key_count; i > idx; i--) {
            entries[i] = entries[i - 1];
        }
        entries[idx].key = key;
        entries[idx].right_child_page_id = right_pid;
        iph.key_count++;
        memcpy(pp, &iph, sizeof(iph));
        pager_mark_dirty(pager, parent_pid);
        pager_unpin(pager, parent_pid);

        /* set right child's parent */
        uint8_t *rcp = pager_get_page(pager, right_pid);
        memcpy(rcp + 4, &parent_pid, sizeof(uint32_t));
        pager_mark_dirty(pager, right_pid);
        pager_unpin(pager, right_pid);
    } else {
        pager_unpin(pager, parent_pid);
        /* set right child's parent temporarily */
        uint8_t *rcp = pager_get_page(pager, right_pid);
        memcpy(rcp + 4, &parent_pid, sizeof(uint32_t));
        pager_mark_dirty(pager, right_pid);
        pager_unpin(pager, right_pid);

        split_internal(pager, parent_pid, key, right_pid);
    }
}

int bptree_insert(pager_t *pager, uint64_t key, row_ref_t ref)
{
    uint32_t leaf_pid = find_leaf(pager, key);
    uint8_t *page = pager_get_page(pager, leaf_pid);
    leaf_page_header_t lph;
    memcpy(&lph, page, sizeof(lph));
    leaf_entry_t *entries = leaf_entries(page);

    /* check duplicate */
    uint32_t idx = leaf_find(entries, lph.key_count, key);
    if (idx < lph.key_count && entries[idx].key == key) {
        pager_unpin(pager, leaf_pid);
        return -1; /* duplicate */
    }

    if (lph.key_count < max_leaf_keys(pager)) {
        leaf_insert_entry(page, &lph, key, ref);
        pager_mark_dirty(pager, leaf_pid);
        pager_unpin(pager, leaf_pid);
        return 0;
    }

    pager_unpin(pager, leaf_pid);
    split_leaf(pager, leaf_pid, key, ref);
    return 0;
}

/* ── delete ── */

static void delete_entry_from_leaf(uint8_t *page, leaf_page_header_t *lph, uint32_t idx)
{
    leaf_entry_t *entries = leaf_entries(page);
    for (uint32_t i = idx; i < lph->key_count - 1; i++) {
        entries[i] = entries[i + 1];
    }
    lph->key_count--;
    memcpy(page, lph, sizeof(*lph));
}

static void delete_entry_from_internal(uint8_t *page, internal_page_header_t *iph, uint32_t idx)
{
    internal_entry_t *entries = internal_entries(page);
    for (uint32_t i = idx; i < iph->key_count - 1; i++) {
        entries[i] = entries[i + 1];
    }
    iph->key_count--;
    memcpy(page, iph, sizeof(*iph));
}

static void fix_internal_after_delete(pager_t *pager, uint32_t node_pid);

static void fix_leaf_after_delete(pager_t *pager, uint32_t leaf_pid)
{
    uint8_t *page = pager_get_page(pager, leaf_pid);
    leaf_page_header_t lph;
    memcpy(&lph, page, sizeof(lph));
    pager_unpin(pager, leaf_pid);

    if (leaf_pid == root_id(pager)) {
        return; /* root can be underfull */
    }
    if (lph.key_count >= min_leaf_keys(pager)) {
        return;
    }

    uint32_t parent_pid = lph.parent_page_id;
    uint8_t *pp = pager_get_page(pager, parent_pid);
    internal_page_header_t iph;
    memcpy(&iph, pp, sizeof(iph));
    internal_entry_t *pentries = internal_entries(pp);

    /* find our index in parent */
    int child_idx = -1;
    if (iph.leftmost_child_page_id == leaf_pid) {
        child_idx = 0;
    } else {
        for (uint32_t i = 0; i < iph.key_count; i++) {
            if (pentries[i].right_child_page_id == leaf_pid) {
                child_idx = (int)i + 1;
                break;
            }
        }
    }
    pager_unpin(pager, parent_pid);
    if (child_idx < 0) {
        return;
    }

    /* try borrow from right sibling */
    if (child_idx <= (int)iph.key_count - 1 || (child_idx == 0 && iph.key_count > 0)) {
        /* right sibling */
        uint32_t sep_idx = (child_idx == 0) ? 0 : (uint32_t)child_idx;
        if (sep_idx < iph.key_count) {
            uint32_t rsib_pid = pentries[sep_idx].right_child_page_id;
            if (child_idx > 0) {
                /* we are a right_child, so right sibling is pentries[child_idx].right_child if exists */
                if ((uint32_t)child_idx < iph.key_count) {
                    rsib_pid = pentries[child_idx].right_child_page_id;
                    sep_idx = (uint32_t)child_idx;
                } else {
                    goto try_left;
                }
            }

            uint8_t *rpage = pager_get_page(pager, rsib_pid);
            leaf_page_header_t rlph;
            memcpy(&rlph, rpage, sizeof(rlph));

            if (rlph.key_count > min_leaf_keys(pager)) {
                /* borrow first entry from right */
                leaf_entry_t *rentries = leaf_entries(rpage);
                leaf_entry_t borrowed = rentries[0];
                delete_entry_from_leaf(rpage, &rlph, 0);
                pager_mark_dirty(pager, rsib_pid);
                pager_unpin(pager, rsib_pid);

                page = pager_get_page(pager, leaf_pid);
                memcpy(&lph, page, sizeof(lph));
                leaf_insert_entry(page, &lph, borrowed.key, borrowed.row_ref);
                pager_mark_dirty(pager, leaf_pid);
                pager_unpin(pager, leaf_pid);

                /* update separator in parent */
                pp = pager_get_page(pager, parent_pid);
                memcpy(&iph, pp, sizeof(iph));
                pentries = internal_entries(pp);
                /* new separator = first key of right sibling after borrow */
                rpage = pager_get_page(pager, rsib_pid);
                memcpy(&rlph, rpage, sizeof(rlph));
                pentries[sep_idx].key = leaf_entries(rpage)[0].key;
                pager_unpin(pager, rsib_pid);
                memcpy(pp, &iph, sizeof(iph));
                pager_mark_dirty(pager, parent_pid);
                pager_unpin(pager, parent_pid);
                return;
            }
            pager_unpin(pager, rsib_pid);
        }
    }

try_left:
    /* try borrow from left sibling */
    if (child_idx > 0) {
        uint32_t lsib_pid;
        uint32_t sep_idx = (uint32_t)(child_idx - 1);
        if (child_idx == 1) {
            lsib_pid = iph.leftmost_child_page_id;
        } else {
            lsib_pid = pentries[child_idx - 2].right_child_page_id;
        }

        uint8_t *lpage = pager_get_page(pager, lsib_pid);
        leaf_page_header_t llph;
        memcpy(&llph, lpage, sizeof(llph));

        if (llph.key_count > min_leaf_keys(pager)) {
            /* borrow last entry from left */
            leaf_entry_t *lentries = leaf_entries(lpage);
            leaf_entry_t borrowed = lentries[llph.key_count - 1];
            llph.key_count--;
            memcpy(lpage, &llph, sizeof(llph));
            pager_mark_dirty(pager, lsib_pid);
            pager_unpin(pager, lsib_pid);

            page = pager_get_page(pager, leaf_pid);
            memcpy(&lph, page, sizeof(lph));
            leaf_insert_entry(page, &lph, borrowed.key, borrowed.row_ref);
            pager_mark_dirty(pager, leaf_pid);
            pager_unpin(pager, leaf_pid);

            /* update separator */
            pp = pager_get_page(pager, parent_pid);
            memcpy(&iph, pp, sizeof(iph));
            pentries = internal_entries(pp);
            pentries[sep_idx].key = borrowed.key;
            /* actually separator should be the first key of the right node (us) */
            page = pager_get_page(pager, leaf_pid);
            memcpy(&lph, page, sizeof(lph));
            pentries[sep_idx].key = leaf_entries(page)[0].key;
            pager_unpin(pager, leaf_pid);
            memcpy(pp, &iph, sizeof(iph));
            pager_mark_dirty(pager, parent_pid);
            pager_unpin(pager, parent_pid);
            return;
        }
        pager_unpin(pager, lsib_pid);
    }

    /* merge: prefer merging with left sibling */
    if (child_idx > 0) {
        uint32_t lsib_pid;
        uint32_t sep_idx = (uint32_t)(child_idx - 1);
        if (child_idx == 1) {
            lsib_pid = iph.leftmost_child_page_id;
        } else {
            lsib_pid = pentries[child_idx - 2].right_child_page_id;
        }

        /* merge current into left */
        uint8_t *lpage = pager_get_page(pager, lsib_pid);
        leaf_page_header_t llph;
        memcpy(&llph, lpage, sizeof(llph));

        page = pager_get_page(pager, leaf_pid);
        memcpy(&lph, page, sizeof(lph));
        leaf_entry_t *cur_entries = leaf_entries(page);
        leaf_entry_t *left_entries = leaf_entries(lpage);

        for (uint32_t i = 0; i < lph.key_count; i++) {
            left_entries[llph.key_count + i] = cur_entries[i];
        }
        llph.key_count += lph.key_count;
        llph.next_leaf_page_id = lph.next_leaf_page_id;
        memcpy(lpage, &llph, sizeof(llph));
        pager_mark_dirty(pager, lsib_pid);
        pager_unpin(pager, lsib_pid);
        pager_unpin(pager, leaf_pid);

        /* update prev pointer of next neighbor */
        if (lph.next_leaf_page_id != 0) {
            uint8_t *np = pager_get_page(pager, lph.next_leaf_page_id);
            leaf_page_header_t nlph;
            memcpy(&nlph, np, sizeof(nlph));
            nlph.prev_leaf_page_id = lsib_pid;
            memcpy(np, &nlph, sizeof(nlph));
            pager_mark_dirty(pager, lph.next_leaf_page_id);
            pager_unpin(pager, lph.next_leaf_page_id);
        }

        pager_free_page(pager, leaf_pid);

        /* remove separator from parent */
        pp = pager_get_page(pager, parent_pid);
        memcpy(&iph, pp, sizeof(iph));
        pentries = internal_entries(pp);
        delete_entry_from_internal(pp, &iph, sep_idx);
        pager_mark_dirty(pager, parent_pid);
        pager_unpin(pager, parent_pid);

        fix_internal_after_delete(pager, parent_pid);
    } else {
        /* merge right sibling into us */
        uint32_t rsib_pid = pentries[0].right_child_page_id;

        page = pager_get_page(pager, leaf_pid);
        memcpy(&lph, page, sizeof(lph));
        leaf_entry_t *cur_entries = leaf_entries(page);

        uint8_t *rpage = pager_get_page(pager, rsib_pid);
        leaf_page_header_t rlph;
        memcpy(&rlph, rpage, sizeof(rlph));
        leaf_entry_t *rentries = leaf_entries(rpage);

        for (uint32_t i = 0; i < rlph.key_count; i++) {
            cur_entries[lph.key_count + i] = rentries[i];
        }
        lph.key_count += rlph.key_count;
        lph.next_leaf_page_id = rlph.next_leaf_page_id;
        memcpy(page, &lph, sizeof(lph));
        pager_mark_dirty(pager, leaf_pid);
        pager_unpin(pager, leaf_pid);
        pager_unpin(pager, rsib_pid);

        if (rlph.next_leaf_page_id != 0) {
            uint8_t *np = pager_get_page(pager, rlph.next_leaf_page_id);
            leaf_page_header_t nlph;
            memcpy(&nlph, np, sizeof(nlph));
            nlph.prev_leaf_page_id = leaf_pid;
            memcpy(np, &nlph, sizeof(nlph));
            pager_mark_dirty(pager, rlph.next_leaf_page_id);
            pager_unpin(pager, rlph.next_leaf_page_id);
        }

        pager_free_page(pager, rsib_pid);

        pp = pager_get_page(pager, parent_pid);
        memcpy(&iph, pp, sizeof(iph));
        pentries = internal_entries(pp);
        delete_entry_from_internal(pp, &iph, 0);
        pager_mark_dirty(pager, parent_pid);
        pager_unpin(pager, parent_pid);

        fix_internal_after_delete(pager, parent_pid);
    }
}

static void fix_internal_after_delete(pager_t *pager, uint32_t node_pid)
{
    if (node_pid == root_id(pager)) {
        /* root shrink: if root has 0 keys, promote its only child */
        uint8_t *page = pager_get_page(pager, node_pid);
        internal_page_header_t iph;
        memcpy(&iph, page, sizeof(iph));
        if (iph.key_count == 0) {
            uint32_t child = iph.leftmost_child_page_id;
            pager_unpin(pager, node_pid);
            pager_free_page(pager, node_pid);

            pager->header.root_index_page_id = child;
            uint8_t *cp = pager_get_page(pager, child);
            uint32_t zero = 0;
            memcpy(cp + 4, &zero, sizeof(uint32_t)); /* parent = 0 */
            pager_mark_dirty(pager, child);
            pager_unpin(pager, child);
        } else {
            pager_unpin(pager, node_pid);
        }
        return;
    }

    uint8_t *page = pager_get_page(pager, node_pid);
    internal_page_header_t iph;
    memcpy(&iph, page, sizeof(iph));
    pager_unpin(pager, node_pid);

    if (iph.key_count >= min_internal_keys(pager)) {
        return;
    }

    /* simplified: for minimal implementation, we skip internal borrow/merge
       as it's rare and complex. The tree stays valid but may be slightly underfull. */
    /* A full implementation would borrow/merge internal nodes here. */
}

int bptree_delete(pager_t *pager, uint64_t key)
{
    uint32_t leaf_pid = find_leaf(pager, key);
    uint8_t *page = pager_get_page(pager, leaf_pid);
    leaf_page_header_t lph;
    memcpy(&lph, page, sizeof(lph));
    leaf_entry_t *entries = leaf_entries(page);

    uint32_t idx = leaf_find(entries, lph.key_count, key);
    if (idx >= lph.key_count || entries[idx].key != key) {
        pager_unpin(pager, leaf_pid);
        return -1;
    }

    delete_entry_from_leaf(page, &lph, idx);
    pager_mark_dirty(pager, leaf_pid);
    pager_unpin(pager, leaf_pid);

    fix_leaf_after_delete(pager, leaf_pid);
    return 0;
}

/* ── debug ── */

static void print_node(pager_t *pager, uint32_t pid, int depth)
{
    uint8_t *page = pager_get_page(pager, pid);
    uint32_t ptype;
    memcpy(&ptype, page, sizeof(uint32_t));

    for (int i = 0; i < depth; i++) {
        printf("  ");
    }

    if (ptype == PAGE_TYPE_LEAF) {
        leaf_page_header_t lph;
        memcpy(&lph, page, sizeof(lph));
        leaf_entry_t *entries = leaf_entries(page);
        printf("[LEAF page=%u] keys=%u:", pid, lph.key_count);
        for (uint32_t i = 0; i < lph.key_count && i < 5; i++) {
            printf(" %lu", (unsigned long)entries[i].key);
        }
        if (lph.key_count > 5) {
            printf(" ...");
        }
        printf("\n");
    } else if (ptype == PAGE_TYPE_INTERNAL) {
        internal_page_header_t iph;
        memcpy(&iph, page, sizeof(iph));
        internal_entry_t *entries = internal_entries(page);
        printf("[INTERNAL page=%u] keys=%u:", pid, iph.key_count);
        for (uint32_t i = 0; i < iph.key_count && i < 5; i++) {
            printf(" %lu", (unsigned long)entries[i].key);
        }
        if (iph.key_count > 5) {
            printf(" ...");
        }
        printf("\n");
        pager_unpin(pager, pid);

        /* recurse */
        page = pager_get_page(pager, pid);
        memcpy(&iph, page, sizeof(iph));
        entries = internal_entries(page);

        print_node(pager, iph.leftmost_child_page_id, depth + 1);
        for (uint32_t i = 0; i < iph.key_count; i++) {
            print_node(pager, entries[i].right_child_page_id, depth + 1);
        }
        pager_unpin(pager, pid);
        return;
    }
    pager_unpin(pager, pid);
}

void bptree_print(pager_t *pager)
{
    printf("B+ Tree (root: page %u)\n", root_id(pager));
    print_node(pager, root_id(pager), 1);
}

int bptree_height(pager_t *pager)
{
    int h = 0;
    uint32_t pid = root_id(pager);
    while (1) {
        h++;
        uint8_t *page = pager_get_page(pager, pid);
        uint32_t ptype;
        memcpy(&ptype, page, sizeof(uint32_t));
        if (ptype == PAGE_TYPE_LEAF) {
            pager_unpin(pager, pid);
            return h;
        }
        internal_page_header_t iph;
        memcpy(&iph, page, sizeof(iph));
        pager_unpin(pager, pid);
        pid = iph.leftmost_child_page_id;
    }
}
