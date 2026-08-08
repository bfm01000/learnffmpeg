// ============================================================================
// 子进程 — 每秒通过 socket 向主进程发送 "我是进程X"
//
// 【功能】
//   独立运行的子进程。连接到主进程后，每秒发送一条带身份标识的消息。
//   支持断线自动重连。
//
// 【使用方式】
//   ./child A      → 以「进程A」身份运行
//   ./child B      → 以「进程B」身份运行
//   （通常由脚本或 Makefile 批量启动，也可从主进程通过 fork+exec 启动）
//
// 【与主进程的关系】
//   主进程 (TCP Server) ←———— TCP 连接 ————→ 子进程 (TCP Client)
//   每个子进程是独立的 OS 进程，拥有自己的 PID、地址空间、文件描述符表。
//   它们通过 TCP loopback (127.0.0.1) 与主进程通信——即使在同一台机器上，
//   TCP 协议栈仍然完整工作（三次握手、拥塞控制、ACK 确认等），
//   这比 pipe 或 Unix domain socket 更接近真实网络编程场景。
// ============================================================================

#define _GNU_SOURCE          // 启用 GNU 扩展
#include <arpa/inet.h>       // inet_pton, htons
#include <errno.h>           // errno, EPIPE, ECONNRESET
#include <netinet/in.h>      // sockaddr_in
#include <signal.h>          // signal, SIGTERM, SIGINT, SIGPIPE, sig_atomic_t
#include <stdio.h>           // printf, fprintf, perror, snprintf
#include <stdlib.h>          // EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>          // memset, strlen
#include <sys/socket.h>      // socket, connect, send, MSG_NOSIGNAL
#include <unistd.h>          // close, sleep

#define PORT 9999            // 与主进程相同的端口

// ============================================================================
// 全局运行标志 — 信号安全的退出机制
//
// 【sig_atomic_t 是什么】
//   C 标准规定的「信号处理函数中可以安全读写的整数类型」。
//   通常是 int 或 long 的 typedef。volatile 告诉编译器:
//   「这个变量的值可能在任何时刻被改变（信号处理函数在栈之外异步执行）」，
//   禁止编译器对它做缓存优化——每次访问都必须从内存重新读取。
//
//   不加 volatile 的后果:
//     编译器可能把 running 缓存在寄存器里：
//       mov eax, [running]   ; 只读一次
//       .loop:
//         cmp eax, 1         ; 一直用寄存器里的旧值
//         jne .done
//         ...
//         jmp .loop
//     信号处理函数改了内存里的 running=0，但循环永远看不到。
// ============================================================================
static volatile sig_atomic_t running = 1;

// ============================================================================
// 信号处理函数
//
// 【参数 int sig】
//   信号编号（如 SIGTERM=15, SIGINT=2）。同一个处理函数可以注册到多个信号，
//   通过 sig 参数区分是哪个信号触发的。这里我们只做同一件事（running=0），
//   所以用 (void)sig 显式标记「我知道这个参数存在但不需要用」，消除编译器警告。
// ============================================================================
static void sig_handler(int sig) {
    (void)sig;
    running = 0;   // 通知主循环优雅退出
}

