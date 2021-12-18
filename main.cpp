#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <vector>
#include <functional>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

using namespace std;

// 最大的文件描述符个数
#define MAX_FD 65535

// 一次监听的最大的事件数量
#define MAX_EVENT_NUM 10000

static int pipefd[2];
static int epollfd = 0;

void sigHandler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], reinterpret_cast<char*>(&msg), 1, 0);
    errno = save_errno;
}

// 注册信号处理函数
void addsig(int sig, void (*handler)(int)) {
    struct sigaction sa; // 这里必须加struct，要不然和函数名字冲突了
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

void timerHandler()
{
    // 定时处理任务，实际上就是调用tick()函数
    HttpConn::timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}

// TODO: getopt
int main(int argc, char *argv[]) {
    if (argc <= 1) {
        // basename(): 从路径获得文件名
        std::cerr << "Usage: " << basename(argv[0]) << " port_number" << std::endl;
        exit(-1); // TODO: status code
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    ThreadPool<HttpConn> *pool = nullptr;
    try {
        pool = new ThreadPool<HttpConn>();
    } catch (...) {
        exit(-1);
    }

    // 创建监听的套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket");
        exit(-1);
    }

    // 设置端口复用
    // 端口复用一定要在绑定前设置
    int reuse = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt");
        exit(-1);
    }

    // 绑定
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(listenfd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        perror("bind");
        exit(-1);
    }

    // 监听
    if (listen(listenfd, 5) == -1) {
        perror("listen");
        exit(-1);
    }

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUM];
    epollfd = epoll_create(5);
    if (epollfd == -1) {
        perror("epoll_create");
        exit(-1);
    }
    HttpConn::m_epollfd = epollfd;

    // 将监听的文件描述符添加到epoll对象中
    addOtherfd(epollfd, listenfd);

    // 创建管道
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd) == -1) {
        perror("socketpair");
        exit(-1);
    }
    setnonblocking(pipefd[1]);
    addOtherfd(epollfd, pipefd[0]);

    // 设置信号处理函数
    addsig(SIGALRM, sigHandler);
    addsig(SIGTERM, sigHandler);
    bool stopServer = false;

    HttpConn* users = new HttpConn[MAX_FD]; // 哈希表
    bool timeout = false;
    alarm(TIMESLOT);  // 定时，5秒后产生SIGALARM信号

    while (!stopServer) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        if (num < 0 && errno != EINTR) {
            std::cerr << "epoll failed" << std::endl;
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                // 有客户端连接进来
                sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, reinterpret_cast<sockaddr*>(&client_address), &client_addrlen);
                if (connfd == -1) {
                    perror("accept");
                    exit(-1);
                }

                if (HttpConn::mUserCount >= MAX_FD) {
                    // 目前连接数满了
                    // TODO: 给客户端写一个信息：服务器内部正忙
                    close(connfd);
                    continue;
                }

                // 将新的客户端数据初始化，放到数组中
                users[connfd].init(connfd, client_address);
            } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 说明有信号到来，要处理信号
                int sig;
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for(int i = 0; i < ret; ++i) {
                        switch (signals[i])  {
                            case SIGALRM:
                            {
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stopServer = true;
                            }
                        }
                    }
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常端口或者错误等事件
                // 关闭连接
                users[sockfd].closeConn();
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    // 一次性把所有数据都读完
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].closeConn();
                }
            } else if (events[i].events & EPOLLOUT) {
                std::cout << "aaa" << std::endl;
                if (!users[sockfd].write()) {
                    // 一次性把所有数据都写完
                    users[sockfd].closeConn();
                }
            }
        }

        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if (timeout) {
            timerHandler();
            timeout = false;
        }
    }

    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete []users;
    delete pool;

    return 0;
}