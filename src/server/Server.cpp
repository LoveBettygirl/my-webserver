#include "Server.h"

using namespace std;

int WebServer::pipefd[2] = {};
TimerList WebServer::timerList;

WebServer::WebServer(int port, const string &docRoot):
    port(port),
    epollfd(0),
    listenfd(0)
{
    if (docRoot != "")
        HttpConn::setDocRoot(docRoot);
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
    timerList.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}

void WebServer::addClientInfo(int connfd, struct sockaddr_in client_address)
{
    usersConn[connfd] = make_shared<HttpConn>();
    usersConn[connfd]->init(connfd, client_address);

    usersTimer[connfd] = make_shared<ClientData>(client_address, connfd);
    shared_ptr<Timer> timer = make_shared<Timer>();
    timer->setUserData(usersTimer[connfd]);
    timer->setExpire(time(nullptr) + 3 * TIMESLOT);
    Callback callback(epollfd);
    timer->setCallback(callback);
    usersTimer[connfd]->setTimer(timer);
    timerList.addTimer(timer);
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

    if (HttpConn::getUserCount() >= MAX_FD) {
        // 目前连接数满了
        // 服务器内部正忙
        close(connfd);
        LOG_ERROR("%s", "Cannot establish with %s", ip);
        return false;
    }

    LOG_INFO("client(%s) is connected", ip);

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
                {
                    stopServer = true;
                    break;
                }
            }
        }
    }
    return true;
}

void WebServer::doTimer(shared_ptr<Timer> timer, int sockfd)
{
    timer->getCallback()(usersTimer[sockfd]);
    if (timer) {
        timerList.delTimer(timer);
    }
}

void WebServer::adjustTimer(shared_ptr<Timer> timer)
{
    timerList.adjustTimer(timer, time(nullptr) + 3 * TIMESLOT);
}

void WebServer::doRead(int sockfd)
{
    shared_ptr<Timer> timer = usersTimer[sockfd]->getTimer();
    if (usersConn[sockfd]->read()) {
        // 一次性把所有数据都读完
        pool->append(usersConn[sockfd]);
        if (timer)
            adjustTimer(timer);
    } else {
        doTimer(timer, sockfd);
    }
}

void WebServer::doWrite(int sockfd)
{
    shared_ptr<Timer> timer = usersTimer[sockfd]->getTimer();
    // 一次性把所有数据都写完
    if (usersConn[sockfd]->write()) {
        if (timer)
            adjustTimer(timer);
    }
    else {
        doTimer(timer, sockfd);
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
                doTimer(usersTimer[sockfd]->getTimer(), sockfd);
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
    Log::getInstance()->init("./logs/ServerLog", 0, 2000, 800000, 0);
}

void WebServer::threadPool()
{
    // 创建线程池，初始化线程池
    try {
        pool = shared_ptr<ThreadPool<HttpConn>>(new ThreadPool<HttpConn>());
    } catch (...) {
        LOG_ERROR("%s", "Create thread pool failed.");
        exit(CREATE_THREAD_POOL_ERROR);
    }
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

    alarm(TIMESLOT);  // 定时，5秒后产生SIGALARM信号
}

int WebServer::start()
{
    logWrite();

    threadPool();

    eventListen();

    eventLoop();

    return SUCCESS;
}