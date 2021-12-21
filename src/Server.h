#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include "http_conn.h"
#include "thread/threadpool.h"
#include "thread/locker.h"
#include "utils/utils.h"
#include "epoll/epoll.h"
#include "log/log.h"

class WebServer {
private:
    const int MAX_FD = 65535;
    const int MAX_EVENT_NUM = 10000;

private:
    int port;
    int epollfd;
    int listenfd;
    sockaddr_in address;
    bool stopServer;
    bool timeout; // 定时器超时，执行定时任务（关闭非活跃连接）
    std::unordered_map<int, std::shared_ptr<HttpConn>> users;
    std::shared_ptr<ThreadPool<HttpConn>> pool; // 线程池
    static int pipefd[2];

public:
    WebServer(int port, const string &docRoot);
    ~WebServer();
    int start();

    static void sigHandler(int sig);
    static void timerHandler();
};

#endif