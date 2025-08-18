// server/power_processor.hpp
#pragma once

#include <vector>
#include <mutex>
#include <cstddef>

class PowerSumProcessor {
private:
    size_t target_count;
    std::vector<double> power_readings;
    std::mutex readings_mutex;
    size_t current_count{0};

public:
    explicit PowerSumProcessor(size_t count);
    bool add_reading(double power);
};
