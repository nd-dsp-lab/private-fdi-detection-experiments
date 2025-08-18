#pragma once
#include <string>

struct MeterReading {
    std::string device_id;
    double timestamp;
    double voltage;
    double current;
    double power;
    double frequency;
};
