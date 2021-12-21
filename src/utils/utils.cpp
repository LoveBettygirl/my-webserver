#include "utils.h"

void addsig(int sig, void (*handler)(int))
{
    struct sigaction sa; // 这里必须加struct，要不然和函数名字冲突了
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

int setnonblocking(int fd)
{
    int oldFlag = fcntl(fd, F_GETFL);
    int newFlag = oldFlag | O_NONBLOCK;
    fcntl(fd, F_SETFL, newFlag);
    return oldFlag;
}

void createDir(const char *path)
{
    int ret = mkdir(path, 0777);
    if (ret == -1) {
        if (errno != EEXIST)
            exit(CREATE_DIR_ERROR);
    }
}