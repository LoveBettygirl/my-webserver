#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
#include <cstring>
#include "../thread/locker.h"
#include <list>
#include "../common.h"
#include "../log/log.h"
#include <unordered_map>
#include <memory>
using namespace std;

// Redis配置信息
static string serverIP = "127.0.0.1";
static int serverPort = 6379;

class RedisConnectionPool {
public:
    unique_ptr<redisContext, function<void(redisContext *)>> getConnection(); // 获取数据库连接
    bool releaseConnection(redisContext *conn); // 释放连接
    int getFreeConn(); // 获取连接
    void destroyPool(); // 销毁所有连接

    static RedisConnectionPool *getInstance();

    void init(const string &url, int port, int maxConn);

private:
    RedisConnectionPool();
    ~RedisConnectionPool();

    int mMaxConn; // 最大连接数
    int mCurConn; // 当前已使用的连接数
    int mFreeConn; // 当前空闲的连接数
    Locker lock;
    list<redisContext *> connList; // 连接池
    Semaphore reserve;

public:
    string mUrl; // 主机地址
    int mPort; // 数据库端口号
};

class Redis {
public:
    Redis() {}
    ~Redis() {}

    bool setStrValue(const string &key, const string &value, int expires = 0);

    string getStrValue(const string &key);

    bool setTTL(const string &key, int expires = 0);

    bool setAnyHashValue(const string &key, const unordered_map<string, string>& values, int expires = 0);

    unordered_map<string, string> getHashAllValue(const std::string& key);

    bool beginTransaction();

    redisReply *endTransaction();

    void setConn(redisContext *conn) { this->conn = conn; }

private:
    redisContext *conn;
};

#endif