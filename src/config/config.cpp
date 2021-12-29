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
    cerr << " -p, --port=PORT         The port of the server." << endl;
    cerr << " -r, --doc_root=PATH     The root directory of resources." << endl;
    cerr << " -c                      Close log." << endl;
    cerr << " -v, --version           Print the version number and exit." << endl;
    cerr << " -h, --help              Print this message and exit." << endl;
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
            {"version", no_argument, 0, 'v'},
            {"help", no_argument, 0, 'h'},
            {0, 0, 0, 0}};

        int c = getopt_long(argc, argv, "p:r:vh",
                        long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'p':
            port = atoi(optarg);
            if (port < 0 || port > 65535) {
                cerr << "The port " << port << " out of range." << endl;
                exit(INVALID_OPTION);
            }
            break;

        case 'r':
            docRoot = optarg;
            break;

        case 'c':
            closeLog = 1;
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
}