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

// 每隔5s的定时，检测有没有任务超时
#define TIMESLOT 5

// 添加需要监听的文件描述符到epoll中
void addfd(int epollfd, int fd, bool one_shot);
void addOtherfd(int epollfd, int fd);

// 从epoll中移除文件描述符
void removefd(int epollfd, int fd);

// 修改文件描述符
void modifyfd(int epollfd, int fd, int ev);

int setnonblocking(int fd);

class util_timer;   // 前向声明
class sort_timer_lst;

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
    static const int FILENAME_LENGTH = 200; // 文件名的最大长度
    static sort_timer_lst timer_lst;
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
    int mContentLength; // HTTP请求的消息总长度

    CHECK_STATE mCheckState; // 主状态机当前所处的状态

    char mRealFile[FILENAME_LENGTH]; // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char mWriteBuf[WRITE_BUFFER_SIZE];  // 写缓冲区
    int mWriteIndex; // 写缓冲区中待发送的字节数
    char *mFileAddress; // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat mFileStat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2]; // 要写的内存块，有两块：一块是空行前面的响应行（mWriteBuf），另一块是空行之后的文件缓冲区（mFileAddress）
    int m_iv_Count; // 被写内存块的数量

    int mBytesHaveSend; // 将要发送的数据的字节数
    int mBytesToSend; // 已经发送的字节数

    util_timer* mTimer; // 定时器

    void initInfos(); // 初始化连接的其余信息

    HTTP_CODE processRead(); // 解析HTTP请求，主状态机
    bool processWrite(HTTP_CODE ret);
    HTTP_CODE parseRequestLine(char *text); // 解析请求首行
    HTTP_CODE parseHeaders(char *text); // 解析请求头
    HTTP_CODE parseContent(char *text); // 解析请求体

    bool addStatusLine(int status, const char* title); // 生成响应首行
    bool addResponse(const char *format, ...); // 往缓冲区中写入待发送的数据
    void addHeaders(int contentLen); // 生成响应头
    bool addContentLength(int contentLen);
    bool addContentType();
    bool addLinger();
    bool addBlankLine();
    bool addContent(const char *content);

    LINE_STATUS parseLine();

    HTTP_CODE doRequest();

    void unmap(); // 解除对文件的内存映射

    // 获取一行数据
    char *getLine() {
        return mReadBuf + mStartLine;
    }
};

// 定时器类
class util_timer {
public:
    util_timer() : prev(nullptr), next(nullptr) {}

public:
    time_t expire;   // 任务超时时间，这里使用绝对时间
    HttpConn *userData;
    util_timer* prev;    // 指向前一个定时器
    util_timer* next;    // 指向后一个定时器
};

// 定时器链表，它是一个升序、双向链表，且带有头节点和尾节点。
class sort_timer_lst {
public:
    sort_timer_lst() : head(nullptr), tail(nullptr) {}
    // 链表被销毁时，删除其中所有的定时器
    ~sort_timer_lst() {
        util_timer* tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    
    // 将目标定时器timer添加到链表中
    void add_timer(util_timer* timer) {
        std::cout << "add_timer" << std::endl;
        if (!timer) {
            return;
        }
        if (!head) {
            head = tail = timer;
            return; 
        }
        /* 如果目标定时器的超时时间小于当前链表中所有定时器的超时时间，则把该定时器插入链表头部,作为链表新的头节点，
           否则就需要调用重载函数 add_timer(),把它插入链表中合适的位置，以保证链表的升序特性 */
        if (timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }
    
    /* 当某个定时任务发生变化时，调整对应的定时器在链表中的位置。这个函数只考虑被调整的定时器的
    超时时间延长的情况，即该定时器需要往链表的尾部移动。*/
    void adjust_timer(util_timer* timer)
    {
        if (!timer)  {
            return;
        }
        util_timer* tmp = timer->next;
        // 如果被调整的目标定时器处在链表的尾部，或者该定时器新的超时时间值仍然小于其下一个定时器的超时时间则不用调整
        if (!tmp || (timer->expire < tmp->expire)) {
            return;
        }
        // 如果目标定时器是链表的头节点，则将该定时器从链表中取出并重新插入链表
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer, head);
        } else {
            // 如果目标定时器不是链表的头节点，则将该定时器从链表中取出，然后插入其原来所在位置后的部分链表中
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    // 将目标定时器 timer 从链表中删除
    void del_timer(util_timer* timer)
    {
        std::cout << "del_timer" << std::endl;
        if (!timer) {
            return;
        }
        // 下面这个条件成立表示链表中只有一个定时器，即目标定时器
        if ((timer == head) && (timer == tail)) {
            delete timer;
            head = nullptr;
            tail = nullptr;
            return;
        }
        /* 如果链表中至少有两个定时器，且目标定时器是链表的头节点，
         则将链表的头节点重置为原头节点的下一个节点，然后删除目标定时器。 */
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }
        /* 如果链表中至少有两个定时器，且目标定时器是链表的尾节点，
        则将链表的尾节点重置为原尾节点的前一个节点，然后删除目标定时器。*/
        if (timer == tail) {
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
            return;
        }
        // 如果目标定时器位于链表的中间，则把它前后的定时器串联起来，然后删除目标定时器
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    /* SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以处理链表上到期任务。*/
    void tick() {
        std::cout << "tick!!!" << std::endl;
        if (!head) {
            return;
        }
        std::cout << "timer tick" << std::endl;
        time_t cur = time(nullptr);  // 获取当前系统时间
        util_timer* tmp = head;
        // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
        while (tmp) {
            /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
            比较以判断定时器是否到期*/
            if (cur < tmp->expire) {
                break;
            }

            // 调用定时器的回调函数，以执行定时任务
            std::cout << "tick closeconn" << std::endl;
            tmp->userData->closeConn();
            // 执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头节点
            head = tmp->next;
            if (head) {
                head->prev = nullptr;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    /* 一个重载的辅助函数，它被公有的 add_timer 函数和 adjust_timer 函数调用
    该函数表示将目标定时器 timer 添加到节点 lst_head 之后的部分链表中 */
    void add_timer(util_timer* timer, util_timer* lst_head) {
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;
        /* 遍历 list_head 节点之后的部分链表，直到找到一个超时时间大于目标定时器的超时时间节点
        并将目标定时器插入该节点之前 */
        while (tmp) {
            if (timer->expire < tmp->expire) {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        /* 如果遍历完 lst_head 节点之后的部分链表，仍未找到超时时间大于目标定时器的超时时间的节点，
           则将目标定时器插入链表尾部，并把它设置为链表新的尾节点。*/
        if (!tmp) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = nullptr;
            tail = timer;
        }
    }

private:
    util_timer* head;   // 头结点
    util_timer* tail;   // 尾结点
};

#endif