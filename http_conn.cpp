#include "http_conn.h"

int HttpConn::m_epollfd = -1;
int HttpConn::mUserCount = 0;

void setnumblocking(int fd) {
    int oldFlag = fcntl(fd, F_GETFL);
    int newFlag = oldFlag | O_NONBLOCK;
    fcntl(fd, F_SETFL, newFlag);
}

void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; // TODO: 是否使用ET可以作为程序的选项

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnumblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLON能被触发
void modifyfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLRDHUP | EPOLLONESHOT; // TODO: 是否使用ET可以作为程序的选项
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

HttpConn::HttpConn() {

}

HttpConn::~HttpConn() {

}

void HttpConn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;
    // 设置端口复用
    // 端口复用一定要在绑定前设置
    int reuse = 1;
    if (setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt");
        exit(-1);
    }

    // 添加到epoll对象中
    addfd(m_epollfd, sockfd, true);
    mUserCount++; // 总用户数+1
}

void HttpConn::closeConn() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        mUserCount--; // 关闭一个连接，客户总数量-1
    }
}

bool HttpConn::read() {
    std::cout << "read" << std::endl;
    return true;
}

bool HttpConn::write() {
    std::cout << "write" << std::endl;
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConn::process() {
    // 解析HTPP请求
    std::cout << "parse request, create response" << std::endl;
    // 生成响应
}