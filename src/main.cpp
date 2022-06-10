#include "config/config.h"
#include "server/Server.h"

int main(int argc, char *argv[])
{
    Config config;
    config.parseArgs(argc, argv);

    WebServer server(config);
    server.start();

    return SUCCESS;
}