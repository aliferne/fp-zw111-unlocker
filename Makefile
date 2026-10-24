CC := gcc
# 源码/头文件/构建目录
SRC_DIR := src
INC_DIR := inc
BUILD_DIR := build

# 入口与 PAM 模块源文件
MAIN_SRC := $(SRC_DIR)/main.c
PAM_SRC := $(SRC_DIR)/pam_fprint_zw111.c
HEADERS := $(wildcard $(INC_DIR)/*.h)

# 公共核心源码（协议、串口、日志等，主程序与 PAM 模块共用）
CORE_SRCS := $(filter-out $(MAIN_SRC) $(PAM_SRC), $(wildcard $(SRC_DIR)/*.c))
# 主程序源码：核心源码 + main.c，不包含 PAM 模块
APP_SRCS := $(CORE_SRCS) $(MAIN_SRC)

# 生成目标文件
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(APP_SRCS))
TARGET := $(BUILD_DIR)/fingerprint
PAM_TARGET := $(BUILD_DIR)/pam_fprint_zw111.so

CFLAGS := -Wall -Wextra -O2 -g -std=gnu99
PAM_CFLAGS := $(CFLAGS) -fPIC -shared
# LIBFLAGS := -lrt
INCFLAGS := -I$(INC_DIR)
DEFFLAGS += -DLOG_USE_COLOR \
	-DPROTOCOL_USE_LOG
	# -DSERIAL_DEBUG_ON

.PHONY: all run pam clean

# 默认目标：主程序 + PAM 模块
all: $(TARGET) $(PAM_TARGET)

run: all
	@./$(TARGET)

pam: $(PAM_TARGET)

# 清理目标文件和可执行文件
clean:
	rm -rf $(BUILD_DIR)

# 生成可执行文件（不含 pam_fprint_zw111.c）
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 生成 PAM 共享库（不含 main.c，复用其依赖的核心源码）
$(PAM_TARGET): $(PAM_SRC) $(CORE_SRCS) $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(PAM_CFLAGS) $(INCFLAGS) $(DEFFLAGS) $(PAM_SRC) $(CORE_SRCS) -o $@

# 生成目标文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCFLAGS) $(DEFFLAGS) -c $< -o $@
