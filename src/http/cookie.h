#ifndef COOKIE_H
#define COOKIE_H

#include <string>
#include <algorithm>
#include <cctype>
#include <ctime>
#include "md5.h"

class Cookie {
private:
    std::string sessionId;
    uint32_t maxAge;
    std::string path;
public:
    Cookie(const std::string &user, const std::string &pwd, const std::string &path);
    Cookie(const std::string &cookie);
    std::string getSessionId() const { return sessionId; };
    uint32_t getMaxAge() const { return maxAge; };
    void setMaxAge(uint32_t maxAge) { this->maxAge = maxAge; }
    std::string getPath() const { return path; };
    void setPath(const std::string &path) { this->path = path; };
    std::string getCookie() const {
        return "Session-ID=" + sessionId + "; " + "Max-Age=" + std::to_string(maxAge) + "; " + "Path=" + path + "; HttpOnly";
    }
};

#endif