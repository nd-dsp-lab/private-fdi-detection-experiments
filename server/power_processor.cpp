#include "power_processor.hpp"
#include "logger.hpp"
#include <sstream>
#include <iomanip>

PowerSumProcessor::PowerSumProcessor(size_t count) : target_count(count) {
    power_readings.reserve(target_count);
    Logger::info("PowerSumProcessor initialized - will sum every " + std::to_string(target_count) + " readings");
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

        std::stringstream ss;
        ss << "Sum of " << target_count << " power readings: "
           << std::fixed << std::setprecision(2) << sum << " WATTS";
        Logger::sum_result(ss.str());

        power_readings.clear();
        current_count = 0;
        return true;
    }
    return false;
}
