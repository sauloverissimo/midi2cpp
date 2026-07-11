// tests/test_common.h
// DIY assert macros + TX capture hook for host-side tests.

#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) std::printf("  %-60s ", name)
#define PASS()     do { std::printf("PASS\n"); g_passed++; } while (0)
#define FAIL(msg)  do { std::printf("FAIL: %s\n", msg); g_failed++; return; } while (0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); } } while (0)
#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::printf("FAIL: %s (got %lu, want %lu)\n", msg, \
                    (unsigned long)(a), (unsigned long)(b)); \
        g_failed++; \
        return; \
    } \
} while (0)

// TX capture: tests install a WriteFn that writes intercepted UMP to
// g_captured_tx. Same shape as a real platform binding (Device::WriteFn ==
// std::function<void(const uint32_t*, size_t)>), tests prove the contract
// the platforms also use.
constexpr size_t CAPTURE_MAX = 512;
extern uint32_t g_captured_tx[CAPTURE_MAX];
extern size_t   g_captured_tx_len;

inline void capture_reset() {
    g_captured_tx_len = 0;
    std::memset(g_captured_tx, 0, sizeof(g_captured_tx));
}

inline void capture_write(const uint32_t* words, size_t n) {
    for (size_t i = 0; i < n && g_captured_tx_len < CAPTURE_MAX; ++i) {
        g_captured_tx[g_captured_tx_len++] = words[i];
    }
}

// Test clock: tests set g_test_now_ms before each task() to drive the JR
// heartbeat. Wired via Device::setNowFn(test_now_fn).
extern uint32_t g_test_now_ms;
inline uint32_t test_now_fn() { return g_test_now_ms; }
inline void     test_set_now(uint32_t ms) { g_test_now_ms = ms; }

#define REPORT_AND_EXIT() do { \
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed); \
    return g_failed == 0 ? 0 : 1; \
} while (0)
