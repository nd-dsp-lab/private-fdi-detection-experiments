#pragma once
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

class Logger {
public:
    static std::string timestamp();
    static void info(const std::string& msg);
    static void success(const std::string& msg);
    static void warning(const std::string& msg);
    static void error(const std::string& msg);
    static void alert(const std::string& msg);
    static void sum_result(const std::string& msg);
};
