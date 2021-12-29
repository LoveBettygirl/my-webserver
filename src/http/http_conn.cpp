#include "http_conn.h"

using namespace std;

int HttpConn::m_epollfd = -1;
int HttpConn::mUserCount = 0;

const char *HttpConn::OK_200_TITLE = "200 OK";
const char *HttpConn::OK_200_FORM = "<html><head><meta charset=\"utf-8\"><title>200 OK</title></head><body><h2>200 OK</h2><p>Request success.</p><hr><em>MyHTTPServer</em></body></html>";
const char *HttpConn::ERROR_400_TITLE = "400 Bad Request";
const char *HttpConn::ERROR_400_FORM = "<html><head><meta charset=\"utf-8\"><title>400 Bad Request</title></head><body><h2>400 Bad Request</h2><p>Your request has bad syntax or is inherently impossible to satisfy.</p><hr><em>MyHTTPServer</em></body></html>";
const char *HttpConn::ERROR_403_TITLE = "403 Forbidden";
const char *HttpConn::ERROR_403_FORM = "<html><head><meta charset=\"utf-8\"><title>403 Forbidden</title></head><body><h2>403 Forbidden</h2><p>You do not have permission to get file from this server.</p><hr><em>MyHTTPServer</em></body></html>";
const char *HttpConn::ERROR_404_TITLE = "404 Not Found";
const char *HttpConn::ERROR_404_FORM = "<html><head><meta charset=\"utf-8\"><title>404 Not Found</title></head><body><h2>404 Not Found</h2><p>The requested file was not found on this server.</p><hr><em>MyHTTPServer</em></body></html>";
const char *HttpConn::ERROR_500_TITLE = "500 Internal Error";
const char *HttpConn::ERROR_500_FORM = "<html><head><meta charset=\"utf-8\"><title>500 Internal Error</title></head><body><h2>500 Internal Error</h2><p>There was an unusual problem serving the requested file.</p><hr><em>MyHTTPServer</em></body></html>";

const char *HttpConn::TYPE_BIN = "application/octet-stream";

string HttpConn::docRoot = "./resources";

