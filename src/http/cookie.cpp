#include "cookie.h"

#include <iostream>
using namespace std;

Cookie::Cookie(const std::string &user, const std::string &pwd, const std::string &path) : maxAge(86400), path(path)
{
    srand((uint32_t)time(nullptr));
    std::string str = user + ":" + pwd;
    uint32_t currTime = time(nullptr);
    std::string timeStr = std::to_string(currTime);
    str += ":";
    str += timeStr;
    std::string secret;
    for (int i = 0; i < 16; i++) {
        secret.push_back(rand() % 96 + 32);
    }
    str += ":";
    str += secret;

    MD5 md5;
    std::string md5Str = md5.stringResult(str);

    sessionId = md5Str;
}

Cookie::Cookie(const std::string &cookie)
{
    size_t index = 0, preIndex = 0;
    while (preIndex < cookie.size()) {
        index = cookie.find("; ", preIndex);
        index = (index == std::string::npos) ? cookie.size() : index;
        std::string curr = cookie.substr(preIndex, index - preIndex);
        size_t equalIndex = curr.find("=");
        if (equalIndex != std::string::npos) {
            std::string key = curr.substr(0, equalIndex);
            std::string value = curr.substr(equalIndex + 1);
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            cout << key << ": " << value << endl;
            if (key == "session-id") {
                sessionId = value;
            }
            else if (key == "max-age") {
                maxAge = std::stoi(value);
            }
            else if (key == "path") {
                path = value;
            }
        }
        preIndex = index + 2;
    }
}