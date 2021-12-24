#include "config/config.h"
#include "server/Server.h"

int main(int argc, char *argv[])
{
    Config config;
    config.parseArgs(argc, argv);

    WebServer server(config.port, config.docRoot);
    server.start();

    return SUCCESS;
}