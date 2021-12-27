#include <mysql/mysql.h>
#include <string>
#include <cstdlib>
#include <list>
#include <pthread.h>
#include <iostream>
#include "mysql_connection_pool.h"

using namespace std;

ConnectionPool::ConnectionPool() : mCurConn(0), mFreeConn(0) {}

ConnectionPool *ConnectionPool::getInstance()
{
    static ConnectionPool connPool;
    return &connPool;
}

void ConnectionPool::init(const string &url, const string &user, const string &passWord, const string &databaseName, int port, int maxConn, int closeLog)
{
    mUrl = url;
    mPort = port;
    mUser = user;
    mPassWord = passWord;
    mDatabaseName = databaseName;
    mCloseLog = closeLog;

    for (int i = 0; i < maxConn; i++) {
        MYSQL *con = nullptr;
        con = mysql_init(con);

        if (con == nullptr) {
            LOG_ERROR("MySQL Error");
            exit(MYSQL_ERROR);
        }
        con = mysql_real_connect(con, url.c_str(), user.c_str(), passWord.c_str(), databaseName.c_str(), port, nullptr, 0);

        if (con == nullptr) {
            LOG_ERROR("MySQL Error");
            exit(MYSQL_ERROR);
        }
        connList.push_back(con);
        ++mFreeConn;
    }

    reserve = Semaphore(mFreeConn);

    mMaxConn = mFreeConn;
}


// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
unique_ptr<MYSQL, function<void(MYSQL *)>> ConnectionPool::getConnection()
{
    MYSQL *con = nullptr;

    if (0 == connList.size())
        return nullptr;

    reserve.wait();

    lock.lock();

    con = connList.front();
    connList.pop_front();

    --mFreeConn;
    ++mCurConn;

    lock.unlock();
    return unique_ptr<MYSQL, function<void(MYSQL *)>>(con, [](MYSQL *mysql) {
        ConnectionPool::getInstance()->releaseConnection(mysql);
    });
}

// 释放当前使用的连接
bool ConnectionPool::releaseConnection(MYSQL *con)
{
    if (con == nullptr)
        return false;

    lock.lock();

    connList.push_back(con);
    ++mFreeConn;
    --mCurConn;

    lock.unlock();

    reserve.post();
    return true;
}

// 销毁数据库连接池
void ConnectionPool::destroyPool()
{
    lock.lock();
    if (connList.size() > 0) {
        list<MYSQL *>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);
        }
        mCurConn = 0;
        mFreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

// 当前空闲的连接数
int ConnectionPool::getFreeConn()
{
    return this->mFreeConn;
}

ConnectionPool::~ConnectionPool()
{
    destroyPool();
}