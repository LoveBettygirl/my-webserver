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
#include "../mysql/mysql.h"

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
    std::shared_ptr<ThreadPool<HttpConn>> pool; // 线程池
    static int pipefd[2];
    epoll_event events[MAX_EVENT_NUM];
    static const int TIMESLOT = 5; // 每隔5s的定时，检测有没有任务超时
    static TimerHeap timerHeap;

    std::string mUser; // 登陆数据库用户名
    std::string mPassword; // 登陆数据库密码
    std::string mDatabaseName; // 使用数据库名
    int mCloseLog;

    void logWrite();
    void threadPool();
    void connectionPool();
    void eventListen();
    void eventLoop();
    void doWrite(int sockfd);
    void doRead(int sockfd);
    bool doClientData();
    bool doSignal(bool &timeout, bool &stopServer);
    void addClientInfo(int connfd, struct sockaddr_in client_address);
    void doTimer(int sockfd);
    void adjustTimer(int sockfd, time_t expire);
    void closeConn(int sockfd);

public:
    WebServer(int port, const std::string &docRoot, int closeLog, const std::string &user, const std::string &password, const std::string &databaseName);
    ~WebServer();
    int start();

    static void sigHandler(int sig);
    static void timerHandler();
};

#endif