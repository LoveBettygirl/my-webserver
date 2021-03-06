#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include "../common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include "../thread/locker.h"
#include "../utils/utils.h"
#include "../epoll/epoll.h"
#include <unordered_map>
#include "../mysql/mysql.h"
#include <algorithm>
#include <cctype>
#include <sys/wait.h>
#include "../buffer/buffer.h"
#include "url.h"
#include "cookie.h"
#include "../redis/redis.h"

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
        CGI_REQUEST: cgi请求成功
        INTERNAL_ERROR: 表示服务器内部错误
        CLOSED_CONNECTION: 表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, CGI_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

    // 定义HTTP响应的一些状态信息
    static const char *OK_200_TITLE;
    static const char *OK_200_FORM;
    static const char *ERROR_400_TITLE;
    static const char *ERROR_400_FORM;
    static const char *ERROR_403_TITLE;
    static const char *ERROR_403_FORM;
    static const char *ERROR_404_TITLE;
    static const char *ERROR_404_FORM;
    static const char *ERROR_500_TITLE;
    static const char *ERROR_500_FORM;

    // mimetype
    static const char *TYPE_BIN;
    static const unordered_map<string, string> SUFFIX_TYPE;

    static const int READ_BUFFER_SIZE = 65535;
    static const int WRITE_BUFFER_SIZE = 65535;
    static const int FILENAME_LENGTH = 200; // 文件名的最大长度
    static const int SESSION_EXPIRE = 3600; // session持续时长3600s
    static const int USER_INFO_EXPIRE = 7200; // session持续时长3600s

public:
    HttpConn();
    ~HttpConn();
    void process(); // 处理客户端的请求
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接收的连接
    void closeConn(); // 关闭连接
    bool read(); // 非阻塞的读
    bool write(); // 非阻塞的写
    void setMySQL(MYSQL *conn) { this->mysql.setConn(conn); }
    void setRedis(redisContext *conn) { this->redis.setConn(conn); }
    static void tick();
    static void setEpollfd(int fd);
    static int getUserCount();
    static void decUserCount();
    static void setDocRoot(const std::string &path);
    static void initMySQLResult();

private:
    int m_sockfd; // 该HTTP连接的socket
    sockaddr_in m_address; // 通信的socket地址
    int mReadIndex; // 标识读缓冲区中以及读入的客户端数据的最后一个字节的下标（下一次从这里开始读）

    int mCheckedIndex; // 当前正在分析的字符在读缓冲区的位置
    int mStartLine; // 当前正在解析的行的起始位置
    std::string mUrl; // 请求目标文件的文件名
    std::string mVersion; // 协议版本，只支持1.1
    METHOD mMethod; // 请求方法
    std::string mHost; // 主机名
    std::string mContentType;
    bool mLinger; // 是否保持连接（keep-alive）
    int mContentLength; // HTTP请求的消息总长度
    std::string mCookie;
    std::string currLine;

    Redis redis;
    MySQL mysql;

    CHECK_STATE mCheckState; // 主状态机当前所处的状态

    char mRealFile[FILENAME_LENGTH]; // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    int mWriteIndex; // 写缓冲区中待发送的字节数
    char *mFileAddress; // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat mFileStat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2]; // 要写的内存块，有两块：一块是空行前面的响应行（mWriteBuf），另一块是空行之后的文件缓冲区（mFileAddress）
    int m_iv_Count; // 被写内存块的数量
    Buffer readBuffer; // 可变的读缓冲区
    Buffer writeBuffer;
    Buffer cgiBuffer;

    int mBytesHaveSend; // 将要发送的数据的字节数
    int mBytesToSend; // 已经发送的字节数

    int cgi;
    std::string mQueryString;
    Locker mLock;

    std::string mMimeType;
    int mCgiLen;

    static int m_epollfd; // 所有的socket上的事件都被注册到同一个epoll对象中
    static std::string docRoot;
    static int mUserCount; // 统计用户的数量
    static std::unordered_map<std::string, std::string> mUsers;

private:
    void initInfos(); // 初始化连接的其余信息

    HTTP_CODE processRead(); // 解析HTTP请求，主状态机
    bool processWrite(HTTP_CODE ret);
    HTTP_CODE parseRequestLine(const std::string &text); // 解析请求首行
    HTTP_CODE parseHeaders(const std::string &text); // 解析请求头
    HTTP_CODE parseContent(const std::string &text); // 解析请求体

    bool addStatusLine(int status, const char *title); // 生成响应首行
    bool addResponse(const string &str); // 往缓冲区中写入待发送的数据
    void addHeaders(int contentLen); // 生成响应头
    bool addContentLength(int contentLen);
    bool addServerInfo();
    bool addContentType();
    bool addLinger();
    bool addBlankLine();
    bool addContent(const char *content);
    bool addCookie();
    bool addCgiContent();

    LINE_STATUS parseLine();

    HTTP_CODE doRequest();

    void unmap(); // 解除对文件的内存映射
};

#endif