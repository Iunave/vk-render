#ifndef CHEEMSIT_GUI_VK_TIME_HPP
#define CHEEMSIT_GUI_VK_TIME_HPP

#include "vk-render.hpp"
#include <cstdint>
#include <ctime>
#include <numbers>

struct program_time_t
{
    //timespec min_frametime{0, 16666666};
    timespec min_frametime{0, 13333333};
    //timespec min_frametime{0, 0};
    timespec frame_start{0, 0};
    timespec frame_end{0, 0};
    timespec total{0, 0};
    timespec delta{0, 0};
    double fp_delta = 0.0;
    double dilation = 1.0;
    uint64_t frame_count = 0;
};
inline constinit program_time_t program_time{};

double timespec2double(timespec time);
timespec double2timespec(double time);
timespec timespec_time_now();
double double_time_now();
void time_sleep(timespec time);

timespec operator+(timespec lhs, timespec rhs);
timespec operator*(timespec lhs, double scalar);

inline timespec& operator+=(timespec& lhs, timespec rhs)
{
    lhs = lhs + rhs;
    return lhs;
}

inline timespec operator-(timespec lhs, timespec rhs)
{
    return lhs + timespec{-rhs.tv_sec, -rhs.tv_nsec};
}

inline timespec& operator-=(timespec& lhs, timespec rhs)
{
    lhs = lhs - rhs;
    return lhs;
}

inline timespec& operator*=(timespec& lhs, double scalar)
{
    lhs = lhs * scalar;
    return lhs;
}

#endif //CHEEMSIT_GUI_VK_TIME_HPP
