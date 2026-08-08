// ============================================================================
// 主进程 — epoll + fork 多进程 IPC 演示（精简重写版）
//
// 【功能】
//   1. 主进程创建 TCP 监听 socket，使用 epoll 同时管理多个客户端连接
//   2. 主进程 fork 5 个子进程 (A-E)，每个子进程每秒发送 "我是进程X"
//   3. 主进程在 epoll 事件循环中接收并打印所有消息
//   4. Ctrl+C 优雅退出，自动清理所有子进程
//
// 【编译运行】
//   make && ./main
//
// 【架构】
//   ./main (父进程)
//   ├── epoll 事件循环（接收 + 打印）
//   ├── 子进程 A ──TCP──┐
//   ├── 子进程 B ──TCP──┤
//   ├── 子进程 C ──TCP──┼── 127.0.0.1:9999
//   ├── 子进程 D ──TCP──┤
//   └── 子进程 E ──TCP──┘
//
// 【epoll 原理简述】
//   epoll = Linux 高性能 I/O 多路复用。三步使用:
//     epoll_create1(0)            → 内核创建 epoll 实例
//     epoll_ctl(ADD, fd, EPOLLIN) → 把要监听的 fd 注册进去
//     epoll_wait(events, timeout) → 阻塞等待事件，返回就绪 fd 列表
//   对比 select/poll: epoll 只返回有事件的 fd（O(1)），不需要遍历全部 fd。
//
// 【LT vs ET 触发模式】
//   LT (水平触发, 默认): 缓冲区有数据就通知，不读完还会通知，容错性好
//   ET (边沿触发):       新数据到达时只通知一次，必须非阻塞+循环读，性能更高
//   本示例演示 LT 模式。
//
// 【重要问题: signal() vs sigaction()】
//   glibc 的 signal() 默认带 SA_RESTART 标志。这意味着信号处理函数返回后，
//   被中断的系统调用（如 epoll_wait）会被内核自动重启，而不是返回 EINTR。
//   后果: 在信号处理中设 running=0，epoll_wait 被默默重启，while(running)
//   永远检查不到，程序 kill 不死。必须用 sigaction() 并清除 SA_RESTART。
//
// 【epoll_wait timeout 为什么不能用 -1】
//   即使不用 SA_RESTART，timeout=-1 (永久阻塞) 也不可靠:
//   1. 完全依赖信号中断系统调用（极端情况下信号可能被屏蔽）
//   2. 竞态窗口: 检查 running 和 epoll_wait 之间信号到达可能丢失
//   用 timeout=100ms 最稳健: 每 100ms 醒来检查 running，CPU 占用几乎为零。
// ============================================================================

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

// ---- 配置 ----
#define PORT            9999
#define MAX_EVENTS      64
#define LISTEN_BACKLOG  16
#define CHILD_COUNT     5

static const char *child_ids[] = {"A", "B", "C", "D", "E"};

// ---- 全局状态 ----
static volatile sig_atomic_t running = 1;

// ============================================================================
// 信号处理 — 用 sigaction 而非 signal，关键是不设 SA_RESTART
// ============================================================================
static void on_signal(int sig) {
    if (sig == SIGCHLD) {
        // 收割僵尸子进程（WNOHANG = 不阻塞）
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
    } else {
        running = 0;
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sa.sa_flags   = 0;   // ★ 不设 SA_RESTART — 关键！
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

// ============================================================================
// 设置 fd 非阻塞
// fcntl(fd, F_GETFL) → 读当前 flags → OR 上 O_NONBLOCK → fcntl(fd, F_SETFL)
// ============================================================================
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ============================================================================
// 创建 TCP 监听 socket: socket() → setsockopt(SO_REUSEADDR) → bind() → listen()
// ============================================================================
static int tcp_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, LISTEN_BACKLOG) == -1) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

// ============================================================================
// 子进程工作函数 — 连接主进程，每秒发一条消息
// fork 之后在子进程中调用，最后 _exit() 不返回
// ============================================================================
static void child_run(const char *id) {
    // fork 后子进程继承了父进程的 fd 表（epfd, srv_fd 等），
    // 但 spawn_children 中已 close(epfd) 和 close(srv_fd)，这里不需要再关。

    char msg[64];
    snprintf(msg, sizeof(msg), "我是进程%s\n", id);

    // 等父进程 epoll 初始化完成
    sleep(1);

    // 创建 socket 并 connect（带重试，应对父进程尚未就绪）
    int fd = -1;
    while (running && fd == -1) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1) { sleep(1); continue; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(PORT);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            close(fd);
            fd = -1;
            sleep(1);
        }
    }

    // 发送循环 — 每秒一条消息
    while (running) {
        ssize_t sn = send(fd, msg, strlen(msg), MSG_NOSIGNAL);
        if (sn == -1) {
            fprintf(stderr, "[子进程 %s] send 失败: %s\n", id, strerror(errno));
            break;
        }
        fprintf(stderr, "[子进程 %s] send 成功 (%zd 字节)\n", id, sn);
        sleep(1);
    }

    close(fd);
    _exit(0);
}

