#include "logger.hpp"

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
    return ss.str();
}

void Logger::info(const std::string& msg) {
    std::cout << CYAN "[" << timestamp() << "] [INFO]" RESET " " << msg << std::endl;
}

void Logger::success(const std::string& msg) {
    std::cout << GREEN "[" << timestamp() << "] [OK]" RESET " " << msg << std::endl;
}

void Logger::warning(const std::string& msg) {
    std::cout << YELLOW "[" << timestamp() << "] [WARN]" RESET " " << msg << std::endl;
}

void Logger::error(const std::string& msg) {
    std::cout << RED "[" << timestamp() << "] [ERROR]" RESET " " << msg << std::endl;
}

void Logger::alert(const std::string& msg) {
    std::cout << RED BOLD "[" << timestamp() << "] [ALERT]" RESET " " << msg << std::endl;
}

void Logger::sum_result(const std::string& msg) {
    std::cout << GREEN BOLD "[" << timestamp() << "] [SUM]" RESET " " << msg << std::endl;
}