// ============================================================================
// 子进程主函数
//
// 【argc, argv】
//   argc: 命令行参数个数（包括程序名本身）
//   argv: 命令行参数字符串数组
//     argv[0] = "./child"
//     argv[1] = "A"  （进程标识）
// ============================================================================
int main(int argc, char *argv[]) {
    // ---- 参数校验 ----
    if (argc < 2) {
        fprintf(stderr, "用法: %s <进程标识(A-F)>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // ---- 构造消息内容 ----
    const char *id   = argv[1];               // 进程标识，如 "A"
    char        msg[64];
    // snprintf: 安全的 sprintf，第三个参数是缓冲区大小，绝不会溢出
    // msg 自带 \n，每次发送就是完整的一行
    snprintf(msg, sizeof(msg), "我是进程%s\n", id);

    // ---- 注册信号处理 ----
    // signal() 的第二个参数是函数指针:
    //   SIG_IGN (SIGnore): 忽略该信号
    //   SIG_DFL (DeFauLt): 恢复默认行为
    //   自定义函数指针:    收到信号时调用
    //
    // SIGPIPE 特别说明:
    //   当向一个已关闭的 socket 写入数据时，内核会发送 SIGPIPE 给进程。
    //   默认行为是杀死进程。我们设成 SIG_IGN 忽略它——
    //   这样 send() 会返回 -1 并设 errno=EPIPE，而不是直接 crash。
    //   几乎所有网络程序都应该忽略 SIGPIPE 或使用 MSG_NOSIGNAL。
    signal(SIGTERM, sig_handler);   // kill <pid> 或 killall
    signal(SIGINT,  sig_handler);   // Ctrl+C
    signal(SIGPIPE, SIG_IGN);       // 写已关闭的 socket → 返回 EPIPE 而非 crash

    // ========================================================================
    // 外层循环: 连接 → 发送 → 断线 → 重连
    //
    // 这种设计保证了子进程的健壮性:
    //   - 主进程还没启动？等一秒重试 connect
    //   - 主进程中途挂了？检测到 EPIPE 后重连
    //   - 主进程重启了？自动连上继续发送
    // ========================================================================
    while (running) {
        // ---- 第 1 步: 创建 socket ----
        // AF_INET = IPv4, SOCK_STREAM = TCP
        // 注意: 每次重连都要创建新的 socket——
        // 旧的 socket 在连接断开后已不可用，必须重新创建
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1) {
            perror("socket");
            sleep(1);
            continue;
        }

        // ---- 第 2 步: 填充服务器地址 ----
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));    // 清零 padding 字节
        addr.sin_family = AF_INET;         // IPv4
        addr.sin_port   = htons(PORT);     // 端口（主机序 → 网络序）
        // inet_pton: 将点分十进制 IP 字符串转为网络字节序的 32-bit 整数
        //   "127.0.0.1" → 0x7F000001（大端）
        // 返回值: 1=成功, 0=格式错误, -1=协议族不支持
        if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
            perror("inet_pton");
            close(fd);
            sleep(1);
            continue;
        }

        // ---- 第 3 步: 连接主进程 ----
        // connect() 触发 TCP 三次握手:
        //   客户端 → SYN (seq=x)           → 服务器
        //   客户端 ← SYN+ACK (seq=y,ack=x+1) ← 服务器
        //   客户端 → ACK (ack=y+1)         → 服务器
        // 握手完成，connect 返回，连接进入 ESTABLISHED 状态
        //
        // 注意: connect 是阻塞调用。如果服务器没在监听，会返回 ECONNREFUSED，
        // 不会长时间阻塞（不像 read 没有数据会一直等）。
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "[进程%s] 连接失败，1秒后重试...\n", id);
            close(fd);
            sleep(1);
            continue;
        }

        // 连接成功，打印提示
        // 注意: 这段输出只在连接建立时打印一次，不是每秒都打印
        printf("[进程%s] 已连接主进程，开始发送消息\n", id);

        // ---- 第 4 步: 循环发送消息（每秒一条） ----
        while (running) {
            // send 参数说明:
            //   fd:          目标 socket
            //   msg:         要发送的数据
            //   strlen(msg): 数据长度（字节数）
            //                send 不像字符串函数，不会自动算长度，必须显式指定
            //   MSG_NOSIGNAL: 如果对端已关闭连接，返回 EPIPE 错误码
            //                 而不是发送 SIGPIPE 信号杀死进程
            //                 Linux 2.2+ 支持，等同于提前设 SIGPIPE=SIG_IGN
            ssize_t n = send(fd, msg, strlen(msg), MSG_NOSIGNAL);

            if (n == -1) {
                // send 失败了——检查是不是连接断开导致的
                if (errno == EPIPE || errno == ECONNRESET) {
                    // EPIPE: Broken Pipe——对端已关闭，继续写会触发
                    // ECONNRESET: Connection Reset——对端发了 RST 包
                    fprintf(stderr, "[进程%s] 主进程断开，重新连接...\n", id);
                    break;   // 跳出内层循环 → 回到外层循环 → 重新 connect
                }
                // 其他错误（如 EBADF 无效 fd）——打印并退出
                perror("send");
                break;
            }

            // send 返回值 n 是实际发送的字节数
            // TCP 是字节流协议，理论上 send 可能只发送了部分数据
            //   但对我们这种几十字节的小消息+loopback 网络，基本不会出现
            //   生产环境应该用循环 send 确保全部发送完毕
            printf("[进程%s] 发送: %s", id, msg);  // msg 自带 \n，不需要额外换行
            sleep(1);
        }

        // ---- 关闭旧连接 ----
        // close 触发 TCP 四次挥手:
        //   主动关闭方 → FIN → 被动关闭方
        //   主动关闭方 ← ACK ← 被动关闭方
        //   主动关闭方 ← FIN ← 被动关闭方
        //   主动关闭方 → ACK → 被动关闭方
        // 主动关闭方进入 TIME_WAIT 状态（约 60 秒）
        close(fd);
    }

    printf("[进程%s] 退出\n", id);
    return EXIT_SUCCESS;
}
