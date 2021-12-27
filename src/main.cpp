#include "config/config.h"
#include "server/Server.h"

int main(int argc, char *argv[])
{
    string user = "root";
    string password = "123456";
    string databaseName = "mydb";

    Config config;
    config.parseArgs(argc, argv);

    WebServer server(config.port, config.docRoot, 0, user, password, databaseName);
    server.start();

    return SUCCESS;
}