#include "Server.h"

using namespace std;

int WebServer::pipefd[2] = {};
TimerHeap WebServer::timerHeap;

WebServer::WebServer(const Config &config):
    epollfd(0),
    listenfd(0),
    port(config.port),
    mCloseLog(config.closeLog),
    mDaemonProcess(config.daemonProcess),
    mThreadPoolSize(config.threadPool),
    mConnectionPoolSize(config.connectionPool),
    mMySQLUser(config.mysqlUser),
    mMySQLPassword(config.mysqlPassword),
    mMySQLDatabaseName(config.mysqlDatabase),
    mMySQLIP(config.mysqlIP),
    mMySQLPort(config.mysqlPort),
    mRedisIP(config.redisIP),
    mRedisPort(config.redisPort)
{
    if (config.docRoot != "")
        HttpConn::setDocRoot(config.docRoot);
}

WebServer::~WebServer()
{
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
}

void WebServer::sigHandler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void WebServer::timerHandler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timerHeap.tick();
    // 因为一次 alarm 调用只会引起一次SIGALRM 信号，所以我们要重新定时，以不断触发 SIGALRM信号。
    alarm(TIMESLOT);
}

void WebServer::closeConn(int sockfd)
{
    usersConn[sockfd]->closeConn();
}

void WebServer::addClientInfo(int connfd, struct sockaddr_in client_address)
{
    usersConn[connfd] = make_shared<HttpConn>();
    usersConn[connfd]->init(connfd, client_address);

    timerHeap.addTimer(connfd, client_address, time(nullptr) + 3 * TIMESLOT, bind(&WebServer::closeConn, this, connfd));
}

bool WebServer::doClientData()
{
    sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);
    int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);
    if (connfd == -1) {
        perror("accept");
        LOG_ERROR("%s", "accept failed.");
        return false;
    }

    char ip[16] = {0};
    inet_ntop(AF_INET, &client_address.sin_addr ,ip, sizeof(ip));
    int port = ntohs(client_address.sin_port);

    if (HttpConn::getUserCount() >= MAX_FD) {
        // 目前连接数满了
        // 服务器内部正忙
        close(connfd);
        LOG_ERROR("%s", "Cannot establish with %s", ip);
        return false;
    }

    LOG_INFO("client(%s:%d) is connected", ip, port);

    // 将新的客户端数据初始化，放到数组中
    addClientInfo(connfd, client_address);
    return true;
}

bool WebServer::doSignal(bool &timeout, bool &stopServer)
{
    char signals[1024];
    int ret = recv(pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1) {
        return false;
    } else if (ret == 0) {
        return false;
    } else {
        for (int i = 0; i < ret; ++i) {
            switch (signals[i])  {
                case SIGALRM:
                {
                    // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                    // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                    timeout = true;
                    break;
                }
                case SIGTERM:
                case SIGINT:
                {
                    stopServer = true;
                    break;
                }
            }
        }
    }
    return true;
}

void WebServer::doTimer(int sockfd)
{
    timerHeap.doTimer(sockfd);
}

void WebServer::adjustTimer(int sockfd, time_t expire)
{
    timerHeap.adjustTimer(sockfd, expire);
}

void WebServer::doRead(int sockfd)
{
    if (usersConn[sockfd]->read()) {
        // 一次性把所有数据都读完
        pool->append(usersConn[sockfd]);
        adjustTimer(sockfd, time(nullptr) + 3 * TIMESLOT);
    } else {
        doTimer(sockfd);
    }
}

void WebServer::doWrite(int sockfd)
{
    // 一次性把所有数据都写完
    if (usersConn[sockfd]->write()) {
        adjustTimer(sockfd, time(nullptr) + 3 * TIMESLOT);
    }
    else {
        doTimer(sockfd);
    }
}

void WebServer::eventLoop()
{
    bool timeout = false; // 定时器超时，执行定时任务（关闭非活跃连接）
    bool stopServer = false;
    while (!stopServer) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        if (num < 0 && errno != EINTR) {
            LOG_ERROR("%s", "Epoll failed.");
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                // 有客户端连接进来
                if (!doClientData())
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常端口或者错误等事件
                // 关闭连接
                doTimer(sockfd);
            }
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 说明有信号到来，要处理信号
                doSignal(timeout, stopServer);
            } 
            else if (events[i].events & EPOLLIN) {
                doRead(sockfd);
            }
            else if (events[i].events & EPOLLOUT) {
                doWrite(sockfd);
            }
        }

        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if (timeout) {
            timerHandler();
            timeout = false;
        }
    }
}

void WebServer::logWrite()
{
    // 初始化日志
    Log::getInstance()->init("./logs/ServerLog", mCloseLog, 2000, 800000, 0);
}

void WebServer::threadPool()
{
    // 创建线程池，初始化线程池
    try {
        pool = shared_ptr<ThreadPool<HttpConn>>(new ThreadPool<HttpConn>(mThreadPoolSize));
    } catch (...) {
        LOG_ERROR("%s", "Create thread pool failed.");
        exit(CREATE_THREAD_POOL_ERROR);
    }
}

void WebServer::connectionPool()
{
    // 初始化数据库连接池
    MySQLConnectionPool *mysqlConnPool = MySQLConnectionPool::getInstance();
    mysqlConnPool->init(mMySQLIP, mMySQLUser, mMySQLPassword, mMySQLDatabaseName, mMySQLPort, mConnectionPoolSize);

    RedisConnectionPool *redisConnPool = RedisConnectionPool::getInstance();
    redisConnPool->init(mRedisIP, mRedisPort, mConnectionPoolSize);
    // 初始化数据库读取表
    // HttpConn::initMySQLResult();
}

void WebServer::eventListen()
{
    // 创建监听的套接字
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket");
        LOG_ERROR("%s", "Create socket failed.");
        exit(CREATE_SOCKET_ERROR);
    }

    struct linger tmp = {1, 1};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    // 设置端口复用
    // 端口复用一定要在绑定前设置
    int reuse = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt");
        LOG_ERROR("%s", "Set reuse port failed.");
        exit(SET_REUSE_PORT_ERROR);
    }

    // 绑定
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        LOG_ERROR("%s", "bind failed.");
        exit(BIND_ERROR);
    }

    // 监听
    if (listen(listenfd, 100) == -1) {
        perror("listen");
        LOG_ERROR("%s", "listen failed.");
        exit(LISTEN_ERROR);
    }

    // 创建epoll对象，事件数组，添加
    epollfd = epoll_create(5);
    if (epollfd == -1) {
        perror("epoll_create");
        LOG_ERROR("%s", "Create epoll failed.");
        exit(CREATE_EPOLL_ERROR);
    }
    HttpConn::setEpollfd(epollfd);

    // 将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false, false);

    // 创建管道
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd) == -1) {
        perror("socketpair");
        LOG_ERROR("%s", "Create socket pair failed.");
        exit(CREATE_SOCKET_PAIR_ERROR);
    }
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false, false);

    // 设置信号处理函数
    // 对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);
    addsig(SIGALRM, sigHandler);
    addsig(SIGTERM, sigHandler);
    addsig(SIGINT, sigHandler);
    addsig(SIGHUP, SIG_IGN);

    alarm(TIMESLOT);  // 定时，5秒后产生SIGALRM信号
}

void WebServer::setDaemon()
{
    if (mDaemonProcess) {
        daemon();
    }
}

int WebServer::start()
{
    setDaemon();

    logWrite();

    connectionPool();

    threadPool();

    eventListen();

    eventLoop();

    return SUCCESS;
}