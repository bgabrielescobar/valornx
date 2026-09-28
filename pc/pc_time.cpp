#include <chrono>

#include "../source/core/fsutil.hpp"

namespace vnx {
uint64_t nowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace vnx
