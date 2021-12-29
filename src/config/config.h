#ifndef CONFIG_H
#define CONFIG_H

#include <getopt.h>
#include <iostream>
#include <string>
#include "../common.h"

using namespace std;

class Config {
public:
    void parseArgs(int argc, char *argv[]);
    int port = 10000;
    string docRoot = "./resources";
    int closeLog = 0;
private:
    void showVersion();
    void showUsage(const char *argv);
};

#endif