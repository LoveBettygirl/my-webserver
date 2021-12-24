#ifndef LOG_H
#define LOG_H

#include <cstdio>
#include <iostream>
#include <string>
#include <cstdarg>
#include <pthread.h>
#include <memory>
#include "block_queue.h"
#include "../thread/locker.h"
#include "../utils/utils.h"

using namespace std;

class Log {
public:
    static Log *getInstance() {
        static Log instance;
        return &instance;
    }

    static void *flushLogThread(void *args) {
        Log::getInstance()->asyncWriteLog();
        return nullptr;
    }

    bool init(const char *filename, int closeLog, int logBufSize = 8192, int splitLines = 5000000, int maxQueueSize = 0);

    void writeLog(int level, const char *format, ...);

    void flush(void);

    static int mCloseLog; // 关闭日志

private:
    Log();
    virtual ~Log();
    void *asyncWriteLog() {
        string singleLog;
        //从阻塞队列中取出一个日志string，写入文件
        while (mLogQueue->pop(singleLog)) {
            m_mutex.lock();
            fputs(singleLog.c_str(), m_fp);
            m_mutex.unlock();
        }
        return nullptr;
    }

private:
    static const int LOG_NAME_LEN = 128;
    static const int LOG_PATH_LEN = 256;
    char dirName[LOG_NAME_LEN]; // 路径名
    char logName[LOG_NAME_LEN]; //log文件名
    int mSplitLines;  //日志最大行数
    int mLogBufSize; //日志缓冲区大小
    long long mCount;  //日志行数记录
    int mToday;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    unique_ptr<char[]> m_buf;
    unique_ptr<BlockQueue<string>> mLogQueue; //阻塞队列
    bool mIsAsync;                  //是否同步
    Locker m_mutex;
};

#define LOG_DEBUG(format, ...) if(!Log::mCloseLog) {Log::getInstance()->writeLog(0, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_INFO(format, ...) if(!Log::mCloseLog) {Log::getInstance()->writeLog(1, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_WARN(format, ...) if(!Log::mCloseLog) {Log::getInstance()->writeLog(2, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(!Log::mCloseLog) {Log::getInstance()->writeLog(3, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#endif
