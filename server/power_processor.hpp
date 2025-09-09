// server/power_processor.hpp
#pragma once

#include <vector>
#include <mutex>
#include <cstddef>
#include <functional>

class PowerSumProcessor {
private:
    size_t target_count;
    std::vector<double> power_readings;
    std::mutex readings_mutex;
    size_t current_count{0};
    size_t total_sums{0};

    // Callback for when benchmark target is reached
    std::function<void()> benchmark_complete_callback;
    size_t benchmark_sum_target{0};

public:
    explicit PowerSumProcessor(size_t count);
    bool add_reading(double power);
    void set_benchmark_target(size_t target_sums, std::function<void()> callback);
    size_t get_total_sums() const { return total_sums; }
};
