CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -g -Iinclude -fsanitize=address,undefined
LDFLAGS = -fsanitize=address,undefined -lpthread -lreadline

SRC_DIR   = src
BUILD_DIR = build

SRCS = $(SRC_DIR)/storage/pager.c \
       $(SRC_DIR)/storage/schema.c \
       $(SRC_DIR)/storage/table.c \
       $(SRC_DIR)/storage/bptree.c \
       $(SRC_DIR)/sql/parser.c \
       $(SRC_DIR)/sql/planner.c \
       $(SRC_DIR)/sql/executor.c

OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# main binary
all: $(BUILD_DIR)/minidb

$(BUILD_DIR)/minidb: $(OBJS) $(BUILD_DIR)/main.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# test binary
TEST_SRC = tests/test_all.c
$(BUILD_DIR)/test_all: $(TEST_SRC) $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test: $(BUILD_DIR)/test_all
	./$(BUILD_DIR)/test_all

run: $(BUILD_DIR)/minidb
	./$(BUILD_DIR)/minidb sql.db

clean:
	rm -rf $(BUILD_DIR) *.db

.PHONY: all test run clean
