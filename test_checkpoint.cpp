/*
 * Checkpoint Test Suite  (C++17)
 *
 * Tests malloc correctness only — free() does NOT need to work.
 *
 * Usage:
 *   ./test_checkpoint        — run all tests, print summary
 *   ./test_checkpoint <N>    — run only test N (1-indexed), exit 0=pass 1=fail
 *                              (used by the GitHub Classroom autograder)
 */

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cstring>
#include <iomanip>
#include <cstdint>
#include <cstddef>
#include "allocator.h"
#include "memlib.h"

// ─────────────────────────────────────────────
// Minimal test framework
// ─────────────────────────────────────────────

struct TestResult {
    std::string name;
    bool        passed = false;
    std::string failure_msg;
};

// All registered test cases
static std::vector<std::pair<std::string, std::function<TestResult()>>> g_tests;

// Register a test
static void register_test(const std::string &name,
                           std::function<TestResult()> fn) {
    g_tests.emplace_back(name, std::move(fn));
}

// Convenience: make a passing result
static TestResult pass(const std::string &name) {
    return { name, true, "" };
}

// Convenience: make a failing result
static TestResult fail(const std::string &name, const std::string &msg) {
    return { name, false, msg };
}

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static bool is_aligned(const void *ptr) {
    return (reinterpret_cast<uintptr_t>(ptr) % 8) == 0;
}

// Reinitialise the allocator between independent test runs
static bool reset_allocator() {
    mem_deinit();
    mem_init();
    return mm_init() == 0;
}

// ─────────────────────────────────────────────
// Test definitions
// ─────────────────────────────────────────────

// Test 1 — Single allocation
static TestResult test_single_alloc() {
    const std::string name = "Single allocation";
    void *ptr = mm_malloc(8);

    if (ptr == nullptr)
        return fail(name, "malloc returned nullptr — check mm_init and extend_heap");
    if (!is_aligned(ptr))
        return fail(name, "returned pointer is not 8-byte aligned — check your size rounding");

    // Write and read back
    auto *p = static_cast<int *>(ptr);
    *p = 42;
    if (*p != 42)
        return fail(name, "cannot write/read from allocated memory — header may be corrupt");

    return pass(name);
}

// Test 2 — Multiple independent small allocations
static TestResult test_multiple_small_allocs() {
    const std::string name = "Multiple small allocations";
    constexpr int N = 10;
    void *ptrs[N];

    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(8);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc returned nullptr — may have run out of heap space");
        if (!is_aligned(ptrs[i]))
            return fail(name, "returned pointer is not 8-byte aligned");

        *static_cast<int *>(ptrs[i]) = i * 100;
    }

    for (int i = 0; i < N; ++i) {
        if (*static_cast<int *>(ptrs[i]) != i * 100)
            return fail(name, "data corruption — a later allocation overwrote an earlier block");
    }

    return pass(name);
}

// Test 3 — Range of allocation sizes
static TestResult test_various_sizes() {
    const std::string name = "Various allocation sizes";
    constexpr size_t sizes[] = { 1, 8, 16, 32, 64, 128, 256, 512, 1024 };
    constexpr int    N       = sizeof(sizes) / sizeof(sizes[0]);
    void *ptrs[N];

    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(sizes[i]);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc returned nullptr — check size-rounding and extend_heap");
        if (!is_aligned(ptrs[i]))
            return fail(name, "returned pointer is not 8-byte aligned");
        std::memset(ptrs[i], i, sizes[i]);
    }

    for (int i = 0; i < N; ++i) {
        auto *p = static_cast<unsigned char *>(ptrs[i]);
        for (size_t j = 0; j < sizes[i]; ++j) {
            if (p[j] != static_cast<unsigned char>(i))
                return fail(name, "data corruption — blocks are overlapping or too small");
        }
    }

    return pass(name);
}

// Test 4 — 1 MB allocation
static TestResult test_large_alloc() {
    const std::string name = "Large allocation (1 MB)";
    void *ptr = mm_malloc(1024 * 1024);

    if (ptr == nullptr)
        return fail(name, "malloc returned nullptr for 1 MB — check extend_heap loop");
    if (!is_aligned(ptr))
        return fail(name, "returned pointer is not 8-byte aligned");

    auto *p = static_cast<int *>(ptr);
    p[0]      = 1;
    p[1000]   = 2;
    p[262143] = 3;

    if (p[0] != 1 || p[1000] != 2 || p[262143] != 3)
        return fail(name, "data corruption in large allocation — block may be too small");

    return pass(name);
}

// Test 5 — malloc(0) must return nullptr
static TestResult test_zero_size() {
    const std::string name = "Zero-size allocation returns nullptr";
    void *ptr = mm_malloc(0);

    if (ptr != nullptr)
        return fail(name, "malloc(0) should return nullptr per the spec");

    return pass(name);
}

// Test 6 — 100 consecutive fixed-size allocations
static TestResult test_sequential_stress() {
    const std::string name = "Sequential allocations (100 blocks of 32 B)";
    constexpr int N = 100;
    void *ptrs[N];

    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(32);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc failed — heap may not be growing correctly");

        auto *p = static_cast<int *>(ptrs[i]);
        p[0] = i;
        p[1] = i * 2;
    }

    for (int i = 0; i < N; ++i) {
        auto *p = static_cast<int *>(ptrs[i]);
        if (p[0] != i || p[1] != i * 2)
            return fail(name, "data corruption — blocks may be overlapping");
    }

    return pass(name);
}

