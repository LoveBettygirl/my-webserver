#ifndef UTILS_H
#define UTILS_H

#include <signal.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include "../common.h"

void addsig(int sig, void (*handler)(int));
int setnonblocking(int fd);
void createDir(const char *path);

#endif