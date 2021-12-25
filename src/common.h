#ifndef COMMON_H
#define COMMON_H

// C Library
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cerrno>

// C++ Library
#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <unordered_map>
#include <exception>

enum {
    SUCCESS,
    INVALID_OPTION,
    CREATE_THREAD_POOL_ERROR,
    CREATE_SOCKET_ERROR,
    SET_REUSE_PORT_ERROR,
    BIND_ERROR,
    LISTEN_ERROR,
    CREATE_EPOLL_ERROR,
    CREATE_SOCKET_PAIR_ERROR,
    BLOCK_QUEUE_SIZE_ERROR,
    CREATE_DIR_ERROR,
    MYSQL_ERROR
};

#endif