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
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET; // TODO: 是否使用ET可以作为程序的选项

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

    initInfos();
}

void HttpConn::closeConn() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        mUserCount--; // 关闭一个连接，客户总数量-1
    }
}

// 循环读取用户数据，直到无数据可读或者对方关闭连接
bool HttpConn::read() {
    // 缓冲区已满
    if (mReadIndex >= READ_BUFFER_SIZE) {
        return false;
    }
    // 读取到的字节
    int bytesRead = 0;
    while (true) {
        bytesRead = recv(m_sockfd, mReadBuf + mReadIndex, READ_BUFFER_SIZE - mReadIndex, 0);
        if (bytesRead == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据
                break;
            }
            return false;
        } else if (bytesRead == 0) {
            // 对方关闭连接
            return false;
        }
        mReadIndex += bytesRead;
    }
    std::cout << "读取到的数据：" << mReadBuf << std::endl;
    return true;
}

bool HttpConn::write() {
    std::cout << "write" << std::endl;
    return true;
}

void HttpConn::initInfos() {
    mCheckState = CHECK_STATE_REQUESTLINE; // 初始化状态为解析请求首行
    mCheckedIndex = 0;
    mStartLine = 0;
    mReadIndex = 0;
    mUrl = nullptr;
    mVersion = nullptr;
    mMethod = GET;
    memset(mReadBuf, 0, READ_BUFFER_SIZE);
    mLinger = false;
}

HttpConn::HTTP_CODE HttpConn::doRequest() {

}

HttpConn::HTTP_CODE HttpConn::processRead() {
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = nullptr;

    // 解析到了请求体也是完整的数据
    // 或者解析到了一行完整的数据
    while (((mCheckState == CHECK_STATE_CONTENT) && (lineStatus == LINE_OK))
        || ((lineStatus = parseLine()) == LINE_OK)) {
        // 获取一行数据
        text = getLine();
        mStartLine = mCheckedIndex;
        std::cout << "got 1 http line: " << text << std::endl;

        switch (mCheckState) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parseRequestLine(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parseHeaders(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) { // 已经获得了完整的请求
                    return doRequest(); // 解析具体的请求信息
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parseContent(text);
                if (ret == GET_REQUEST) { // 已经获得了完整的请求
                    return doRequest(); // 解析具体的请求信息
                }
                lineStatus = LINE_OPEN; // 失败，请求数据不完整
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 解析HTTP请求行，获得请求方法，目标URL，HTTP版本
HttpConn::HTTP_CODE HttpConn::parseRequestLine(char *text) {
    // TODO: 用正则表达式会简单一些
    // 获取请求方法
    mUrl = strpbrk(text, " \t");
    if (!mUrl) {
        return BAD_REQUEST;
    }
    *mUrl++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        mMethod = GET;
    } else {
        return BAD_REQUEST;
    }

    // 获取版本
    mVersion = strpbrk(mUrl, " \t");
    if (!mVersion) {
        return BAD_REQUEST;
    }
    *mVersion++ = '\0';
    if (strcasecmp(mVersion, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // 解析资源
    if (strncasecmp(mUrl, "http://", 7) == 0) {
        mUrl += 7;
        mUrl = strchr(mUrl, '/'); // find()
    }

    if (!mUrl || mUrl[0] != '/') {
        return BAD_REQUEST;
    }

    mCheckState = CHECK_STATE_HEADER; // 主状态机：检查状态变成检查请求头
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parseHeaders(char *text) {

}

HttpConn::HTTP_CODE HttpConn::parseContent(char *text) {

}

// 解析一行，判断依据\r\n
HttpConn::LINE_STATUS HttpConn::parseLine() {
    char temp;
    // 遍历一行数据
    for (; mCheckedIndex < mReadIndex; ++mCheckedIndex) {
        temp = mReadBuf[mCheckedIndex];
        if (temp == '\r') {
            if (mCheckedIndex + 1 == mReadIndex) {
                return LINE_OPEN; // 这一行不完整
            } else if (mReadBuf[mCheckedIndex + 1] == '\n') {
                // 将\r\n消除掉
                mReadBuf[mCheckedIndex++] = '\0';
                mReadBuf[mCheckedIndex++] = '\0';
                return LINE_OK; // 完整解析到了一行
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            // 判断前面一个字符是不是\r
            if ((mCheckedIndex > 1) && (mReadBuf[mCheckedIndex - 1] == '\r')) {
                // 将\r\n消除掉
                mReadBuf[mCheckedIndex - 1] = '\0';
                mReadBuf[mCheckedIndex++] = '\0';
                return LINE_OK; // 完整解析到了一行
            }
            return LINE_BAD;
        }
        return LINE_OPEN;
    }
    return LINE_OK;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConn::process() {
    // 解析HTPP请求
    HTTP_CODE readRet = processRead(); // 解析一些请求有不同的情况
    if (readRet == NO_REQUEST) { // 请求不完整，要继续获取客户端数据
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    std::cout << "parse request, create response" << std::endl;
    // 生成响应
}