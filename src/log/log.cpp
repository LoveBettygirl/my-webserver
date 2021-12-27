#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

int Log::mCloseLog = 1; //关闭日志

Log::Log()
{
    mCount = 0;
    mIsAsync = false;
}

Log::~Log()
{
    if (m_fp != nullptr) {
        fclose(m_fp);
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *filename, int closeLog, int logBufSize, int splitLines, int maxQueueSize)
{
    //如果设置了maxQueueSize,则设置为异步
    if (maxQueueSize >= 1) {
        mIsAsync = true;
        if (!mLogQueue) {
            unique_ptr<BlockQueue<string>> newQueue(new BlockQueue<string>(maxQueueSize));
            mLogQueue = move(newQueue);
        }
        pthread_t tid;
        //flushLogThread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, nullptr, flushLogThread, nullptr);
    }
    
    mCloseLog = closeLog;
    mLogBufSize = logBufSize;
    if (!m_buf) {
        unique_ptr<char[]> newBuf(new char[mLogBufSize]{});
        m_buf = move(newBuf);
    }
    mSplitLines = splitLines;

    time_t t = time(nullptr);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

 
    const char *p = strrchr(filename, '/');
    char logFullName[LOG_PATH_LEN] = {0};

    if (p == nullptr) {
        snprintf(logFullName, LOG_PATH_LEN - 1, "%d_%02d_%02d_%s.log", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, filename) < 0 ? abort() : (void)0;
    }
    else {
        strcpy(logName, p + 1);
        strncpy(dirName, filename, p - filename + 1);
        createDir(dirName);
        snprintf(logFullName, LOG_PATH_LEN - 1, "%s%d_%02d_%02d_%s.log", dirName, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, logName) < 0 ? abort() : (void)0;
    }

    mToday = my_tm.tm_mday;
    
    m_fp = fopen(logFullName, "a");
    if (m_fp == nullptr) {
        return false;
    }

    return true;
}

void Log::writeLog(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level) {
    case 0:
        strcpy(s, "[DEBUG]:");
        break;
    case 1:
        strcpy(s, "[INFO]:");
        break;
    case 2:
        strcpy(s, "[WARN]:");
        break;
    case 3:
        strcpy(s, "[ERROR]:");
        break;
    case 4:
        strcpy(s, "[FATAL]:");
        break;
    default:
        strcpy(s, "[INFO]:");
        break;
    }
    //写入一个log，对mCount++, mSplitLines最大行数
    m_mutex.lock();
    mCount++;

    // everyday log
    if (mToday != my_tm.tm_mday || mCount % mSplitLines == 0) {
        
        char newLog[LOG_PATH_LEN] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday) < 0 ? abort() : (void)0;
       
        if (mToday != my_tm.tm_mday) {
            snprintf(newLog, LOG_PATH_LEN - 1, "%s%s%s", dirName, tail, logName) < 0 ? abort() : (void)0;
            mToday = my_tm.tm_mday;
            mCount = 0;
        }
        else {
            snprintf(newLog, LOG_PATH_LEN - 1, "%s%s%s.%lld", dirName, tail, logName, mCount / mSplitLines) < 0 ? abort() : (void)0;
        }
        m_fp = fopen(newLog, "a");
    }
 
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string logStr;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf.get(), 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf.get() + n, mLogBufSize, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    logStr = m_buf.get();

    m_mutex.unlock();

    if (mIsAsync && !mLogQueue->full()) {
        mLogQueue->push(logStr);
    }
    else {
        m_mutex.lock();
        fputs(logStr.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}