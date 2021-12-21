#include "epoll.h"

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET; // TODO: 是否使用ET可以作为程序的选项

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

void addOtherfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET; // TODO: 是否使用ET可以作为程序的选项
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    // close(fd); // fd引用计数-1，当引用计数减为0，同时关闭服务器到客户端的的读端和写端（有可能通过3次挥手就能关闭连接）
    shutdown(fd, SHUT_RDWR); // 优雅关闭连接，只关闭服务器到客户端的写端
    // 如果客户端不打算关闭连接（HTTP的keep-alive），则在此之后服务端进入FIN_WAIT_2状态
    // 缩短这个状态的持续时长需要修改net.ipv4.tcp_fin_timeout参数
}

// 重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLON能被触发
void modifyfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLRDHUP | EPOLLONESHOT; // TODO: 是否使用ET可以作为程序的选项
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}