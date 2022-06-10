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
#include "../config/config.h"

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

    int mThreadPoolSize = 8;
    int mConnectionPoolSize = 8;
    bool mCloseLog = false;
    bool mDaemonProcess = false;

    std::string mMySQLIP = "127.0.0.1";
    int mMySQLPort = 3306;
    std::string mMySQLUser; // 登陆数据库用户名
    std::string mMySQLPassword; // 登陆数据库密码
    std::string mMySQLDatabaseName; // 使用数据库名

    std::string mRedisIP = "127.0.0.1";
    int mRedisPort = 6379;

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
    void setDaemon();

public:
    WebServer(const Config &config);
    ~WebServer();
    int start();

    static void sigHandler(int sig);
    static void timerHandler();
};

#endif