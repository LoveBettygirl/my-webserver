#ifndef EPOLL_H
#define EPOLL_H

#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../utils/utils.h"

void addfd(int epollfd, int fd, bool one_shot, bool et);
void removefd(int epollfd, int fd);
void modifyfd(int epollfd, int fd, int ev);

#endif