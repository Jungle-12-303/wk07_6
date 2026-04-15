/*
 * main.c — minidb REPL (Read-Eval-Print Loop)
 *
 * 역할:
 *   사용자로부터 SQL 또는 메타 명령어를 입력받아 실행하는 인터랙티브 셸이다.
 *   GNU readline을 사용하여 방향키 이동, 히스토리(↑↓) 등 터미널 편의 기능을 지원한다.
 *
 * 명령어 흐름:
 *   1. readline()으로 입력 받기
 *   2. '.'으로 시작하면 메타 명령어 (.exit, .btree, .pages, .stats)
 *   3. 그 외는 SQL로 간주 → parse() → execute()
 *
 * 메타 명령어:
 *   .exit / .quit  — 프로그램 종료
 *   .btree         — B+ tree 구조 출력 (디버그용)
 *   .pages         — 페이지 유형별 통계 출력
 *   .stats         — DB 전체 통계 출력
 */

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

/* B+ tree 구조를 출력한다 */
static void cmd_btree(pager_t *pager)
{
    bptree_print(pager);
}

/* 페이지 유형별 개수를 집계하여 출력한다 */
static void cmd_pages(pager_t *pager)
{
    db_header_t *hdr = &pager->header;
    uint32_t heap_count = 0, leaf_count = 0, internal_count = 0, free_count = 0;

    /* 모든 페이지를 순회하며 유형별로 분류 */
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

    /* 빈 페이지 연결 리스트 출력 (최대 20개까지) */
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

/* DB 통계 정보를 출력한다 */
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

/*
 * 프로그램 진입점.
 *
 * 사용법: ./minidb [데이터베이스 파일 경로]
 * 기본값: test.db
 *
 * 파일이 존재하면 기존 DB를 열고, 없으면 새로 생성한다.
 */
int main(int argc, char **argv)
{
    const char *db_path = "test.db";
    if (argc > 1) {
        db_path = argv[1];
    }

    pager_t pager;
    bool create = true;

    /* 파일 존재 여부 확인 */
    FILE *f = fopen(db_path, "r");
    if (f != NULL) {
        create = false;
        fclose(f);
    }

    /* 데이터베이스 열기 */
    if (pager_open(&pager, db_path, create) != 0) {
        fprintf(stderr, "오류: '%s' 데이터베이스를 열 수 없습니다\n", db_path);
        return 1;
    }

    printf("minidb> '%s' 연결됨 (page_size=%u)\n", db_path, pager.page_size);

    /* REPL: readline으로 입력을 받아 반복 실행 */
    char *line;
    while ((line = readline("minidb> ")) != NULL) {
        /* 줄바꿈 문자 제거 (readline은 보통 제거하지만 안전 처리) */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* 빈 입력은 무시 */
        if (len == 0) {
            free(line);
            continue;
        }

        /* 히스토리에 추가 (↑↓ 키로 이전 명령어 재입력 가능) */
        add_history(line);

        /* 메타 명령어 처리 ('.'으로 시작) */
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

        /* SQL 파싱 및 실행 */
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

    /* 종료: 모든 변경사항을 디스크에 플러시하고 파일을 닫는다 */
    pager_close(&pager);
    printf("종료합니다.\n");
    return 0;
}
