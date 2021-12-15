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
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

// 最大的文件描述符个数
#define MAX_FD 65535

// 一次监听的最大的事件数量
#define MAX_EVENT_NUM 10000

// 注册信号处理函数
void addsig(int sig, void (*handler)(int)) {
    struct sigaction sa; // 这里必须加struct，要不然和函数名字冲突了
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
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

    // 创建一个数组保存所有客户端信息
    HttpConn *users = new HttpConn[MAX_FD]; // 哈希表？

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
    listen(listenfd, 5);

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUM];
    int epollfd = epoll_create(5);
    if (epollfd == -1) {
        perror("epoll_create");
        exit(-1);
    }

    // 将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);
    HttpConn::m_epollfd = epollfd;

    while (true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        if (num < 0 && errno != EINTR) {
            std::cerr << "epoll failed" << std::endl;
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
    }

    close(epollfd);
    close(listenfd);
    delete []users;
    delete pool;

    return 0;
}