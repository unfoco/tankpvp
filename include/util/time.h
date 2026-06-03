#pragma once

#include <chrono>

namespace util {

inline double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}
