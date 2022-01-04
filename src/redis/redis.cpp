#include "redis.h"
#include <iostream>
using namespace std;

RedisConnectionPool::RedisConnectionPool() : mCurConn(0), mFreeConn(0) {}

RedisConnectionPool *RedisConnectionPool::getInstance()
{
    static RedisConnectionPool connPool;
    return &connPool;
}

void RedisConnectionPool::init(const string &url, int port, int maxConn)
{
    mUrl = url;
    mPort = port;

    for (int i = 0; i < maxConn; i++) {
        redisContext *con = redisConnect(url.c_str(), port);
        if (con == nullptr) {
            LOG_ERROR("Redis Error");
            exit(REDIS_ERROR);
        }
        connList.push_back(con);
        ++mFreeConn;
    }

    reserve = Semaphore(mFreeConn);

    mMaxConn = mFreeConn;
}


// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
unique_ptr<redisContext, function<void(redisContext *)>> RedisConnectionPool::getConnection()
{
    redisContext *con = nullptr;

    if (0 == connList.size())
        return nullptr;

    reserve.wait();

    lock.lock();

    con = connList.front();
    connList.pop_front();

    --mFreeConn;
    ++mCurConn;

    lock.unlock();
    return unique_ptr<redisContext, function<void(redisContext *)>>(con, [](redisContext *conn) {
        RedisConnectionPool::getInstance()->releaseConnection(conn);
    });
}

// 释放当前使用的连接
bool RedisConnectionPool::releaseConnection(redisContext *con)
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
void RedisConnectionPool::destroyPool()
{
    lock.lock();
    if (connList.size() > 0) {
        list<redisContext *>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            redisContext *con = *it;
            redisFree(con);
        }
        mCurConn = 0;
        mFreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

// 当前空闲的连接数
int RedisConnectionPool::getFreeConn()
{
    return this->mFreeConn;
}

RedisConnectionPool::~RedisConnectionPool()
{
    destroyPool();
}

bool Redis::setTTL(const string &key, int expires)
{
    redisReply *reply = (redisReply *)redisCommand(conn, "EXPIRE %s %d", key.c_str(), expires);
    if (nullptr == reply) {
        LOG_ERROR("EXPIRE command failed!");
        return false;
    }
    else if (!(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1)) {
        LOG_ERROR("EXPIRE command failed!");
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::setStrValue(const string &key, const string &value, int expires)
{
    redisReply *reply = nullptr;
    if (expires > 0)
        reply = (redisReply *)redisCommand(conn, "SET %s %s EX %d", key.c_str(), value.c_str(), expires);
    else
        reply = (redisReply *)redisCommand(conn, "SET %s %s", key.c_str(), value.c_str());
    if (nullptr == reply) {
        LOG_ERROR("SET command failed!");
        return false;
    }
    else if (!(reply->type == REDIS_REPLY_STATUS && strcasecmp(reply->str, "OK") == 0)) {
        LOG_ERROR("SET command failed!");
        return false;
    }
    freeReplyObject(reply);
    return true;
}

string Redis::getStrValue(const string &key)
{
    redisReply *reply = (redisReply *)redisCommand(conn, "GET %s", key.c_str());
    if (nullptr == reply) {
        LOG_ERROR("GET command failed!");
        return "";
    }
    else if (reply->type != REDIS_REPLY_STRING) {
        LOG_ERROR("GET command failed!");
        freeReplyObject(reply);
        return "";
    }
    string result = reply->str;
    freeReplyObject(reply);
    return result;
}

bool Redis::setAnyHashValue(const string &key, const unordered_map<string, string>& values, int expires)
{
    std::vector<const char *> argv(values.size() * 2 + 2);
    std::vector<size_t> argvlen(values.size() * 2 + 2);

    uint32_t j = 0;
    static char msethash[] = "HMSET";
    argv[j] = msethash;
    argvlen[j] = sizeof(msethash) - 1;

    ++j;
    argv[j] = key.c_str();
    argvlen[j] = key.size();

    ++j;
    for (auto i = values.begin(); i != values.end(); i++, j++) {
        argv[j] = i->first.c_str();
        argvlen[j] = i->first.size();

        j++;
        argv[j] = i->second.c_str();
        argvlen[j] = i->second.size();
    }

    // 执行HMSET命令
    redisReply* reply = (redisReply*)redisCommandArgv(conn, argv.size(), &(argv[0]), &(argvlen[0]));
    if (reply) {
        if (REDIS_REPLY_STATUS == reply->type) {
            if (strcmp(reply->str, "OK") == 0) {
                freeReplyObject(reply);
                if (expires > 0) {
                    return setTTL(key, expires);
                }
                return true;
            }
            else {
                LOG_ERROR("HMGET command failed!");
                freeReplyObject(reply);
                return false;
            }
        }
        else {
            LOG_ERROR("HMGET command failed!");
            freeReplyObject(reply);
            return false;
        }
    }

    LOG_ERROR("HMGET command failed!");
    return false;
}

// 获取所有hash值对
unordered_map<string, string> Redis::getHashAllValue(const std::string& key)
{
    unordered_map<string, string> valueMap;
	string strField;
	redisReply *reply = (redisReply *)redisCommand(conn, "HGETALL %s", key.c_str());
    if (reply) {
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (unsigned int j = 0; j < reply->elements; ++j) {
                strField = reply->element[j]->str;
                if (j < reply->elements) {
                    j++;
                    valueMap[strField].assign(reply->element[j]->str, reply->element[j]->len);
                }
            }
            freeReplyObject(reply);
            return valueMap;
        }
        else {
            LOG_ERROR("HGETALL command failed!");
            freeReplyObject(reply);
            return valueMap;
        }
    }

    LOG_ERROR("HGETALL command failed!");

	return valueMap;
}

bool Redis::beginTransaction()
{
    redisReply *reply = (redisReply *)redisCommand(conn, "MULTI");
    if (reply) {
        if (reply->type == REDIS_REPLY_STATUS && strcasecmp(reply->str, "OK") == 0) {
            freeReplyObject(reply);
            return true;
        }
        LOG_ERROR("MULTI command failed!");
        freeReplyObject(reply);
        return false;
    }
    LOG_ERROR("MULTI command failed!");
    return false;
}

redisReply *Redis::endTransaction()
{
    redisReply *reply = (redisReply *)redisCommand(conn, "EXEC");
    if (reply) {
        return reply;
    }
    LOG_ERROR("EXEC command failed!");
	return nullptr;
}