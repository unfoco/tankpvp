#pragma once

#include <chrono>

namespace util {

inline auto now() -> double {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}
