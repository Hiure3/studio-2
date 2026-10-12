#include "TestFramework.h"
#include <cstring>
int main(int argc, char** argv) {
    int failedTests = 0, ran = 0; const char* filter = argc > 1 ? argv[1] : nullptr;
    for (auto& t : registry()) {
        if (filter && !std::strstr(t.name.c_str(), filter)) continue;
        int before = failCount(); double t0 = tu::nowSec(); t.fn(); ++ran; bool ok = failCount() == before; if (!ok) ++failedTests;
        std::printf("[%s] %-48s %.2fs\n", ok ? " PASS " : " FAIL ", t.name.c_str(), tu::nowSec() - t0);
    }
    std::printf("\n%d tests, %d failed, %d checks, %d failed checks\n", ran, failedTests, checkCount(), failCount());
    return failedTests ? 1 : 0;
}
