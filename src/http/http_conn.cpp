#include "http_conn.h"

using namespace std;

int HttpConn::m_epollfd = -1;
int HttpConn::mUserCount = 0;

const char *HttpConn::OK_200_TITLE = "OK";
const char *HttpConn::ERROR_400_TITLE = "Bad Request";
const char *HttpConn::ERROR_400_FORM = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *HttpConn::ERROR_403_TITLE = "Forbidden";
const char *HttpConn::ERROR_403_FORM = "You do not have permission to get file from this server.\n";
const char *HttpConn::ERROR_404_TITLE = "Not Found";
const char *HttpConn::ERROR_404_FORM = "The requested file was not found on this server.\n";
const char *HttpConn::ERROR_500_TITLE = "Internal Error";
const char *HttpConn::ERROR_500_FORM = "There was an unusual problem serving the requested file.\n";

string HttpConn::docRoot = "./resources";

HttpConn::HttpConn() {}

HttpConn::~HttpConn() {}

void HttpConn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    // 设置端口复用
    // 端口复用一定要在绑定前设置
    int reuse = 1;
    if (setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt");
        exit(SET_REUSE_PORT_ERROR);
    }

    // 添加到epoll对象中
    addfd(m_epollfd, sockfd, true, true);
    mUserCount++; // 总用户数+1

    initInfos();
}

void HttpConn::closeConn()
{
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        mUserCount--; // 关闭一个连接，客户总数量-1
    }
}

// 循环读取用户数据，直到无数据可读或者对方关闭连接
bool HttpConn::read()
{
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
    return true;
}

