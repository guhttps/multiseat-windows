#include <roapi.h>
namespace rds_lab { bool run_self_tests(); }
int main() {
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 1;
    const bool okay = rds_lab::run_self_tests();
    RoUninitialize();
    return okay ? 0 : 1;
}
