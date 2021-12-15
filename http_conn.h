#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <iostream>
#include <sys/epoll.h>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <cstdio>
#include "locker.h"

// 添加需要监听的文件描述符到epoll中
void addfd(int epollfd, int fd, bool one_shot);

// 从epoll中移除文件描述符
void removefd(int epollfd, int fd);

// 修改文件描述符
void modifyfd(int epollfd, int fd, int ev);

class HttpConn {
public:
    static int m_epollfd; // 所有的socket上的事件都被注册到同一个epoll对象中
    static int mUserCount; // 统计用户的数量
    HttpConn();
    ~HttpConn();
    void process(); // 处理客户端的请求
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接收的连接
    void closeConn(); // 关闭连接
    bool read(); // 非阻塞的读
    bool write(); // 非阻塞的写
private:
    // static int m_epollfd; // 所有的socket上的事件都被注册到同一个epoll对象中
    // static int mUserCount; // 统计用户的数量
    int m_sockfd; // 该HTTP连接的socket
    sockaddr_in m_address; // 通信的socket地址
};

#endif