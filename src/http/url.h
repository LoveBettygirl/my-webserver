#ifndef URL_H
#define URL_H

#include <iostream>
#include <string>
#include <cstdio>
#include <cassert>
#include <cctype>

unsigned char toHex(unsigned char x);
unsigned char fromHex(unsigned char x);
std::string urlEncode(const std::string &str);
std::string urlDecode(const std::string &str);

#endif