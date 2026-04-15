#include "sql/parser.h"
#include "sql/executor.h"
#include "sql/planner.h"
#include "storage/pager.h"
#include "storage/bptree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static void cmd_btree(pager_t *pager) {
    bptree_print(pager);
}

static void cmd_pages(pager_t *pager) {
    db_header_t *hdr = &pager->header;
    uint32_t heap_count = 0, leaf_count = 0, internal_count = 0, free_count = 0;
    for (uint32_t i = 1; i < hdr->next_page_id; i++) {
        uint8_t *page = pager_get_page(pager, i);
        uint32_t ptype;
        memcpy(&ptype, page, sizeof(uint32_t));
        pager_unpin(pager, i);
        switch (ptype) {
            case PAGE_TYPE_HEAP:     heap_count++; break;
            case PAGE_TYPE_LEAF:     leaf_count++; break;
            case PAGE_TYPE_INTERNAL: internal_count++; break;
            case PAGE_TYPE_FREE:     free_count++; break;
        }
    }
    printf("Total pages: %u\n", hdr->next_page_id);
    printf("  HEADER:   1\n");
    printf("  HEAP:     %u\n", heap_count);
    printf("  LEAF:     %u\n", leaf_count);
    printf("  INTERNAL: %u\n", internal_count);
    printf("  FREE:     %u\n", free_count);

    if (hdr->free_page_head != 0) {
        printf("Free page list:");
        uint32_t fp = hdr->free_page_head;
        int c = 0;
        while (fp != 0 && c < 20) {
            printf(" %u", fp);
            uint8_t *p = pager_get_page(pager, fp);
            free_page_header_t fph;
            memcpy(&fph, p, sizeof(fph));
            pager_unpin(pager, fp);
            fp = fph.next_free_page;
            c++;
            if (fp != 0) printf(" ->");
        }
        if (fp != 0) printf(" ...");
        printf("\n");
    }
}

static void cmd_stats(pager_t *pager) {
    db_header_t *hdr = &pager->header;
    printf("Rows: %" PRIu64 " (live)\n", hdr->row_count);
    printf("Next ID: %" PRIu64 "\n", hdr->next_id);
    printf("Page size: %u\n", hdr->page_size);
    printf("Row size: %u\n", hdr->row_size);
    if (hdr->row_size > 0) {
        uint32_t rows_per_page = (hdr->page_size - sizeof(heap_page_header_t)) /
                                 (hdr->row_size + sizeof(slot_t));
        printf("Rows per heap page: ~%u\n", rows_per_page);
    }
    printf("Total pages: %u\n", hdr->next_page_id);
    printf("B+ Tree height: %d\n", bptree_height(pager));
    printf("Free page head: %u\n", hdr->free_page_head);
}

int main(int argc, char **argv) {
    const char *db_path = "test.db";
    if (argc > 1) db_path = argv[1];

    pager_t pager;
    bool create = true;

    /* check if file exists */
    FILE *f = fopen(db_path, "r");
    if (f) { create = false; fclose(f); }

    if (pager_open(&pager, db_path, create) != 0) {
        fprintf(stderr, "Failed to open database: %s\n", db_path);
        return 1;
    }

    printf("minidb> Connected to %s (page_size=%u)\n", db_path, pager.page_size);

    char line[4096];
    while (1) {
        printf("minidb> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        /* trim newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        /* meta commands */
        if (line[0] == '.') {
            if (strcmp(line, ".exit") == 0 || strcmp(line, ".quit") == 0) break;
            if (strcmp(line, ".btree") == 0) { cmd_btree(&pager); continue; }
            if (strcmp(line, ".pages") == 0) { cmd_pages(&pager); continue; }
            if (strcmp(line, ".stats") == 0) { cmd_stats(&pager); continue; }
            printf("Unknown command: %s\n", line);
            continue;
        }

        statement_t stmt;
        if (parse(line, &stmt) != 0) {
            printf("Error: could not parse statement\n");
            continue;
        }

        exec_result_t res = execute(&pager, &stmt);
        if (res.message[0] != '\0')
            printf("%s\n", res.message);
    }

    pager_close(&pager);
    printf("Bye.\n");
    return 0;
}
