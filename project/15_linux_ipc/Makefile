# ============================================================================
# Makefile — epoll + fork 多进程 IPC 演示
#
# 目标:
#   make        - 编译所有程序
#   make run    - 一键编译并运行（主进程自动 fork 子进程）
#   make clean  - 清理编译产物
#   make kill   - 终止所有相关进程
#
# 注意: main 已集成 fork，一键启动即可，无需单独启动 child。
#       child 可独立编译用于手动测试场景。
# ============================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS =

# main 是核心——集成了 epoll 主循环 + fork 子进程
# child 是独立的子进程程序（用于手动启动/测试）
TARGETS = main child

.PHONY: all clean run kill

all: $(TARGETS)

main: main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

child: child.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# 一键启动: main 会自动 fork 出 5 个子进程 (A-E)
# 按 Ctrl+C 停止
run: all
	@echo "========================================"
	@echo "  epoll + fork 多进程 IPC 演示"
	@echo "  主进程自动 fork $(shell grep CHILD_COUNT main.c | head -1 | grep -oP '\d+') 个子进程 (A-E)"
	@echo "  按 Ctrl+C 停止所有进程"
	@echo "========================================"
	@echo ""
	./main

# 手动模式: 先启动 main (带 fork)，也可以用 child 独立启动额外子进程
run-manual: all
	@echo "手动模式: 启动 main (自动 fork 子进程)"
	./main &
	@sleep 2
	@echo ""
	@echo "已后台运行。如需额外子进程: ./child F &"
	@echo "终止: make kill"

# 杀掉所有相关进程
kill:
	@pkill -f './main'  2>/dev/null || true
	@pkill -f './child' 2>/dev/null || true
	@echo "已终止所有进程"

clean:
	rm -f $(TARGETS)