bool HttpConn::write()
{
    int temp = 0;
    
    if (mBytesToSend == 0) {
        // 将要发送的字节为0，这一次响应结束。
        modifyfd(m_epollfd, m_sockfd, EPOLLIN); 
        initInfos();
        return true;
    }

    while (true) {
        // 分散写
        // 有多块内存的数据要写，一起写出去
        temp = writev(m_sockfd, m_iv, m_iv_Count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN) {
                modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        mBytesToSend -= temp;
        mBytesHaveSend += temp;

        if (mBytesHaveSend >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = mFileAddress + (mBytesHaveSend - mWriteIndex);
            m_iv[1].iov_len = mBytesToSend;
        }
        else {
            m_iv[0].iov_base = mWriteBuf + mBytesHaveSend;
            // m_iv[0].iov_len = m_iv[0].iov_len - temp;
            m_iv[0].iov_len = m_iv[0].iov_len - mBytesHaveSend;
        }

        if (mBytesToSend <= 0) {
            // 没有数据要发送了
            unmap();
            modifyfd(m_epollfd, m_sockfd, EPOLLIN);

            if (mLinger) {
                initInfos();
                return true;
            }
            else {
                return false;
            }
        }
    }
}

void HttpConn::initInfos()
{
    mBytesHaveSend = 0;
    mBytesToSend = 0;
    mCheckState = CHECK_STATE_REQUESTLINE; // 初始化状态为解析请求首行
    mCheckedIndex = 0;
    mStartLine = 0;
    mReadIndex = 0;
    mWriteIndex = 0;
    mUrl = "";
    mVersion = "";
    mContentLength = 0;
    mHost = "";
    mMethod = GET;
    memset(mReadBuf, 0, READ_BUFFER_SIZE);
    memset(mWriteBuf, 0, WRITE_BUFFER_SIZE);
    memset(mRealFile, 0, FILENAME_LENGTH);
    mLinger = false;
}

void HttpConn::unmap()
{
    if (mFileAddress) {
        munmap(mFileAddress, mFileStat.st_size);
        mFileAddress = 0;
    }
}

HttpConn::HTTP_CODE HttpConn::doRequest() {
    // 已获得完整请求，要去执行这个请求
    // 从请求首行和根目录，得到要找的资源的路径
    string filename = docRoot + mUrl;
    strcpy(mRealFile, filename.c_str());
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(mRealFile, &mFileStat) < 0) {
        return NO_RESOURCE; // 没有这个文件
    }

    // 判断访问权限
    if (!(mFileStat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST; // 没有访问权限
    }

    // 判断是否是目录
    if (S_ISDIR(mFileStat.st_mode)) {
        return BAD_REQUEST; // 不能访问目录
    }

    // 以只读方式打开文件
    int fd = open(mRealFile, O_RDONLY);
    // 创建内存映射
    mFileAddress = (char*)mmap(0, mFileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // 要发送的资源
    close(fd);
    return FILE_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::processRead()
{
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    // char *text = nullptr;

    // 解析到了请求体也是完整的数据
    // 或者解析到了一行完整的数据
    while (((mCheckState == CHECK_STATE_CONTENT) && (lineStatus == LINE_OK))
        || ((lineStatus = parseLine()) == LINE_OK)) {
        // 获取一行数据
        char *text = getLine();
        mStartLine = mCheckedIndex;
        std::cout << "got 1 http line: " << text << std::endl;

        switch (mCheckState) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) { // 已经获得了完整的请求
                    return doRequest(); // 解析具体的请求信息
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if (ret == GET_REQUEST) { // 已经获得了完整的请求
                    return doRequest(); // 解析具体的请求信息
                }
                lineStatus = LINE_OPEN; // 失败，请求数据不完整
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

bool HttpConn::addResponse(const char *format, ...)
{
    // 缓冲区写满了
    if(mWriteIndex >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list argList;
    va_start(argList, format);
    // 从哪开始写入发送数据
    int len = vsnprintf(mWriteBuf + mWriteIndex, WRITE_BUFFER_SIZE - 1 - mWriteIndex, format, argList);
    if (len >= (WRITE_BUFFER_SIZE - 1 - mWriteIndex)) {
        va_end(argList);
        return false;
    }
    mWriteIndex += len;
    va_end(argList);
    return true;
}

bool HttpConn::addStatusLine(int status, const char *title)
{
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::addContentLength(int contentLen)
{
    return addResponse("Content-Length: %d\r\n", contentLen);
}

bool HttpConn::addContentType()
{
    return addResponse("Content-Type: %s\r\n", "text/html");
}

bool HttpConn::addServerInfo()
{
    return addResponse("Server: %s\r\n", "MyHTTPServer/1.0");
}

bool HttpConn::addLinger()
{
    return addResponse("Connection: %s\r\n", (mLinger == true) ? "keep-alive" : "close");
}

bool HttpConn::addBlankLine()
{
    return addResponse("%s", "\r\n");
}

bool HttpConn::addContent(const char *content)
{
    return addResponse("%s", content);
}

void HttpConn::addHeaders(int contentLen)
{
    addContentLength(contentLen);
    addContentType();
    addLinger();
    addServerInfo();
    addBlankLine();
}

bool HttpConn::processWrite(HTTP_CODE ret)
{
    switch (ret) {
        case INTERNAL_ERROR:
            addStatusLine(500, ERROR_500_TITLE);
            addHeaders(strlen(ERROR_500_FORM));
            if (!addContent(ERROR_500_FORM)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            addStatusLine(400, ERROR_400_TITLE);
            addHeaders(strlen(ERROR_400_FORM));
            if (!addContent(ERROR_400_FORM)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            addStatusLine(404, ERROR_404_TITLE);
            addHeaders(strlen(ERROR_404_FORM));
            if (!addContent(ERROR_404_FORM)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            addStatusLine(403, ERROR_403_TITLE);
            addHeaders(strlen(ERROR_403_FORM));
            if (!addContent(ERROR_403_FORM)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            addStatusLine(200, OK_200_TITLE);
            addHeaders(mFileStat.st_size);
            m_iv[0].iov_base = mWriteBuf;
            m_iv[0].iov_len = mWriteIndex;
            m_iv[1].iov_base = mFileAddress;
            m_iv[1].iov_len = mFileStat.st_size;
            m_iv_Count = 2;
            mBytesToSend = mWriteIndex + mFileStat.st_size; // 响应头的大小+文件的大小
            return true;
        default:
            return false;
    }

    m_iv[0].iov_base = mWriteBuf;
    m_iv[0].iov_len = mWriteIndex;
    m_iv_Count = 1;
    mBytesToSend = mWriteIndex;
    return true;
}

// 解析HTTP请求行，获得请求方法，目标URL，HTTP版本
HttpConn::HTTP_CODE HttpConn::parseRequestLine(char *text)
{
    string str(text);
    // std::cout << "parseRequestLine: " << text << std::endl;
    // TODO: 用正则表达式会简单一些
    // 获取请求方法
    istringstream is(str);
    string temp;
    int i = 0;
    while (is >> temp) {
        if (i == 0) { // 请求方法
            if (temp == "GET") {
                mMethod = GET;
            } else {
                return BAD_REQUEST;
            }
        } else if (i == 1) { // url
            mUrl = temp;
            size_t index = mUrl.find("http://");
            if (index != string::npos) {
                if (index != 0)
                    return BAD_REQUEST;
                mUrl = mUrl.substr(mUrl.find("/", index));
            }
        } else if (i == 2) { // version
            if (temp != "HTTP/1.1") {
                return BAD_REQUEST;
            }
            mVersion = temp;
        }
        i++;
    }

    if (i != 3) {
        return BAD_REQUEST;
    }

    mCheckState = CHECK_STATE_HEADER; // 主状态机：检查状态变成检查请求头
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parseHeaders(char *text)
{
    // 遇到空行，表示头部字段解析完毕
    if(text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (mContentLength != 0) { // 说明有请求体
            mCheckState = CHECK_STATE_CONTENT;
            return NO_REQUEST; // 还没开始解析请求体
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    string str(text);
    istringstream is(str);
    string temp, key, value;
    int i = 0;
    while (is >> temp) {
        if (i == 0) {
            key = temp;
        } else if (i == 1) {
            value = temp;
        }
        i++;
    }

    if (i != 2) {
        std::cout << "oop! unknown header " << text << std::endl;
        return NO_REQUEST;
    }
    
    if (key == "Connection:") {
        // 处理Connection 头部字段  Connection: keep-alive
        if (value == "keep-alive") {
            mLinger = true; // 保持连接
        }
    } else if (key == "Content-Length:") {
        // 处理Content-Length头部字段
        mContentLength = stol(value);
    } else if (key == "Host:") {
        // 处理Host头部字段
        mHost = value;
    } else {
        std::cout << "oop! unknown header " << text << std::endl;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parseContent(char *text)
{
    // 数据是否被完整读入
    if (mReadIndex >= (mContentLength + mCheckedIndex)) {
        text[mContentLength] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
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
    }
    return LINE_OPEN;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConn::process()
{
    // 解析HTPP请求
    HTTP_CODE readRet = processRead(); // 解析一些请求有不同的情况
    if (readRet == NO_REQUEST) { // 请求不完整，要继续获取客户端数据
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    // std::cout << "parse request, create response" << std::endl;
    // 生成响应
    bool writeRet = processWrite(readRet);
    if (!writeRet) {
        closeConn();
    }
    modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
}

void HttpConn::setEpollfd(int fd)
{
    m_epollfd = fd;
}

int HttpConn::getUserCount()
{
    return mUserCount;
}

void HttpConn::decUserCount()
{
    mUserCount--;
}

void HttpConn::setDocRoot(const string &path)
{
    docRoot = path;
}