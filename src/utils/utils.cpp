#include "utils.h"

void addsig(int sig, void (*handler)(int))
{
    struct sigaction sa; // 这里必须加struct，要不然和函数名字冲突了
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
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

void daemon()
{
    int i;
    int fd0;
    pid_t pid;
    
    umask(0);
    
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(FORK_ERROR);
    }
    else if (pid > 0) {
        exit(SUCCESS);
    }
    
    setsid(); // 创建一个会话
    
    addsig(SIGCHLD, SIG_IGN); // 忽略子进程退出信号
    
    if (chdir("/") < 0) { // 将当前工作目录更改为根目录
        return;
    }
    
    // 关闭不需要的文件描述符，或者重定向到/dev/null
    close(0);
    fd0 = open("/dev/null", O_RDWR);
    dup2(fd0, 1);
    dup2(fd0, 2);
}