// ============================================================================
// fork 子进程
// fork 调用一次，返回两次: 父进程得到子进程 PID，子进程得到 0
// ============================================================================
static void spawn_children(int epfd, int srv_fd) {
    for (int i = 0; i < CHILD_COUNT; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            continue;
        }
        if (pid == 0) {
            // ★ 子进程分支: 关闭不需要的继承 fd，进入工作循环
            // fork 复制了 fd 表，子进程不需要 epoll fd 和 listen fd
            // 关闭它们避免资源泄漏和端口占用
            close(epfd);
            close(srv_fd);
            child_run(child_ids[i]);
        }
        // 父进程分支: 记录 PID，继续 fork 下一个
        printf("[主进程] fork 子进程 %s (PID=%d)\n", child_ids[i], pid);
    }
}

// ============================================================================
// 主函数
// ============================================================================
int main(void) {
    setlinebuf(stdout);   // 行缓冲: 每行立即输出，不等待缓冲区满
    setup_signals();

    // 1. 创建 TCP 监听 socket
    int srv_fd = tcp_listen(PORT);
    if (srv_fd == -1) return 1;
    printf("[主进程] 监听 0.0.0.0:%d\n", PORT);

    // 2. 创建 epoll 实例
    int epfd = epoll_create1(0);
    if (epfd == -1) { perror("epoll_create1"); return 1; }

    // 3. 把监听 fd 加入 epoll（监听「有新连接可 accept」事件）
    struct epoll_event ev;
    ev.events  = EPOLLIN;     // 关心可读事件（对 listen fd = 有新连接）
    ev.data.fd = srv_fd;      // 事件触发时通过此字段识别是哪个 fd
    epoll_ctl(epfd, EPOLL_CTL_ADD, srv_fd, &ev);

    // 4. fork 子进程
    spawn_children(epfd, srv_fd);
    printf("[主进程] 全部子进程已启动，进入事件循环 (PID=%d)\n", getpid());

    // 5. 事件循环
    struct epoll_event events[MAX_EVENTS];

    while (running) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            // ---- listen fd: 新连接 ----
            // ★ 限制单次最多 accept 16 个连接，防止无限循环
            //   子进程断线重连时可能产生大量连接请求，如果 accept 循环不设上限，
            //   主进程就会一直 accept 新连接而永远没机会读数据（恶性循环）。
            //   限制后: 每次 epoll_wait 最多 accept 16 个，多余的留给下一轮，
            //   确保 client fd 的读事件也能得到处理。
            if (fd == srv_fd) {
                for (int j = 0; j < 16; j++) {
                    int cfd = accept(srv_fd, NULL, NULL);
                    if (cfd == -1) break;
                    fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
                    ev.events  = EPOLLIN;
                    ev.data.fd = cfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
                    printf("[主进程] 接入: fd=%d\n", cfd);
                }
                continue;
            }

            // ---- client fd: 数据或断开 ----
            // 临时诊断：确认 epoll_wait 返回了此 fd
            fprintf(stderr, "[DIAG] client fd=%d ev=0x%x\n", fd, events[i].events);

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                printf("[主进程] fd=%d 异常断开\n", fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                continue;
            }

            char buf[256];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            fprintf(stderr, "[DIAG] fd=%d read()=%zd errno=%d\n", fd, n, errno);
            if (n > 0) {
                buf[n] = '\0';
                size_t l = strlen(buf);
                while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) buf[--l] = '\0';
                printf("[主进程] fd=%d | %s\n", fd, buf);
            } else if (n == 0) {
                printf("[主进程] fd=%d 断开 (EOF)\n", fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
            }
        }
    }

    // 6. 清理
    printf("\n[主进程] 退出中...\n");
    kill(0, SIGTERM);              // 通知所有子进程
    while (wait(NULL) != -1)        // 等待全部退出
        ;
    close(epfd);
    close(srv_fd);
    printf("[主进程] 完成\n");
    return 0;
}