const unordered_map<string, string> HttpConn::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".htm",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".ico",   "image/x-icon" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".mp4",   "video/mp4" },
    { ".mpg4",   "video/mp4" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

std::unordered_map<std::string, std::string> HttpConn::mUsers;

HttpConn::HttpConn() {}

HttpConn::~HttpConn() {}

void HttpConn::initMySQLResult()
{
    ConnectionPool *connPool = ConnectionPool::getInstance();

    // 先从连接池中取一个连接
    auto mysqlRAII = connPool->getConnection();
    MYSQL *mysql = mysqlRAII.get();

    // 在user表中检索username，password数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username, password FROM user")) {
        LOG_ERROR("SELECT error: %s", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int numFields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        mUsers[temp1] = temp2;
    }
}

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
    // 读取到的字节
    int bytesRead = 0;
    while (true) {
        // bytesRead = recv(m_sockfd, mReadBuf + mReadIndex, READ_BUFFER_SIZE - mReadIndex, 0);
        int readErrno = 0;
        bytesRead = readBuffer.readFd(m_sockfd, &readErrno);
        if (bytesRead == -1) {
            if (readErrno == EAGAIN || readErrno == EWOULDBLOCK) {
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
    mysql = nullptr;
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
    mContentType = "";
    mMethod = GET;
    memset(mWriteBuf, 0, WRITE_BUFFER_SIZE);
    memset(mRealFile, 0, FILENAME_LENGTH);
    mLinger = false;
    cgi = 0;
    mMimeType = TYPE_BIN;
    memset(mCgiBuf, 0, READ_BUFFER_SIZE);
    mCgiLen = 0;
    mFileAddress = nullptr;
}

void HttpConn::unmap()
{
    if (mFileAddress) {
        munmap(mFileAddress, mFileStat.st_size);
        mFileAddress = nullptr;
    }
}

HttpConn::HTTP_CODE HttpConn::doRequest() {
    // 已获得完整请求，要去执行这个请求
    // 从请求首行和根目录，得到要找的资源的路径
    // string filename = docRoot + mUrl;
    string filename = docRoot;
    strcpy(mRealFile, filename.c_str());
    int len = filename.size();
    int index = mUrl.rfind("/");

    // 处理cgi
    if (cgi) {
        // 登录校验
        if (mUrl[index + 1] == '2' || mUrl[index + 1] == '3') {
            // 将用户名和密码提取出来
            // user=123&password=123
            char name[100] = {0}, password[100] = {0};
            int i;
            for (i = 5; mQueryString[i] != '&'; ++i)
                name[i - 5] = mQueryString[i];
            name[i - 5] = '\0';

            int j = 0;
            for (i = i + 10; i < mQueryString.size(); ++i, ++j)
                password[j] = mQueryString[i];
            password[j] = '\0';

            if (mUrl[index + 1] == '3') {
                // 如果是注册，先检测数据库中是否有重名的
                // 没有重名的，进行增加数据
                char sqlInsert[200] = {0};
                sprintf(sqlInsert, "INSERT INTO user(username, password) VALUES('%s', '%s')", name, password);

                if (mUsers.find(name) == mUsers.end()) {
                    mLock.lock();
                    int res = mysql_query(mysql, sqlInsert);
                    mUsers.insert(pair<string, string>(name, password));
                    mLock.unlock();

                    if (!res)
                        mUrl = "/log.html";
                    else
                        mUrl = "/registerError.html";
                }
                else
                    mUrl = "/registerError.html";
            }
            // 如果是登录，直接判断
            // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
            else if (mUrl[index + 1] == '2') {
                if (mUsers.find(name) != mUsers.end() && mUsers[name] == password)
                    mUrl = "/welcome.html";
                else
                    mUrl = "/logError.html";
            }
            strncpy(mRealFile + len, mUrl.c_str(), FILENAME_LENGTH - len - 1);
        }
        else if (mUrl[index + 1] == '0') {
            string urlReal = "/register.html";
            strncpy(mRealFile + len, urlReal.c_str(), urlReal.size());
        }
        else if (mUrl[index + 1] == '1') {
            string urlReal = "/log.html";
            strncpy(mRealFile + len, urlReal.c_str(), urlReal.size());
        }
        else if (mUrl[index + 1] == '5') {
            string urlReal = "/picture.html";
            strncpy(mRealFile + len, urlReal.c_str(), urlReal.size());
        }
        else if (mUrl[index + 1] == '6') {
            string urlReal = "/video.html";
            strncpy(mRealFile + len, urlReal.c_str(), urlReal.size());
        }
        else if (mUrl[index + 1] == '7') {
            string urlReal = "/fans.html";
            strncpy(mRealFile + len, urlReal.c_str(), urlReal.size());
        }
        else { // 动态解析cgi
            strncpy(mRealFile + len, mUrl.c_str(), FILENAME_LENGTH - len - 1);
            len = strlen(mRealFile);

            // 先看看有没有对应的文件
            if (stat(mRealFile, &mFileStat) < 0) {
                return NO_RESOURCE; // 没有这个文件
            }

            // 判断访问权限
            if (!(mFileStat.st_mode & S_IROTH)) {
                return FORBIDDEN_REQUEST; // 没有访问权限
            }

            // 判断是否是目录
            if (S_ISDIR(mFileStat.st_mode)) {
                mUrl += "/index.html";
                strncpy(mRealFile + len, "/index.html", FILENAME_LENGTH - len - 1);

                // 先看看有没有对应的文件
                if (stat(mRealFile, &mFileStat) < 0) {
                    return NO_RESOURCE; // 没有这个文件
                }

                // 判断访问权限
                if (!(mFileStat.st_mode & S_IROTH)) {
                    return FORBIDDEN_REQUEST; // 没有访问权限
                }
            }

            // 判断是否可执行
            if (!(mFileStat.st_mode & S_IXUSR) &&
                !(mFileStat.st_mode & S_IXGRP) &&
                !(mFileStat.st_mode & S_IXOTH)) {
                if (mMethod == POST) {
                    return BAD_REQUEST;
                }
                cgi = 0;
                // 否则，是GET请求，改成静态请求处理
                // 以只读方式打开文件
                int fd = open(mRealFile, O_RDONLY);
                // 创建内存映射
                mFileAddress = (char*)mmap(0, mFileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // 要发送的资源
                close(fd);
                return FILE_REQUEST;
            }

            // fd[0]: 读管道，fd[1]:写管道
            pid_t pid;
            int cgiOutput[2];
            int cgiInput[2];
            if (pipe(cgiOutput) < 0) {
                perror("pipe");
                LOG_ERROR("%s", "Create pipe failed.");
                return INTERNAL_ERROR;
            }
            if (pipe(cgiInput) < 0) {
                perror("pipe");
                LOG_ERROR("%s", "Create pipe failed.");
                return INTERNAL_ERROR;
            }
            if ((pid = fork()) < 0) {
                perror("fork");
                LOG_ERROR("%s", "Fork failed.");
                return INTERNAL_ERROR;
            }
            if (pid == 0) {
                char methEnv[255] = {0};
                char queryEnv[255] = {0};
                char lengthEnv[255] = {0};
                char typeEnv[255] = {0};

                dup2(cgiOutput[1], 1);
                dup2(cgiInput[0], 0);

                close(cgiOutput[0]); // 关闭了cgiOutput中的读通道
                close(cgiInput[1]); // 关闭了cgiInput中的写通道

                if (mMethod == GET) {
                    strcpy(methEnv, "REQUEST_METHOD=GET");
                    putenv(methEnv);
                    // 存储QUERY_STRING
                    sprintf(queryEnv, "QUERY_STRING=%s", mQueryString.c_str());
                    putenv(queryEnv);
                }
                else if (mMethod == POST) {
                    strcpy(methEnv, "REQUEST_METHOD=POST");
                    putenv(methEnv);
                    // 存储CONTENT_LENGTH
                    sprintf(lengthEnv, "CONTENT_LENGTH=%d", mContentLength);
                    putenv(lengthEnv);
                    // 存储CONTENT_TYPE
                    sprintf(typeEnv, "CONTENT_TYPE=%s", mContentType.c_str());
                    putenv(typeEnv);
                }

                execl(mRealFile, mRealFile, nullptr);
            }
            else
            {
                // 父进程关闭写端，打开读端，读取子进程的输出
                close(cgiOutput[1]);
                close(cgiInput[0]);
                int ret = 0;
                if (mMethod == POST) {
                    // 向cgi程序标准输入中写入querystring
                    ret = ::write(cgiInput[1], mQueryString.c_str(), mContentLength);
                    if (ret < 0) {
                        perror("write");
                        LOG_ERROR("%s", "Write to pipe failed.");
                        return INTERNAL_ERROR;
                    }
                }

                // 读取cgi脚本返回数据
                char readBuf[READ_BUFFER_SIZE] = {0};
                ret = ::read(cgiOutput[0], readBuf, sizeof(readBuf));
                if (ret <= 0) {
                    perror("read");
                    LOG_ERROR("%s", "Read from pipe failed.");
                    return INTERNAL_ERROR;
                }
                // TODO: 严格的来说，这里还需要再解析可能出现的响应头信息，这里只处理Content-Type
                if (strncasecmp(readBuf, "Content-Type:", 13) == 0) {
                    int i = 13;
                    while (readBuf[i] == ' ' && readBuf[i] != '\0')
                        i++;
                    mMimeType.clear();
                    while (readBuf[i] != '\n' && readBuf[i] != '\0')
                        mMimeType.push_back(readBuf[i++]);
                    if (readBuf[i] == '\n')
                        i += 2;
                    mCgiLen = ret - i;
                    memcpy(mCgiBuf, readBuf + i, mCgiLen);
                }
                else {
                    mCgiLen = ret;
                    memcpy(mCgiBuf, readBuf, mCgiLen);
                }

                // 运行结束关闭
                close(cgiOutput[0]);
                close(cgiInput[1]);

                // 回收子进程资源
                waitpid(pid, nullptr, 0);

                return CGI_REQUEST;
            }
        }
    }
    else {
        strncpy(mRealFile + len, mUrl.c_str(), FILENAME_LENGTH - len - 1);
        len = strlen(mRealFile);
    }

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
        mUrl += "/index.html";
        strncpy(mRealFile + len, "/index.html", FILENAME_LENGTH - len - 1);

        // 先看看有没有对应的文件
        if (stat(mRealFile, &mFileStat) < 0) {
            return NO_RESOURCE; // 没有这个文件
        }

        // 判断访问权限
        if (!(mFileStat.st_mode & S_IROTH)) {
            return FORBIDDEN_REQUEST; // 没有访问权限
        }
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
    lineStatus = parseLine(); // 规避短路规则
    while (lineStatus == LINE_OK) {
        // 获取一行数据
        std::cout << "got 1 http line: " << currLine << std::endl;

        switch (mCheckState) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(currLine);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(currLine);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) { // 已经获得了完整的请求
                    currLine.clear();
                    return doRequest(); // 解析具体的请求信息
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(currLine);
                if (ret == GET_REQUEST) { // 已经获得了完整的请求
                    currLine.clear();
                    return doRequest(); // 解析具体的请求信息
                }
                lineStatus = LINE_OPEN; // 失败，请求数据不完整
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
        if (mCheckState != CHECK_STATE_CONTENT)
            currLine.clear();
        lineStatus = parseLine();
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
    return addResponse("Content-Type: %s\r\n", mMimeType.c_str());
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
    mQueryString.clear();
    switch (ret) {
        case INTERNAL_ERROR:
            mMimeType = SUFFIX_TYPE.find(".html")->second;
            addStatusLine(500, ERROR_500_TITLE);
            addHeaders(strlen(ERROR_500_FORM));
            if (!addContent(ERROR_500_FORM)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            mMimeType = SUFFIX_TYPE.find(".html")->second;
            addStatusLine(400, ERROR_400_TITLE);
            addHeaders(strlen(ERROR_400_FORM));
            if (!addContent(ERROR_400_FORM)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            mMimeType = SUFFIX_TYPE.find(".html")->second;
            addStatusLine(404, ERROR_404_TITLE);
            addHeaders(strlen(ERROR_404_FORM));
            if (!addContent(ERROR_404_FORM)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            mMimeType = SUFFIX_TYPE.find(".html")->second;
            addStatusLine(403, ERROR_403_TITLE);
            addHeaders(strlen(ERROR_403_FORM));
            if (!addContent(ERROR_403_FORM)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            addStatusLine(200, OK_200_TITLE);
            if (mFileStat.st_size != 0) {
                addHeaders(mFileStat.st_size);
                m_iv[0].iov_base = mWriteBuf;
                m_iv[0].iov_len = mWriteIndex;
                m_iv[1].iov_base = mFileAddress;
                m_iv[1].iov_len = mFileStat.st_size;
                m_iv_Count = 2;
                mBytesToSend = mWriteIndex + mFileStat.st_size; // 响应头的大小+文件的大小
                return true;
            }
            else {
                mMimeType = SUFFIX_TYPE.find(".html")->second;
                addHeaders(strlen(OK_200_FORM));
                if (!addContent(OK_200_FORM)) {
                    return false;
                }
                break;
            }
        case CGI_REQUEST:
            addStatusLine(200, OK_200_TITLE);
            addHeaders(mCgiLen);
            if (!addContent(mCgiBuf))
                return false;
            break;
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
HttpConn::HTTP_CODE HttpConn::parseRequestLine(const std::string &text)
{
    // string str(text);
    // std::cout << "parseRequestLine: " << text << std::endl;
    // TODO: 用正则表达式会简单一些
    // 获取请求方法
    istringstream is(text);
    string temp;
    int i = 0;
    while (is >> temp) {
        if (i == 0) { // 请求方法
            if (temp == "GET") {
                mMethod = GET;
            } else if (temp == "POST") {
                mMethod = POST;
                cgi = 1;
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
            size_t queryIndex = mUrl.find("?");
            if (queryIndex != string::npos) { // url上有参数
                mQueryString = mUrl.substr(queryIndex + 1);
                mUrl = mUrl.substr(0, queryIndex);
                cgi = 1;
            }
            if (mUrl.empty()) {
                mUrl = "/index.html";
            }
            if (mUrl.back() == '/') {
                mUrl += "index.html";
            }
            // 解析mimetype
            string type = mUrl.substr(mUrl.rfind("."));
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            auto it = SUFFIX_TYPE.find(type);
            if (it != SUFFIX_TYPE.end()) {
                mMimeType = it->second;
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

HttpConn::HTTP_CODE HttpConn::parseHeaders(const std::string &text)
{
    // 遇到空行，表示头部字段解析完毕
    if(text.empty()) {
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
    string temp, key, value;
    size_t index = str.find(":");
    if (index == string::npos) {
        std::cout << "oop! unknown header " << text << std::endl;
        return NO_REQUEST;
    }
    index++;
    key = str.substr(0, index);
    while (index != str.size() && (str[index] == ' ' || str[index] == '\t')) {
        index++;
    }
    value = str.substr(index);

    if (key == "Connection:") {
        // 处理Connection 头部字段  Connection: keep-alive
        if (value == "keep-alive") {
            mLinger = true; // 保持连接
        }
    } else if (key == "Content-Length:") {
        // 处理Content-Length头部字段
        mContentLength = stol(value);
    } else if (key == "Content-Type:") {
        // 处理Content-Type头部字段
        mContentType = value;
    } else if (key == "Host:") {
        // 处理Host头部字段
        mHost = value;
    } else {
        std::cout << "oop! unknown header " << text << std::endl;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parseContent(const std::string &text)
{
    // 数据是否被完整读入
    if (text.size() >= mContentLength) {
        mQueryString.append(text, 0, mContentLength);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析一行，判断依据\r\n
HttpConn::LINE_STATUS HttpConn::parseLine() {
    if (mCheckState == CHECK_STATE_CONTENT) {
        size_t restSize = mContentLength - currLine.size();
        if (readBuffer.readableBytes() >= restSize) {
            currLine.append(readBuffer.peek(), restSize);
            readBuffer.retrieve(restSize);
        }
        else {
            currLine.append(readBuffer.peek(), readBuffer.readableBytes());
            readBuffer.retrieveAll();
        }
        if (currLine.size() == mContentLength) {
            return LINE_OK;
        }
        return LINE_OPEN;
    }
    char temp;
    // 遍历一行数据
    const char *p = readBuffer.peek();
    for (; p != readBuffer.beginWriteConst(); ++p) {
        temp = *p;
        if (mCheckState == CHECK_STATE_CONTENT) {
            currLine.push_back(temp);
            continue;
        }
        if (temp == '\r') {
            if (p + 1 == readBuffer.beginWriteConst()) {
                readBuffer.retrieveUntil(p + 1);
                return LINE_OPEN; // 这一行不完整
            } else if (*(p + 1) == '\n') {
                // 将\r\n消除掉
                readBuffer.retrieveUntil(p + 2);
                return LINE_OK; // 完整解析到了一行
            }
            readBuffer.retrieveUntil(p);
            return LINE_BAD;
        } else if (temp == '\n') {
            // 判断前面一个字符是不是\r
            if ((p != readBuffer.peek()) && (*(p - 1) == '\r')) {
                // 将\r\n消除掉
                readBuffer.retrieveUntil(p + 1);
                return LINE_OK; // 完整解析到了一行
            }
            readBuffer.retrieveUntil(p);
            return LINE_BAD;
        }
        currLine.push_back(temp);
    }
    readBuffer.retrieveAll();
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