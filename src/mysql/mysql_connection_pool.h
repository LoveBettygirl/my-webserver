#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <cstdio>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <cstring>
#include <iostream>
#include <string>
#include "../thread/locker.h"
#include "../log/log.h"
#include "../common.h"
#include <memory>
#include <functional>

using namespace std;

class ConnectionPool {
public:
    unique_ptr<MYSQL, function<void(MYSQL *)>> getConnection(); // 获取数据库连接
    bool releaseConnection(MYSQL *conn); // 释放连接
    int getFreeConn(); // 获取连接
    void destroyPool(); // 销毁所有连接

    static ConnectionPool *getInstance();

    void init(const string &url, const string &user, const string &passWord, const string &databaseName, int port, int maxConn, int closeLog);

private:
    ConnectionPool();
    ~ConnectionPool();

    int mMaxConn; // 最大连接数
    int mCurConn; // 当前已使用的连接数
    int mFreeConn; // 当前空闲的连接数
    Locker lock;
    list<MYSQL *> connList; // 连接池
    Semaphore reserve;

public:
    string mUrl; // 主机地址
    string mPort; // 数据库端口号
    string mUser; // 登陆数据库用户名
    string mPassWord; // 登陆数据库密码
    string mDatabaseName; // 使用数据库名
    int mCloseLog; // 日志开关
};

#endif