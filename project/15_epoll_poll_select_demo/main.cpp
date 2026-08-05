/**
 * epoll / poll / select 三者对比 Demo
 *
 * 通过实际代码演示 Linux 三种 I/O 多路复用机制的核心差异：
 *   1. select  — fd_set 位图，FD_SETSIZE(1024) 硬上限，每次调用需重置，O(n) 扫描
 *   2. poll    — pollfd 数组，无 fd 数量硬上限，但仍需 O(n) 扫描
 *   3. epoll   — 内核维护事件表，O(1) 就绪事件获取，支持 ET/LT 两种触发模式
 *
 * 构建: mkdir build && cd build && cmake .. && make
 * 运行: ./epoll_poll_select_demo
 */

#include <sys/select.h>
#include <sys/epoll.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <chrono>
#include <algorithm>

// ────────────────────────────────────────────────────────────────
// 工具函数
// ────────────────────────────────────────────────────────────────

static double now_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::milli>(now).count();
}

static void print_separator(const char *title) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  %s\n", title);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

// ────────────────────────────────────────────────────────────────
// Part 1: select 演示
// ────────────────────────────────────────────────────────────────

void demo_select() {
    print_separator("Part 1: select() — 位图式 fd 集合，FD_SETSIZE=1024 硬上限");
    printf("  ▸ API: int select(int nfds, fd_set *r, *w, *e, struct timeval *t)\n");
    printf("  ▸ 模型: 每次调用都传入「关心的 fd 集合」，内核遍历整个集合后写回就绪 fd\n");
    printf("  ▸ 代价: ① 每次都要把整个 fd_set 从用户态拷贝到内核态\n");
    printf("  ▸       ② 内核 O(n) 扫描所有 fd 检查就绪状态\n");
    printf("  ▸       ③ fd_set 被内核修改，下次调用前必须用 FD_ZERO/FD_SET 重建\n");
    printf("  ▸ 硬伤: fd 值必须 < FD_SETSIZE(通常 1024)，高并发场景直接爆炸\n\n");

    // 创建 5 对 pipe 作为待监控 fd
    const int N = 5;
    int pipes[N][2];
    for (int i = 0; i < N; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(1); }
        // 设为非阻塞（只是为了演示规范）
        fcntl(pipes[i][0], F_SETFL, O_NONBLOCK);
        fcntl(pipes[i][1], F_SETFL, O_NONBLOCK);
    }

    printf("  ▶ 创建了 %d 对 pipe，共 %d 个 fd 等待监控\n", N, N);

    // 只向其中 2 个 pipe 写入数据来模拟"就绪事件"
    const char *msg = "hello";
    write(pipes[1][1], msg, strlen(msg));   // pipe[1] 可读
    write(pipes[3][1], msg, strlen(msg));   // pipe[3] 可读
    printf("  ▶ 向 pipe[1] 和 pipe[3] 写入数据，模拟 I/O 就绪\n\n");

    // ── select 监控 ──
    fd_set readfds;
    FD_ZERO(&readfds);
    int maxfd = -1;
    for (int i = 0; i < N; i++) {
        FD_SET(pipes[i][0], &readfds);
        if (pipes[i][0] > maxfd) maxfd = pipes[i][0];
    }

    printf("  ▶ 调用 select(maxfd+1=%d, ...) 开始监控...\n", maxfd + 1);

    struct timeval tv = {1, 0}; // 1 秒超时
    double t0 = now_ms();
    int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
    double t1 = now_ms();

    printf("  ▶ select() 返回: ready=%d, 耗时 %.3f ms\n\n", ready, t1 - t0);

    printf("  ▶ 遍历 fd_set 找到就绪的 fd:\n");
    for (int i = 0; i < N; i++) {
        if (FD_ISSET(pipes[i][0], &readfds)) {
            printf("     ✓ pipe[%d] 读端 (fd=%d) 就绪\n", i, pipes[i][0]);
        } else {
            printf("     ✗ pipe[%d] 读端 (fd=%d) 未就绪\n", i, pipes[i][0]);
        }
    }

    // 展示：再次调用 select 前必须重建 fd_set（因为内核已修改）
    printf("\n  ▸ 注意: 此时 readfds 已被内核修改，只保留就绪 fd\n");
    printf("  ▸       下一次 select() 前必须重新 FD_ZERO + FD_SET 重建集合!\n");

    // 清理
    for (int i = 0; i < N; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

// ────────────────────────────────────────────────────────────────
// Part 2: poll 演示
// ────────────────────────────────────────────────────────────────

void demo_poll() {
    print_separator("Part 2: poll() — pollfd 数组，告别 FD_SETSIZE 上限");
    printf("  ▸ API: int poll(struct pollfd *fds, nfds_t nfds, int timeout)\n");
    printf("  ▸ 改进: ① 不再用位图，fd 值不受 FD_SETSIZE 限制\n");
    printf("  ▸       ② events / revents 分离，内核只写 revents，events 原样保留\n");
    printf("  ▸       ③ 不用每次重建数组，只需重置 revents\n");
    printf("  ▸ 仍存: 内核仍需 O(n) 扫描整个 pollfd 数组\n\n");

    const int N = 5;
    int pipes[N][2];
    for (int i = 0; i < N; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(1); }
        fcntl(pipes[i][0], F_SETFL, O_NONBLOCK);
        fcntl(pipes[i][1], F_SETFL, O_NONBLOCK);
    }

    // 同样只向其中 2 个 pipe 写入
    const char *msg = "hello";
    write(pipes[1][1], msg, strlen(msg));
    write(pipes[3][1], msg, strlen(msg));
    printf("  ▶ 创建 %d 对 pipe，向 pipe[1]、pipe[3] 写入数据\n\n", N);

    // ── poll 监控 ──
    struct pollfd fds[N];
    for (int i = 0; i < N; i++) {
        fds[i].fd     = pipes[i][0];
        fds[i].events = POLLIN;   // 我们关心的：可读
        fds[i].revents = 0;        // 内核会在这里写回实际就绪的事件
    }

    printf("  ▶ pollfd 数组已准备好，events=POLLIN, revents 初始为 0\n");
    printf("  ▶ 调用 poll(fds, nfds=%d, timeout=1000) 开始监控...\n", N);

    double t0 = now_ms();
    int ready = poll(fds, N, 1000);
    double t1 = now_ms();

    printf("  ▶ poll() 返回: ready=%d, 耗时 %.3f ms\n\n", ready, t1 - t0);

    printf("  ▶ 遍历 pollfd 数组，检查 revents:\n");
    for (int i = 0; i < N; i++) {
        if (fds[i].revents & POLLIN) {
            printf("     ✓ pipe[%d] (fd=%d) revents=0x%04x → POLLIN 就绪\n",
                   i, fds[i].fd, fds[i].revents);
        } else {
            printf("     ✗ pipe[%d] (fd=%d) revents=0x%04x → 未就绪\n",
                   i, fds[i].fd, fds[i].revents);
        }
    }

    printf("\n  ▸ 对比 select: events 没被破坏！下次 poll 只需把 revents 清零即可\n");
    printf("  ▸ 无需重建 pollfd 数组，events 字段原样保留\n");

    for (int i = 0; i < N; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

// ────────────────────────────────────────────────────────────────
// Part 3: epoll 演示（Level-Triggered）
// ────────────────────────────────────────────────────────────────

void demo_epoll_lt() {
    print_separator("Part 3: epoll (Level-Triggered, 默认) — 内核维护事件表");
    printf("  ▸ API: epoll_create1 / epoll_ctl / epoll_wait\n");
    printf("  ▸ 核心思想: 把「注册 fd」和「等待事件」分离\n");
    printf("  ▸         ① epoll_create1() — 内核分配一个 eventpoll 对象\n");
    printf("  ▸         ② epoll_ctl(ADD/MOD/DEL) — 向内核注册/修改/删除关心的 fd\n");
    printf("  ▸         ③ epoll_wait() — 内核只返回已就绪的 fd，O(1) 就绪事件获取\n");
    printf("  ▸ LT 模式: 只要 fd 还就绪，每次 epoll_wait 都会通知（不丢事件、安全）\n\n");

    const int N = 5;
    int pipes[N][2];
    for (int i = 0; i < N; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(1); }
        fcntl(pipes[i][0], F_SETFL, O_NONBLOCK);
        fcntl(pipes[i][1], F_SETFL, O_NONBLOCK);
    }

    const char *msg = "hello";
    write(pipes[1][1], msg, strlen(msg));
    write(pipes[3][1], msg, strlen(msg));
    printf("  ▶ 创建 %d 对 pipe，向 pipe[1]、pipe[3] 写入数据\n\n", N);

    // ── epoll LT 监控 ──
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }
    printf("  ▶ epoll_create1(0) → epfd=%d (内核分配 eventpoll 对象)\n", epfd);

    for (int i = 0; i < N; i++) {
        struct epoll_event ev;
        ev.events   = EPOLLIN;          // 默认就是 LT（不设 EPOLLET）
        ev.data.fd  = pipes[i][0];      // 把 fd 存进 data，回传时能对上号
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, pipes[i][0], &ev) < 0) {
            perror("epoll_ctl ADD"); exit(1);
        }
        printf("  ▶ epoll_ctl(ADD) fd=%d (pipe[%d] 读端) events=EPOLLIN (LT)\n",
               pipes[i][0], i);
    }

    printf("\n  ▶ 第 1 次 epoll_wait (LT 模式)...\n");
    struct epoll_event events[10];
    double t0 = now_ms();
    int ready = epoll_wait(epfd, events, 10, 1000);
    double t1 = now_ms();
    printf("  ▶ epoll_wait() 返回: ready=%d, 耗时 %.3f ms\n\n", ready, t1 - t0);

    for (int i = 0; i < ready; i++) {
        printf("     ✓ events[%d].data.fd=%d, events=0x%08x\n",
               i, events[i].data.fd, events[i].events);
    }

    // ── LT 关键行为：不读数据，再次 epoll_wait 仍会通知 ──
    printf("\n  ▸ [LT 关键行为] 因为没有读取 pipe 里的数据，fd 仍然就绪\n");
    printf("  ▶ 第 2 次 epoll_wait (LT, 不消费数据)...\n");
    ready = epoll_wait(epfd, events, 10, 100);
    printf("  ▶ epoll_wait() 返回: ready=%d ← LT 模式继续通知!\n", ready);
    printf("  ▸ 这就是 LT 的安全性：只要还有数据没读完，每次 wait 都会返回这个 fd\n");

    // 消费数据
    char buf[64];
    for (int i = 0; i < N; i++) {
        while (read(pipes[i][0], buf, sizeof(buf)) > 0) {}
    }

    close(epfd);
    for (int i = 0; i < N; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

// ────────────────────────────────────────────────────────────────
// Part 4: epoll 演示（Edge-Triggered）
// ────────────────────────────────────────────────────────────────

void demo_epoll_et() {
    print_separator("Part 4: epoll (Edge-Triggered) — 高性能但要求非阻塞 + 循环读");
    printf("  ▸ ET 模式: 只在状态从「未就绪→就绪」的边沿通知一次\n");
    printf("  ▸ 优势: 减少不必要的重复通知，高并发下大幅降低系统调用次数\n");
    printf("  ▸ 要求: ① fd 必须是非阻塞的 (O_NONBLOCK)\n");
    printf("  ▸       ② 收到通知后必须循环 read/write 直到 EAGAIN\n");
    printf("  ▸       ③ 漏读会导致数据永远丢失（没有下次通知了）\n\n");

    const int N = 5;
    int pipes[N][2];
    for (int i = 0; i < N; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(1); }
        fcntl(pipes[i][0], F_SETFL, O_NONBLOCK);
        fcntl(pipes[i][1], F_SETFL, O_NONBLOCK);
    }

    const char *msg = "hello";
    write(pipes[1][1], msg, strlen(msg));
    write(pipes[3][1], msg, strlen(msg));
    printf("  ▶ 创建 %d 对 pipe，向 pipe[1]、pipe[3] 写入数据\n\n", N);

    // ── epoll ET 监控 ──
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    for (int i = 0; i < N; i++) {
        struct epoll_event ev;
        ev.events   = EPOLLIN | EPOLLET;   // ← 关键：加了 EPOLLET
        ev.data.fd  = pipes[i][0];
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, pipes[i][0], &ev) < 0) {
            perror("epoll_ctl ADD"); exit(1);
        }
        printf("  ▶ epoll_ctl(ADD) fd=%d (pipe[%d] 读端) events=EPOLLIN|EPOLLET\n",
               pipes[i][0], i);
    }

    printf("\n  ▶ 第 1 次 epoll_wait (ET 模式)...\n");
    struct epoll_event events[10];
    double t0 = now_ms();
    int ready = epoll_wait(epfd, events, 10, 1000);
    double t1 = now_ms();
    printf("  ▶ epoll_wait() 返回: ready=%d, 耗时 %.3f ms\n\n", ready, t1 - t0);

    for (int i = 0; i < ready; i++) {
        printf("     ✓ events[%d].data.fd=%d, events=0x%08x\n",
               i, events[i].data.fd, events[i].events);
    }

    // ── ET 关键行为：不读数据，再次 epoll_wait 不会通知！ ──
    printf("\n  ▸ [ET 关键行为] 没有读取 pipe 里的数据，但 fd 状态没有变化\n");
    printf("  ▸               (没有从「未就绪」到「就绪」的边沿)\n");
    printf("  ▶ 第 2 次 epoll_wait (ET, 不消费数据)...\n");
    ready = epoll_wait(epfd, events, 10, 100);
    printf("  ▶ epoll_wait() 返回: ready=%d ← ET 模式不再通知！\n", ready);
    printf("  ▸ 如果此时不主动读完数据，这些数据就永远'丢失'在这个 fd 上\n");

    // 正确的 ET 读法：循环读到 EAGAIN
    printf("\n  ▸ 正确的 ET 处理方式：收到通知后循环 read 直到 EAGAIN\n");
    for (int i = 0; i < N; i++) {
        char buf[64];
        int total = 0;
        while (true) {
            ssize_t n = read(pipes[i][0], buf, sizeof(buf));
            if (n > 0) {
                total += n;
            } else if (n == 0) {
                break;  // EOF（pipe 写端关闭）
            } else {
                // n < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // 没数据了，正常退出
                }
                perror("read"); break;
            }
        }
        if (total > 0) {
            printf("     pipe[%d] 读到 %d 字节\n", i, total);
        }
    }

    close(epfd);
    for (int i = 0; i < N; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

// ────────────────────────────────────────────────────────────────
// Part 5: 性能对比 — 大量 fd 场景
// ────────────────────────────────────────────────────────────────

void demo_benchmark() {
    print_separator("Part 5: 性能对比 — 模拟大量 fd 场景");

    const int TOTAL_FD = 100;  // 用 100 个 fd 来放大 O(n) vs O(1) 的差异
    printf("  ▶ 场景: 创建 %d 对 pipe，其中仅 3 个有数据就绪\n", TOTAL_FD);
    printf("  ▶ 预期: select/poll 要 O(n) 遍历全部 %d 个，epoll 只返回就绪的 3 个\n\n", TOTAL_FD);

    // 创建 TOTAL_FD 对 pipe
    std::vector<int> read_fds(TOTAL_FD);
    std::vector<int> write_fds(TOTAL_FD);
    for (int i = 0; i < TOTAL_FD; i++) {
        int p[2];
        if (pipe(p) < 0) { perror("pipe"); exit(1); }
        read_fds[i]  = p[0];
        write_fds[i] = p[1];
        fcntl(read_fds[i], F_SETFL, O_NONBLOCK);
    }

    // 只让 3 个 pipe 就绪
    const char *msg = "benchmark";
    write(write_fds[10], msg, strlen(msg));
    write(write_fds[50], msg, strlen(msg));
    write(write_fds[90], msg, strlen(msg));

    // ── select 计时 ──
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = 0;
        for (int i = 0; i < TOTAL_FD; i++) {
            FD_SET(read_fds[i], &rfds);
            if (read_fds[i] > maxfd) maxfd = read_fds[i];
        }
        struct timeval tv = {0, 100000}; // 100ms
        double t0 = now_ms();
        int n = select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        double t1 = now_ms();
        printf("  ▶ select:  ready=%d, 耗时 %.4f ms  (遍历上限=%d)\n", n, t1 - t0, maxfd + 1);
    }

    // ── poll 计时 ──
    {
        std::vector<struct pollfd> fds(TOTAL_FD);
        for (int i = 0; i < TOTAL_FD; i++) {
            fds[i].fd     = read_fds[i];
            fds[i].events = POLLIN;
            fds[i].revents = 0;
        }
        double t0 = now_ms();
        int n = poll(fds.data(), fds.size(), 100);
        double t1 = now_ms();
        printf("  ▶ poll:    ready=%d, 耗时 %.4f ms  (遍历数量=%zu)\n", n, t1 - t0, fds.size());
    }

    // ── epoll 计时 ──
    {
        int epfd = epoll_create1(0);
        for (int i = 0; i < TOTAL_FD; i++) {
            struct epoll_event ev;
            ev.events  = EPOLLIN;
            ev.data.fd = read_fds[i];
            epoll_ctl(epfd, EPOLL_CTL_ADD, read_fds[i], &ev);
        }
        struct epoll_event events[64];
        double t0 = now_ms();
        int n = epoll_wait(epfd, events, 64, 100);
        double t1 = now_ms();
        printf("  ▶ epoll:   ready=%d, 耗时 %.4f ms  (内核直接返回就绪列表)\n", n, t1 - t0);

        close(epfd);
    }

    // 清理
    for (int i = 0; i < TOTAL_FD; i++) {
        close(read_fds[i]);
        close(write_fds[i]);
    }
}

