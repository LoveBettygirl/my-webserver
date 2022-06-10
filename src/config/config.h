#ifndef CONFIG_H
#define CONFIG_H

#include <getopt.h>
#include <iostream>
#include <string>
#include <fstream>
#include <regex>
#include "../common.h"
#include "../utils/utils.h"

using namespace std;

class Config {
public:
    void parseArgs(int argc, char *argv[]);
    int port = 10000;
    string docRoot = "./resources";

    bool closeLog = false;
    int threadPool = 8;
    int connectionPool = 8;
    bool daemonProcess = false;

    string mysqlUser = "root";
    string mysqlPassword = "123456";
    string mysqlDatabase = "mydb";
    string mysqlIP = "127.0.0.1";
    int mysqlPort = 3306;

    string redisIP = "127.0.0.1";
    int redisPort = 6379;
private:
    void showVersion();
    void showUsage(const char *argv);
    void loadConfigFile();
    void trim(string &temp); // 去掉字符串前后空格
    string configFile;
};

#endif