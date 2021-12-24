#ifndef SERVER_H
#define SERVER_H

#include "../common.h"
#include "../http/http_conn.h"
#include "../thread/threadpool.h"
#include "../thread/locker.h"
#include "../utils/utils.h"
#include "../epoll/epoll.h"
#include "../log/log.h"
#include "../timer/Timer.h"

class WebServer {
private:
    static const int MAX_FD = 65536;
    static const int MAX_EVENT_NUM = 10000;

private:
    int port;
    int epollfd;
    int listenfd;
    sockaddr_in address;
    std::unordered_map<int, std::shared_ptr<HttpConn>> usersConn;
    std::unordered_map<int, std::shared_ptr<ClientData>> usersTimer;
    std::shared_ptr<ThreadPool<HttpConn>> pool; // 线程池
    static int pipefd[2];
    epoll_event events[MAX_EVENT_NUM];
    static const int TIMESLOT = 5; // 每隔5s的定时，检测有没有任务超时
    static TimerList timerList;

    void logWrite();
    void threadPool();
    void eventListen();
    void eventLoop();
    void doWrite(int sockfd);
    void doRead(int sockfd);
    bool doClientData();
    bool doSignal(bool &timeout, bool &stopServer);
    void addClientInfo(int connfd, struct sockaddr_in client_address);
    void doTimer(std::shared_ptr<Timer> timer, int sockfd);
    void adjustTimer(std::shared_ptr<Timer> timer);

public:
    WebServer(int port, const string &docRoot);
    ~WebServer();
    int start();

    static void sigHandler(int sig);
    static void timerHandler();
};

#endif