// ────────────────────────────────────────────────────────────────
// Part 6: 总结对比表
// ────────────────────────────────────────────────────────────────

void demo_summary() {
    print_separator("Part 6: 总结对比");

    printf("  ┌──────────┬─────────────────────┬─────────────────────┬──────────────────────┐\n");
    printf("  │ 特性     │ select              │ poll                │ epoll                │\n");
    printf("  ├──────────┼─────────────────────┼─────────────────────┼──────────────────────┤\n");
    printf("  │ API      │ select(nfds, r,w,e) │ poll(fds,n,timeout) │ epoll_create/ctl/wait│\n");
    printf("  │ 数据结构 │ fd_set (位图)       │ struct pollfd[]     │ 内核红黑树+就绪链表  │\n");
    printf("  │ fd 上限  │ FD_SETSIZE (1024)   │ 无硬限制            │ 无硬限制             │\n");
    printf("  │ 参数复用 │ 每次重建 fd_set     │ 只需清 revents      │ events 数组纯输出    │\n");
    printf("  │ 扫描方式 │ O(n) 全量扫描       │ O(n) 全量扫描       │ O(1) 只返回就绪 fd   │\n");
    printf("  │ 内核拷贝 │ 每次传入整个位图    │ 每次传入整个数组    │ 只注册时拷贝一次     │\n");
    printf("  │ 触发模式 │ 仅 LT (电平触发)    │ 仅 LT (电平触发)    │ LT + ET (边沿触发)   │\n");
    printf("  │ fd 增长  │ 性能线性下降        │ 性能线性下降        │ 性能几乎不随 fd 增长 │\n");
    printf("  │ 适用场景 │ 小规模/兼容老代码   │ 中等规模            │ 大规模高并发         │\n");
    printf("  └──────────┴─────────────────────┴─────────────────────┴──────────────────────┘\n");

    printf("\n  ▸ 选型建议:\n");
    printf("    - fd < 10 且要跨平台（Windows/macOS）           → select/poll\n");
    printf("    - fd < 100, Linux only, 简单场景               → poll\n");
    printf("    - fd > 100 或 需要高性能                        → epoll (LT)\n");
    printf("    - 海量连接(>10000)、对延迟极度敏感              → epoll (ET)\n");
    printf("    - 注意: Windows 的 IOCP、macOS 的 kqueue 是各自的 epoll 等价物\n");
}

// ────────────────────────────────────────────────────────────────
// main
// ────────────────────────────────────────────────────────────────

int main() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     epoll / poll / select — Linux I/O 多路复用对比 Demo     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    demo_select();
    demo_poll();
    demo_epoll_lt();
    demo_epoll_et();
    demo_benchmark();
    demo_summary();

    return 0;
}
