#ifndef PAGE_FORMAT_H
#define PAGE_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

/* ── page type ── */
typedef enum {
    PAGE_TYPE_HEADER   = 0x01,
    PAGE_TYPE_HEAP     = 0x02,
    PAGE_TYPE_LEAF     = 0x03,
    PAGE_TYPE_INTERNAL = 0x04,
    PAGE_TYPE_FREE     = 0x05
} page_type_t;

/* ── DB header (page 0) ── */
#define DB_MAGIC    "MINIDB\0"
#define DB_VERSION  1
#define MAX_COLUMNS 16

typedef enum {
    COL_TYPE_INT     = 1,   /* 4 bytes  */
    COL_TYPE_BIGINT  = 2,   /* 8 bytes  */
    COL_TYPE_VARCHAR = 3    /* N bytes  */
} column_type_t;

typedef struct {
    char          name[32];
    uint8_t       type;      /* column_type_t */
    uint16_t      size;
    uint16_t      offset;
    uint8_t       is_system; /* 1 = id column */
} __attribute__((packed)) column_meta_t;

typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t page_size;
    uint32_t root_index_page_id;
    uint32_t first_heap_page_id;
    uint32_t next_page_id;
    uint32_t free_page_head;
    uint64_t next_id;
    uint64_t row_count;
    uint16_t column_count;
    uint16_t row_size;
    column_meta_t columns[MAX_COLUMNS];
} __attribute__((packed)) db_header_t;

/* ── row_ref ── */
typedef struct {
    uint32_t page_id;
    uint16_t slot_id;
} __attribute__((packed)) row_ref_t;

/* ── free page ── */
typedef struct {
    uint32_t page_type;
    uint32_t next_free_page;
} free_page_header_t;

/* ── heap page ── */
#define SLOT_ALIVE 0x01
#define SLOT_DEAD  0x02
#define SLOT_FREE  0x03
#define SLOT_NONE  0xFFFF

typedef struct {
    uint32_t page_type;          /* PAGE_TYPE_HEAP */
    uint32_t next_heap_page_id;
    uint16_t slot_count;
    uint16_t free_slot_head;     /* 0xFFFF = none */
    uint16_t free_space_offset;  /* next free byte from page end (grows down) */
    uint16_t reserved;
} __attribute__((packed)) heap_page_header_t;

typedef struct {
    uint16_t offset;    /* row payload offset within page */
    uint16_t status;    /* SLOT_ALIVE / SLOT_DEAD / SLOT_FREE */
    uint16_t next_free; /* free chain next (valid when FREE) */
    uint16_t reserved;
} __attribute__((packed)) slot_t;

/* ── B+ tree leaf ── */
typedef struct {
    uint32_t page_type;           /* PAGE_TYPE_LEAF */
    uint32_t parent_page_id;
    uint32_t key_count;
    uint32_t next_leaf_page_id;
    uint32_t prev_leaf_page_id;
} __attribute__((packed)) leaf_page_header_t;

typedef struct {
    uint64_t  key;
    row_ref_t row_ref;
} __attribute__((packed)) leaf_entry_t;

/* ── B+ tree internal ── */
typedef struct {
    uint32_t page_type;               /* PAGE_TYPE_INTERNAL */
    uint32_t parent_page_id;
    uint32_t key_count;
    uint32_t leftmost_child_page_id;
} __attribute__((packed)) internal_page_header_t;

typedef struct {
    uint64_t key;
    uint32_t right_child_page_id;
} __attribute__((packed)) internal_entry_t;

#endif /* PAGE_FORMAT_H */
