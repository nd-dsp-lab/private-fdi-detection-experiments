// server/power_processor.cpp
#include "power_processor.hpp"
#include "logger.hpp"
#include <sstream>
#include <iomanip>

PowerSumProcessor::PowerSumProcessor(size_t count) : target_count(count) {
    power_readings.reserve(target_count);
    Logger::info("PowerSumProcessor initialized - will sum every " + std::to_string(target_count) + " readings");
}

void PowerSumProcessor::set_benchmark_target(size_t target_sums, std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(readings_mutex);
    benchmark_sum_target = target_sums;
    benchmark_complete_callback = callback;
    Logger::info("Benchmark target set: " + std::to_string(target_sums) + " power summations");
}

bool PowerSumProcessor::add_reading(double power) {
    std::lock_guard<std::mutex> lock(readings_mutex);

    power_readings.push_back(power);
    size_t count = ++current_count;

    if (count >= target_count) {
        double sum = 0.0;
        for (double p : power_readings) {
            sum += p;
        }

        total_sums++;

        std::stringstream ss;
        ss << "Sum " << total_sums << " of " << target_count << " power readings: "
           << std::fixed << std::setprecision(2) << sum << " WATTS";
        Logger::sum_result(ss.str());

        power_readings.clear();
        current_count = 0;

        // Check if benchmark target reached
        if (benchmark_sum_target > 0 && total_sums >= benchmark_sum_target && benchmark_complete_callback) {
            benchmark_complete_callback();
        }

        return true;
    }
    return false;
}
