CC := gcc
# 目标文件
SRC_DIR := src
INC_DIR := inc
SRCS := $(wildcard $(SRC_DIR)/*.c)
BUILD_DIR := build
# 生成目标文件
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
TARGET := $(BUILD_DIR)/fingerprint

CFLAGS := -Wall -Wextra -O2 -g -std=c99
# LIBFLAGS :=
INCFLAGS := -I$(INC_DIR)
DEFFLAGS += -DLOG_USE_COLOR
#	-DSERIAL_DEBUG_ON

.PHONY: all run clean

# 默认目标
all: $(TARGET)

run: all
	@./$(TARGET)

# 清理目标文件和可执行文件
clean:
	rm -rf $(BUILD_DIR)

# 生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 生成目标文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCFLAGS) $(DEFFLAGS) -c $< -o $@
