#include "config.h"

void Config::showVersion()
{
    cout << "v1.0.0" << endl;
    exit(SUCCESS);
}

void Config::showUsage(const char *argv)
{
    // basename(): 从路径获得文件名
    cerr << "Usage: " << basename(argv) << " [options]" << endl;
    cerr << "Options:" << endl;
    cerr << " -p, --port=PORT                        The port of the server." << endl;
    cerr << " -r, --doc_root=PATH                    The root directory of resources." << endl;
    cerr << " -c, --close_log                        Close log." << endl;
    cerr << " -d, --daemon                           Run in daemon process." << endl;
    cerr << " -t NUM, --thread_pool_size=NUM         The thread pool size of the server." << endl;
    cerr << " -s NUM, --connectcion_pool_size=NUM    The connectcion pool size of the server." << endl;
    cerr << " -i, --config                           Specify config file." << endl;
    cerr << " -v, --version                          Print the version number and exit." << endl;
    cerr << " -h, --help                             Print this message and exit." << endl;
    exit(INVALID_OPTION);
}

void Config::parseArgs(int argc, char *argv[])
{
    while (true) {
        int option_index = 0;
        static struct option long_options[] = {
            {"port", required_argument, 0, 'p'},
            {"doc_root", required_argument, 0, 'r'},
            {"close_log", no_argument, 0, 'c'},
            {"daemon", no_argument, 0, 'd'},
            {"thread_pool_size", required_argument, 0, 't'},
            {"connectcion_pool_size", required_argument, 0, 's'},
            {"config", required_argument, 0, 'i'},
            {"version", no_argument, 0, 'v'},
            {"help", no_argument, 0, 'h'},
            {0, 0, 0, 0}};

        int c = getopt_long(argc, argv, "p:r:t:s:i:cdvh",
                        long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'p':
            port = atoi(optarg);
            if (port < 0 || port > 65535) {
                cerr << "The server port " << port << " out of range." << endl;
                exit(INVALID_OPTION);
            }
            break;

        case 'r':
            docRoot = optarg;
            break;

        case 'c':
            closeLog = true;
            break;

        case 'd':
            daemonProcess = true;
            break;

        case 't':
            threadPool = atoi(optarg);
            if (threadPool <= 0) {
                cerr << "The thread pool size " << threadPool << " is invalid." << endl;
                exit(INVALID_OPTION);
            }
            break;

        case 's':
            connectionPool = atoi(optarg);
            if (connectionPool <= 0) {
                cerr << "The connection pool size " << connectionPool << " is invalid." << endl;
                exit(INVALID_OPTION);
            }
            break;
        
        case 'i':
            configFile = optarg;
            break;

        case 'v':
            showVersion();
            break;

        case 'h':
        case '?':
            showUsage(argv[0]);
            break;

        default:
            showUsage(argv[0]);
        }
    }

    if (optind < argc)
        showUsage(argv[0]);

    // 如果指定了配置文件，就以配置文件中的内容为准
    if (!configFile.empty())
        loadConfigFile();

    docRoot = getPath(docRoot);
}

void Config::loadConfigFile()
{
    ifstream fin(configFile);
    if (!fin.is_open()) {
        perror("Open config file");
        exit(INVALID_OPTION);
    }

    regex r("^(1\\d{2}|2[0-4]\\d|25[0-5]|[1-9]\\d|\\d)\\."
            "(1\\d{2}|2[0-4]\\d|25[0-5]|[1-9]\\d|\\d)\\."
            "(1\\d{2}|2[0-4]\\d|25[0-5]|[1-9]\\d|\\d)\\."
            "(1\\d{2}|2[0-4]\\d|25[0-5]|[1-9]\\d|\\d)$");

    string temp;
    // 1. 注释 2. 正确的配置项 3. 去掉开头多余的空格
    while (getline(fin, temp)) {
        // 忽略注释
        int idx = temp.find('#');
        if (idx != -1) {
            temp = temp.substr(0, idx);
        }

        trim(temp);
        
        if (temp.empty()) {
            continue;
        }

        // 解析配置项
        idx = temp.find('=');
        if (idx == -1) {
            // 配置项不合法
            continue;
        }

        std::string key;
        std::string value;
        key = temp.substr(0, idx);
        trim(key);
        value = temp.substr(idx + 1);
        trim(value);

        if (key == "server.port") {
            port = stoi(value);
            if (port < 0 || port > 65535) {
                cerr << "The server port " << port << " out of range." << endl;
                exit(INVALID_OPTION);
            }
        }
        else if (key == "server.doc_root") {
            docRoot = value;
        }
        else if (key == "server.close_log") {
            if (value != "true" && value != "false") {
                cerr << "The option close_log is invalid, must be true or false." << endl;
                exit(INVALID_OPTION);
            }
            closeLog = (value == "true");
        }
        else if (key == "server.daemon") {
            if (value != "true" && value != "false") {
                cerr << "The option daemon is invalid, must be true or false." << endl;
                exit(INVALID_OPTION);
            }
            daemonProcess = (value == "true");
        }
        else if (key == "server.thread_pool_size") {
            threadPool = stoi(value);
            if (threadPool <= 0) {
                cerr << "The thread pool size " << threadPool << " is invalid." << endl;
                exit(INVALID_OPTION);
            }
        }
        else if (key == "server.connection_pool_size") {
            connectionPool = stoi(value);
            if (connectionPool <= 0) {
                cerr << "The connection pool size " << connectionPool << " is invalid." << endl;
                exit(INVALID_OPTION);
            }
        }
        else if (key == "mysql.user") {
            mysqlUser = value;
        }
        else if (key == "mysql.password") {
            mysqlPassword = value;
        }
        else if (key == "mysql.ip") {
            mysqlIP = value;
            if (mysqlIP != "localhost" && !regex_match(mysqlIP, r)) {
                cerr << "Invaild mysql IP address format: " << mysqlIP << endl;
                exit(INVALID_OPTION);
            }
        }
        else if (key == "mysql.port") {
            mysqlPort = stoi(value);
            if (mysqlPort < 0 || mysqlPort > 65535) {
                cerr << "The mysql port " << mysqlPort << " out of range." << endl;
                exit(INVALID_OPTION);
            }
        }
        else if (key == "mysql.database") {
            mysqlDatabase = value;
        }
        else if (key == "redis.ip") {
            redisIP = value;
            if (redisIP != "localhost" && !regex_match(redisIP, r)) {
                cerr << "Invaild redis IP address format: " << redisIP << endl;
                exit(INVALID_OPTION);
            }
        }
        else if (key == "redis.port") {
            redisPort = stoi(value);
            if (redisPort < 0 || redisPort > 65535) {
                cerr << "The redis port " << redisPort << " out of range." << endl;
                exit(INVALID_OPTION);
            }
        }
        else {
            cerr << "Invalid config item: " << key << endl;
            exit(INVALID_OPTION);
        }
    }

    fin.close();
}

// 去掉字符串前后的空格
void Config::trim(string &temp)
{
    // 去掉字符串前面多余的空格
    int idx = temp.find_first_not_of(' ');
    if (idx != -1) {
        // 说明字符串前面有空格
        temp = temp.substr(idx);
    }
    // 去掉字符串后面多余的空格
    idx = temp.find_last_not_of(' ');
    if (idx != -1) {
        // 说明字符串后面有空格
        temp = temp.substr(0, idx + 1);
    }
}