#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <iostream>
#include <sys/epoll.h>
#include <cstdlib>
#include <cstring>
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
private:
    // HTTP请求方法
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE: 当前正在分析请求行
        CHECK_STATE_HEADER: 当前正在分析头部字段
        CHECK_STATE_CONTENT: 当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    /*
        从状态机的三种可能状态，即行的读取状态，分别表示：
        1. 读取到一个完整的行
        2. 行出错
        3. 行数据尚且不完整
    */
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST: 请求不完整，需要继续读取客户端
        GET_REQUEST: 表示获得了一个完成的客户请求
        BAD_REQUEST: 表示客户请求语法错误
        NO_RESOURCE: 表示服务器没有资源
        FORBIDDEN_REQUEST: 表示客户对资源没有足够的访问权限
        FILE_REQUEST: 文件请求，获取文件成功
        INTERNAL_ERROR: 表示服务器内部错误
        CLOSED_CONNECTION: 表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
public:
    static int m_epollfd; // 所有的socket上的事件都被注册到同一个epoll对象中
    static int mUserCount; // 统计用户的数量
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
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
    char mReadBuf[READ_BUFFER_SIZE]; // 读缓冲区
    int mReadIndex; // 标识读缓冲区中以及读入的客户端数据的最后一个字节的下标（下一次从这里开始读）

    int mCheckedIndex; // 当前正在分析的字符在读缓冲区的位置
    int mStartLine; // 当前正在解析的行的起始位置
    char *mUrl; // 请求目标文件的文件名
    char *mVersion; // 协议版本，只支持1.1
    METHOD mMethod; // 请求方法
    char *mHost; // 主机名
    bool mLinger; // 是否保持连接（keep-alive）

    CHECK_STATE mCheckState; // 主状态机当前所处的状态

    void initInfos(); // 初始化连接的其余信息

    HTTP_CODE processRead(); // 解析HTTP请求，主状态机
    HTTP_CODE parseRequestLine(char *text); // 解析请求首行
    HTTP_CODE parseHeaders(char *text); // 解析请求头
    HTTP_CODE parseContent(char *text); // 解析请求体

    LINE_STATUS parseLine();

    HTTP_CODE doRequest();

    // 获取一行数据
    char *getLine() {
        return mReadBuf + mStartLine;
    }
};

#endif