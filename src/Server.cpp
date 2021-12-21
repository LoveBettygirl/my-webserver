#include "Server.h"

using namespace std;

int WebServer::pipefd[2] = {};

WebServer::WebServer(int port, const string &docRoot):
    port(port),
    epollfd(0),
    stopServer(false),
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
    send(pipefd[1], reinterpret_cast<char*>(&msg), 1, 0);
    errno = save_errno;
}

void WebServer::timerHandler()
{
    // 定时处理任务，实际上就是调用tick()函数
    HttpConn::tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(HttpConn::TIMESLOT);
}

int WebServer::start()
{
    // 初始化日志
    Log::getInstance()->init("./logs/ServerLog", 0, 2000, 800000, 0);

    // 对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    try {
        pool = shared_ptr<ThreadPool<HttpConn>>(new ThreadPool<HttpConn>());
    } catch (...) {
        LOG_ERROR("%s", "Create thread pool failed.");
        return CREATE_THREAD_POOL_ERROR;
    }

    // 创建监听的套接字
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket");
        LOG_ERROR("%s", "Create socket failed.");
        return CREATE_SOCKET_ERROR;
    }

    // 设置端口复用
    // 端口复用一定要在绑定前设置
    int reuse = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt");
        LOG_ERROR("%s", "Set reuse port failed.");
        return SET_REUSE_PORT_ERROR;
    }

    // 绑定
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(listenfd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        perror("bind");
        LOG_ERROR("%s", "bind failed.");
        return BIND_ERROR;
    }

    // 监听
    if (listen(listenfd, 100) == -1) {
        perror("listen");
        LOG_ERROR("%s", "listen failed.");
        return LISTEN_ERROR;
    }

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUM];
    epollfd = epoll_create(5);
    if (epollfd == -1) {
        perror("epoll_create");
        LOG_ERROR("%s", "Create epoll failed.");
        return CREATE_EPOLL_ERROR;
    }
    HttpConn::setEpollfd(epollfd);

    // 将监听的文件描述符添加到epoll对象中
    addOtherfd(epollfd, listenfd);

    // 创建管道
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd) == -1) {
        perror("socketpair");
        LOG_ERROR("%s", "Create socket pair failed.");
        return CREATE_SOCKET_PAIR_ERROR;
    }
    setnonblocking(pipefd[1]);
    addOtherfd(epollfd, pipefd[0]);

    // 设置信号处理函数
    addsig(SIGALRM, sigHandler);
    addsig(SIGTERM, sigHandler);
    bool stopServer = false;

    bool timeout = false;
    alarm(HttpConn::TIMESLOT);  // 定时，5秒后产生SIGALARM信号

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
                sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, reinterpret_cast<sockaddr*>(&client_address), &client_addrlen);
                if (connfd == -1) {
                    perror("accept");
                    LOG_ERROR("%s", "accept failed.");
                    continue;
                }

                char ip[16] = {0};
                inet_ntop(AF_INET, &client_address.sin_addr ,ip, sizeof(ip));

                if (HttpConn::getUserCount() >= MAX_FD) {
                    // 目前连接数满了
                    // 服务器内部正忙
                    close(connfd);
                    LOG_ERROR("%s", "Cannot establish with %s", ip);
                    continue;
                }

                LOG_INFO("client(%s) is connected", ip);

                // 将新的客户端数据初始化，放到数组中
                users[connfd] = make_shared<HttpConn>();
                users[connfd]->init(connfd, client_address, users[connfd]);
            } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 说明有信号到来，要处理信号
                int sig;
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for(int i = 0; i < ret; ++i) {
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
                            }
                        }
                    }
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常端口或者错误等事件
                // 关闭连接
                users[sockfd]->closeConn();
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd]->read()) {
                    // 一次性把所有数据都读完
                    pool->append(users[sockfd]);
                } else {
                    users[sockfd]->closeConn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd]->write()) {
                    // 一次性把所有数据都写完
                    users[sockfd]->closeConn();
                }
            }
        }

        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if (timeout) {
            timerHandler();
            timeout = false;
        }
    }

    return SUCCESS;
}