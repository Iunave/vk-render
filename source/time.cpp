#include "time.hpp"
#include <ratio>
#include <cmath>
#include <cstdio>
#include <cassert>
#include "math.hpp"

double timespec2double(timespec time)
{
    return double(time.tv_sec) + (double(time.tv_nsec) / double(std::nano::den));
}

timespec double2timespec(double time)
{
    int64_t seconds = std::trunc(time);
    int64_t nanoseconds = std::round(math::frac(time) * std::nano::den);

    return timespec{seconds, nanoseconds};
}

double double_time_now()
{
    return timespec2double(timespec_time_now());
}

timespec timespec_time_now()
{
    timespec res;
    if(clock_gettime(CLOCK_MONOTONIC, &res) == -1)
    {
        syscall_error("clock_gettime");
    }
    return res;
}

void time_sleep(timespec time)
{
    if(nanosleep(&time, nullptr) == -1)
    {
        syscall_error("nanosleep");
    }
}

timespec operator+(timespec lhs, timespec rhs)
{
    lhs.tv_sec += rhs.tv_sec;
    lhs.tv_nsec += rhs.tv_nsec;

    if(lhs.tv_nsec < 0)
    {
        lhs.tv_sec -= 1;
        lhs.tv_nsec += std::nano::den;
    }
    else if(lhs.tv_nsec >= std::nano::den)
    {
        lhs.tv_sec += 1;
        lhs.tv_nsec -= std::nano::den;
    }

    return lhs;
}

//just dont input a negative.....
timespec operator*(timespec lhs, double scalar)
{
    static_assert(std::is_signed_v<__time_t> && std::is_signed_v<__syscall_slong_t> && std::is_signed_v<decltype(std::nano::den)>);

    double sec = double(lhs.tv_sec) * scalar;
    double nsec = double(lhs.tv_nsec) * scalar;
    double sec_whole = std::floor(sec);
    double sec_frac = sec - sec_whole;
    double nsec_discarded = sec_frac * double(std::nano::den);

    lhs.tv_sec = __time_t(sec_whole);
    lhs.tv_nsec = __syscall_slong_t(std::round(nsec) + nsec_discarded);

    //todo can we come up with a smarter solution here...
    while(lhs.tv_nsec < 0)
    {
        lhs.tv_sec -= 1;
        lhs.tv_nsec += std::nano::den;
    }
    while(lhs.tv_nsec >= std::nano::den)
    {
        lhs.tv_sec += 1;
        lhs.tv_nsec -= std::nano::den;
    }

    return lhs;
}
