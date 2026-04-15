#include "sql/parser.h"
#include "sql/executor.h"
#include "sql/planner.h"
#include "storage/pager.h"
#include "storage/bptree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <readline/readline.h>
#include <readline/history.h>

static void cmd_btree(pager_t *pager)
{
    bptree_print(pager);
}

static void cmd_pages(pager_t *pager)
{
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
    printf("전체 페이지: %u\n", hdr->next_page_id);
    printf("  HEADER:   1\n");
    printf("  HEAP:     %u\n", heap_count);
    printf("  LEAF:     %u\n", leaf_count);
    printf("  INTERNAL: %u\n", internal_count);
    printf("  FREE:     %u\n", free_count);

    if (hdr->free_page_head != 0) {
        printf("빈 페이지 목록:");
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
            if (fp != 0) {
                printf(" ->");
            }
        }
        if (fp != 0) {
            printf(" ...");
        }
        printf("\n");
    }
}

static void cmd_stats(pager_t *pager)
{
    db_header_t *hdr = &pager->header;
    printf("행 수: %" PRIu64 " (live)\n", hdr->row_count);
    printf("다음 ID: %" PRIu64 "\n", hdr->next_id);
    printf("페이지 크기: %u\n", hdr->page_size);
    printf("행 크기: %u\n", hdr->row_size);
    if (hdr->row_size > 0) {
        uint32_t rows_per_page = (hdr->page_size - sizeof(heap_page_header_t)) /
                                 (hdr->row_size + sizeof(slot_t));
        printf("페이지당 행 수: ~%u\n", rows_per_page);
    }
    printf("전체 페이지: %u\n", hdr->next_page_id);
    printf("B+ Tree 높이: %d\n", bptree_height(pager));
    printf("빈 페이지 헤드: %u\n", hdr->free_page_head);
}

int main(int argc, char **argv)
{
    const char *db_path = "test.db";
    if (argc > 1) {
        db_path = argv[1];
    }

    pager_t pager;
    bool create = true;

    /* check if file exists */
    FILE *f = fopen(db_path, "r");
    if (f != NULL) {
        create = false;
        fclose(f);
    }

    if (pager_open(&pager, db_path, create) != 0) {
        fprintf(stderr, "오류: '%s' 데이터베이스를 열 수 없습니다\n", db_path);
        return 1;
    }

    printf("minidb> '%s' 연결됨 (page_size=%u)\n", db_path, pager.page_size);

    char *line;
    while ((line = readline("minidb> ")) != NULL) {
        /* trim newline (readline strips it, but just in case) */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0) {
            free(line);
            continue;
        }

        /* add to history */
        add_history(line);

        /* meta commands */
        if (line[0] == '.') {
            if (strcmp(line, ".exit") == 0 || strcmp(line, ".quit") == 0) {
                free(line);
                break;
            }
            if (strcmp(line, ".btree") == 0) {
                cmd_btree(&pager);
                free(line);
                continue;
            }
            if (strcmp(line, ".pages") == 0) {
                cmd_pages(&pager);
                free(line);
                continue;
            }
            if (strcmp(line, ".stats") == 0) {
                cmd_stats(&pager);
                free(line);
                continue;
            }
            printf("알 수 없는 명령어: %s\n", line);
            free(line);
            continue;
        }

        statement_t stmt;
        if (parse(line, &stmt) != 0) {
            printf("오류: SQL 구문을 해석할 수 없습니다\n");
            free(line);
            continue;
        }

        exec_result_t res = execute(&pager, &stmt);
        if (res.message[0] != '\0') {
            printf("%s\n", res.message);
        }

        free(line);
    }

    pager_close(&pager);
    printf("종료합니다.\n");
    return 0;
}