// Test 7 — Alternating small and large allocations
static TestResult test_alternating_sizes() {
    const std::string name = "Alternating small (8 B) and large (512 B) allocations";
    constexpr int N = 20;
    void *ptrs[N];

    for (int i = 0; i < N; ++i) {
        size_t sz = (i % 2 == 0) ? 8 : 512;
        ptrs[i] = mm_malloc(sz);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc returned nullptr — check alignment rounding");
        if (!is_aligned(ptrs[i]))
            return fail(name, "returned pointer is not 8-byte aligned");
        std::memset(ptrs[i], i, sz);
    }

    for (int i = 0; i < N; ++i) {
        size_t sz = (i % 2 == 0) ? 8 : 512;
        auto  *p  = static_cast<unsigned char *>(ptrs[i]);
        for (size_t j = 0; j < sz; ++j) {
            if (p[j] != static_cast<unsigned char>(i))
                return fail(name, "data corruption — adjacent blocks may be overlapping");
        }
    }

    return pass(name);
}

// Test 8 — 4 MB allocation (requires extend_heap to loop or request large chunks)
static TestResult test_very_large_alloc() {
    const std::string name = "Very large allocation (4 MB)";
    void *ptr = mm_malloc(4 * 1024 * 1024);

    if (ptr == nullptr)
        return fail(name, "malloc returned nullptr for 4 MB — extend_heap may not request enough");

    auto  *p         = static_cast<uint64_t *>(ptr);
    size_t num_words = (4 * 1024 * 1024) / sizeof(uint64_t);

    p[0]             = 0x123456789ABCDEF0ULL;
    p[num_words / 2] = 0xFEDCBA9876543210ULL;
    p[num_words - 1] = 0xAAAABBBBCCCCDDDDULL;

    if (p[0]             != 0x123456789ABCDEF0ULL ||
        p[num_words / 2] != 0xFEDCBA9876543210ULL ||
        p[num_words - 1] != 0xAAAABBBBCCCCDDDDULL)
        return fail(name, "data corruption in 4 MB block — block boundary may be wrong");

    return pass(name);
}

// ─────────────────────────────────────────────
// Registration + main
// ─────────────────────────────────────────────

static void register_all() {
    register_test("Single allocation",                            test_single_alloc);
    register_test("Multiple small allocations",                   test_multiple_small_allocs);
    register_test("Various allocation sizes",                     test_various_sizes);
    register_test("Large allocation (1 MB)",                      test_large_alloc);
    register_test("Zero-size allocation returns nullptr",         test_zero_size);
    register_test("Sequential allocations (100 blocks)",         test_sequential_stress);
    register_test("Alternating small and large allocations",     test_alternating_sizes);
    register_test("Very large allocation (4 MB)",                test_very_large_alloc);
}

int main(int argc, char *argv[]) {
    register_all();

    // ── Single-test mode (used by GitHub Classroom autograder) ──────────────
    if (argc == 2) {
        int n = std::stoi(argv[1]);
        if (n < 1 || n > static_cast<int>(g_tests.size())) {
            std::cerr << "Test number out of range (1-" << g_tests.size() << ")\n";
            return 2;
        }
        mem_init();
        if (mm_init() != 0) {
            std::cerr << "FAIL: mm_init() returned non-zero\n";
            return 1;
        }
        auto &[tname, fn] = g_tests[n - 1];
        TestResult r = fn();
        if (r.passed) {
            std::cout << "PASS: " << tname << "\n";
            mem_deinit();
            return 0;
        } else {
            std::cout << "FAIL: " << tname << "\n";
            std::cout << "  Hint: " << r.failure_msg << "\n";
            mem_deinit();
            return 1;
        }
    }

    // ── Full-suite mode ──────────────────────────────────────────────────────
    std::cout << "============================================\n";
    std::cout << "  CHECKPOINT TEST SUITE\n";
    std::cout << "  malloc correctness  (free not required)\n";
    std::cout << "============================================\n\n";

    int passed = 0;
    int total  = static_cast<int>(g_tests.size());

    for (int i = 0; i < total; ++i) {
        // Fresh allocator state per test
        if (!reset_allocator()) {
            std::cout << "  [" << std::setw(2) << (i + 1) << "] "
                      << g_tests[i].first << "\n"
                      << "       FAIL: mm_init() returned non-zero\n";
            continue;
        }

        std::cout << "  [" << std::setw(2) << (i + 1) << "] "
                  << std::left << std::setw(50) << g_tests[i].first;

        TestResult r = g_tests[i].second();
        if (r.passed) {
            std::cout << "  PASS\n";
            ++passed;
        } else {
            std::cout << "  FAIL\n";
            std::cout << "       Hint: " << r.failure_msg << "\n";
        }
    }

    std::cout << "\n============================================\n";
    std::cout << "  Result: " << passed << "/" << total << " tests passed\n";
    std::cout << "============================================\n";

    if (passed == total) {
        std::cout << "\nAll checkpoint tests passed!\n"
                  << "Reminder: free() is NOT required for checkpoint.\n";
        mem_deinit();
        return 0;
    } else {
        std::cout << "\nSome tests failed — keep debugging!\n"
                  << "Run  ./test_checkpoint <N>  to isolate a single test.\n";
        mem_deinit();
        return 1;
    }
}
