#include "cha.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <x86intrin.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>


uint64_t compute_perm(uintptr_t physical_address) {
    // SPDLOG_TRACE("0b{:064b} (0x{:016x}) --> physical address, input to compute_perm func.", physical_address,
    // physical_address);

    // static const uint64_t SelectorMasks[14] = {0x4c8fc0000, 0x1d05380000, 0x262b8c0000, 0x41f500000, 0x2c6d780000,
    //                                            0x2cd5140000, 0x21d80c0000, 0x3b3f480000, 0x3a03500000, 0x3033280000,
    //                                            0x0, 0x1469b40000, 0x0, 0x0};

    static const uint64_t SelectorMasks[14] = {0x32770c0000, 0x3433d40000, 0x39a2900000, 0x3857680000, 0x1ad2880000,
                                               0x1a6ae40000, 0x2b2fc40000, 0x24b6540000, 0x3a03500000, 0xc7b100000,
                                               0xaf7c80000,  0x28218c0000, 0x0,          0x0};

    uint64_t computed_perm = 0;

    for (int bit = 0; bit < 14; ++bit) {
        auto permutation_selector_mask = SelectorMasks[bit];
        // SPDLOG_TRACE("Selector mask: 0b{:b} --> binary; 0x{:x} --> hexadecimal", permutation_selector_mask,
        // permutation_selector_mask);

        uint64_t k = permutation_selector_mask & physical_address;  // bitwise AND with mask
        // SPDLOG_TRACE("will AND below 2 numbers.");
        // SPDLOG_TRACE("0b{:064b} (0x{:016x}) -> permutation_selector_mask", permutation_selector_mask,
        // permutation_selector_mask); SPDLOG_TRACE("0b{:064b} (0x{:016x}) -> physical_address", physical_address,
        // physical_address); SPDLOG_TRACE("0b{:064b} (0x{:016x}) -> AND (&) result.", k, k);

        uint64_t j = __builtin_popcountl(k);  // count number of bits set
        // SPDLOG_TRACE("Number of bits in 0b{:b} : {}", k, j);

        uint64_t i = j % 2;  // compute parity
        // SPDLOG_TRACE("Parity of 0b{:b}: {}", j, i);

        computed_perm += (i << bit);  // scale and accumulate
        // SPDLOG_TRACE("computed permutation += Parity ({}) << {} --> 0b{:b}", i, bit, computed_perm);
    }

    // SPDLOG_TRACE("Computed permutation for physical address 0x{:x}: {}", physical_address, computed_perm);

    return (computed_perm);  /// why parentheses around variable here?
}

uintptr_t getPhysicalAddress(uintptr_t virtual_address) {
    const pid_t pid = getpid();

    uintptr_t physical_address = 0;

    // SPDLOG_TRACE("getting physical address for virtual address (0x{:016x})", virtual_address);

    if (virt_to_phys_user(&physical_address, pid, virtual_address)) {
        // SPDLOG_ERROR("error: virt_to_phys_user");
        return EXIT_FAILURE;
    };

    return physical_address;
}

/// it is important to get the pointer by reference so that we do not copy it here! Has trouble while working with space
/// allocated by mmap().
int findCHAByHashing(uintptr_t virtual_address) {
static const std::vector<int> base_sequence{ // for 28 core SKX, CLX
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 17, 16, 19, 18, 21, 20, 23, 22, 25, 24, 27, 26, 1,
    16, 11, 18, 18, 19, 16, 17, 22, 23, 20, 21, 26, 27, 24, 25, 18, 3,  16, 9,  3,  2,  1,  0,  7,  6,  5,  4,  11, 10,
    9,  8,  15, 14, 13, 12, 13, 12, 15, 14, 9,  8,  11, 10, 5,  4,  7,  6,  1,  0,  3,  2,  12, 13, 6,  7,  24, 25, 26,
    27, 20, 21, 22, 23, 16, 17, 18, 19, 15, 14, 5,  4,  27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 14, 15, 12, 13,
    10, 11, 8,  9,  6,  7,  4,  5,  2,  3,  0,  1,  22, 23, 20, 21, 18, 19, 16, 17, 14, 23, 4,  21, 26, 27, 24, 25, 7,
    6,  5,  4,  3,  2,  1,  0,  15, 14, 13, 12, 11, 10, 9,  8,  4,  5,  6,  7,  0,  1,  2,  3,  12, 13, 14, 15, 8,  9,
    10, 11, 21, 20, 23, 22, 17, 16, 19, 18, 21, 12, 23, 6,  25, 24, 27, 26, 27, 26, 25, 24, 3,  2,  9,  8,  19, 18, 17,
    16, 23, 22, 21, 20, 10, 11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,  7,  4,  5,  9,  8,  11, 10, 13, 12, 15, 14,
    1,  0,  3,  2,  5,  4,  7,  6,  24, 25, 26, 27, 0,  1,  10, 11, 16, 17, 18, 19, 20, 21, 22, 23, 1,  0,  3,  2,  5,
    4,  7,  6,  9,  8,  11, 10, 13, 12, 15, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 0,  1,  10, 11, 19, 18,
    17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 3,  2,  9,  8,  2,  3,  0,  1,  6,  7,  4,  5,  10, 11, 8,  9,  14, 15, 12,
    13, 12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,  21, 12, 23, 6,  25, 24, 27, 26, 21, 20, 23, 22,
    17, 16, 19, 18, 14, 23, 4,  21, 26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 15, 14, 13, 12, 11, 10, 9,  8,  7,
    6,  5,  4,  3,  2,  1,  0,  23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 5,  4,  27, 26, 25, 24, 6,  7,  4,  5,  2,  3,
    0,  1,  14, 15, 12, 13, 10, 11, 8,  9,  5,  4,  7,  6,  1,  0,  3,  2,  13, 12, 15, 14, 9,  8,  11, 10, 20, 21, 22,
    23, 16, 17, 18, 19, 12, 13, 6,  7,  24, 25, 26, 27, 26, 27, 24, 25, 18, 3,  16, 9,  18, 19, 16, 17, 22, 23, 20, 21,
    11, 10, 9,  8,  15, 14, 13, 12, 3,  2,  1,  0,  7,  6,  5,  4,  8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,
    5,  6,  7,  25, 24, 27, 26, 1,  16, 11, 18, 17, 16, 19, 18, 21, 20, 23, 22, 2,  3,  0,  1,  6,  7,  4,  5,  10, 11,
    8,  9,  14, 15, 12, 13, 19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 7,  22, 13, 20, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 20, 5,  22, 15, 1,  0,  3,  2,  5,  4,  7,  6,  9,  8,  11, 10, 13, 12, 15, 14, 15, 14, 13, 12,
    11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,  10, 11, 0,  1,  26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 9,
    16, 3,  18, 25, 24, 27, 26, 21, 20, 23, 22, 17, 16, 19, 18, 12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  0,  1,
    2,  3,  20, 21, 22, 23, 16, 17, 18, 19, 8,  17, 2,  19, 24, 25, 26, 27, 5,  4,  7,  6,  1,  0,  3,  2,  13, 12, 15,
    14, 9,  8,  11, 10, 6,  7,  4,  5,  2,  3,  0,  1,  14, 15, 12, 13, 10, 11, 8,  9,  23, 22, 21, 20, 19, 18, 17, 16,
    19, 10, 17, 0,  27, 26, 25, 24, 25, 24, 27, 26, 21, 4,  23, 14, 17, 16, 19, 18, 21, 20, 23, 22, 8,  9,  10, 11, 12,
    13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  11, 10, 9,  8,  15, 14, 13, 12, 3,  2,  1,  0,  7,  6,  5,  4,  26, 27,
    24, 25, 6,  23, 12, 21, 18, 19, 16, 17, 22, 23, 20, 21, 3,  2,  1,  0,  7,  6,  5,  4,  11, 10, 9,  8,  15, 14, 13,
    12, 18, 19, 16, 17, 22, 23, 20, 21, 26, 27, 24, 25, 6,  7,  12, 13, 17, 16, 19, 18, 21, 20, 23, 22, 25, 24, 27, 26,
    21, 4,  23, 14, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 14, 15, 12, 13, 10, 11, 8,  9,  6,
    7,  4,  5,  2,  3,  0,  1,  19, 10, 17, 0,  27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 8,  17, 2,  19, 24, 25,
    26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 13, 12, 15, 14, 9,  8,  11, 10, 5,  4,  7,  6,  1,  0,  3,  2,  21, 20, 23,
    22, 17, 16, 19, 18, 9,  16, 3,  18, 25, 24, 27, 26, 4,  5,  6,  7,  0,  1,  2,  3,  12, 13, 14, 15, 8,  9,  10, 11,
    7,  6,  5,  4,  3,  2,  1,  0,  15, 14, 13, 12, 11, 10, 9,  8,  22, 23, 20, 21, 18, 19, 16, 17, 18, 11, 16, 1,  26,
    27, 24, 25, 24, 25, 26, 27, 20, 5,  22, 15, 16, 17, 18, 19, 20, 21, 22, 23, 9,  8,  11, 10, 13, 12, 15, 14, 1,  0,
    3,  2,  5,  4,  7,  6,  10, 11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,  7,  4,  5,  27, 26, 25, 24, 7,  22, 13,
    20, 19, 18, 17, 16, 23, 22, 21, 20, 5,  4,  15, 14, 25, 24, 27, 26, 21, 20, 23, 22, 17, 16, 19, 18, 12, 13, 14, 15,
    8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,  15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,  6,
    7,  12, 13, 26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 8,  17,
    2,  19, 1,  0,  3,  2,  5,  4,  7,  6,  9,  8,  11, 10, 13, 12, 15, 14, 2,  3,  0,  1,  6,  7,  4,  5,  10, 11, 8,
    9,  14, 15, 12, 13, 19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 19, 10, 17, 0,  11, 10, 9,  8,  15, 14, 13, 12,
    3,  2,  1,  0,  7,  6,  5,  4,  26, 27, 24, 25, 10, 11, 0,  1,  18, 19, 16, 17, 22, 23, 20, 21, 25, 24, 27, 26, 9,
    8,  3,  2,  17, 16, 19, 18, 21, 20, 23, 22, 8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  6,  7,
    4,  5,  2,  3,  0,  1,  14, 15, 12, 13, 10, 11, 8,  9,  23, 22, 21, 20, 19, 18, 17, 16, 7,  22, 13, 20, 27, 26, 25,
    24, 20, 21, 22, 23, 16, 17, 18, 19, 20, 5,  22, 15, 24, 25, 26, 27, 5,  4,  7,  6,  1,  0,  3,  2,  13, 12, 15, 14,
    9,  8,  11, 10, 20, 5,  22, 15, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 13, 12, 15, 14, 9,  8,  11, 10, 5,
    4,  7,  6,  1,  0,  3,  2,  14, 15, 12, 13, 10, 11, 8,  9,  6,  7,  4,  5,  2,  3,  0,  1,  7,  22, 13, 20, 27, 26,
    25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 17, 16, 19, 18, 21, 20, 23, 22, 25, 24, 27, 26, 9,  8,  3,  2,  0,  1,  2,
    3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 3,  2,  1,  0,  7,  6,  5,  4,  11, 10, 9,  8,  15, 14, 13, 12,
    18, 19, 16, 17, 22, 23, 20, 21, 26, 27, 24, 25, 10, 11, 0,  1,  10, 11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,
    7,  4,  5,  27, 26, 25, 24, 19, 10, 17, 0,  19, 18, 17, 16, 23, 22, 21, 20, 24, 25, 26, 27, 8,  17, 2,  19, 16, 17,
    18, 19, 20, 21, 22, 23, 9,  8,  11, 10, 13, 12, 15, 14, 1,  0,  3,  2,  5,  4,  7,  6,  7,  6,  5,  4,  3,  2,  1,
    0,  15, 14, 13, 12, 11, 10, 9,  8,  22, 23, 20, 21, 18, 19, 16, 17, 6,  7,  12, 13, 26, 27, 24, 25, 21, 20, 23, 22,
    17, 16, 19, 18, 5,  4,  15, 14, 25, 24, 27, 26, 4,  5,  6,  7,  0,  1,  2,  3,  12, 13, 14, 15, 8,  9,  10, 11, 19,
    2,  17, 8,  27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 14, 15, 12, 13, 10, 11, 8,  9,  6,  7,  4,  5,  2,  3,
    0,  1,  13, 12, 15, 14, 9,  8,  11, 10, 5,  4,  7,  6,  1,  0,  3,  2,  0,  17, 10, 19, 24, 25, 26, 27, 20, 21, 22,
    23, 16, 17, 18, 19, 18, 19, 16, 17, 22, 23, 20, 21, 26, 27, 24, 25, 14, 23, 4,  21, 3,  2,  1,  0,  7,  6,  5,  4,
    11, 10, 9,  8,  15, 14, 13, 12, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 17, 16, 19, 18, 21,
    20, 23, 22, 25, 24, 27, 26, 21, 12, 23, 6,  9,  8,  11, 10, 13, 12, 15, 14, 1,  0,  3,  2,  5,  4,  7,  6,  24, 25,
    26, 27, 20, 13, 22, 7,  16, 17, 18, 19, 20, 21, 22, 23, 27, 26, 25, 24, 15, 14, 5,  4,  19, 18, 17, 16, 23, 22, 21,
    20, 10, 11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,  7,  4,  5,  4,  5,  6,  7,  0,  1,  2,  3,  12, 13, 14, 15,
    8,  9,  10, 11, 21, 20, 23, 22, 17, 16, 19, 18, 1,  16, 11, 18, 25, 24, 27, 26, 22, 23, 20, 21, 18, 19, 16, 17, 18,
    3,  16, 9,  26, 27, 24, 25, 7,  6,  5,  4,  3,  2,  1,  0,  15, 14, 13, 12, 11, 10, 9,  8,  18, 3,  16, 9,  26, 27,
    24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,  12, 13, 14,
    15, 8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,  1,  16, 11, 18, 25, 24, 27, 26, 21, 20, 23, 22, 17, 16, 19, 18,
    19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 15, 22, 5,  20, 2,  3,  0,  1,  6,  7,  4,  5,  10, 11, 8,  9,  14,
    15, 12, 13, 1,  0,  3,  2,  5,  4,  7,  6,  9,  8,  11, 10, 13, 12, 15, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 20, 13, 22, 7,  8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  25, 24, 27, 26, 21, 12, 23,
    6,  17, 16, 19, 18, 21, 20, 23, 22, 26, 27, 24, 25, 14, 23, 4,  21, 18, 19, 16, 17, 22, 23, 20, 21, 11, 10, 9,  8,
    15, 14, 13, 12, 3,  2,  1,  0,  7,  6,  5,  4,  5,  4,  7,  6,  1,  0,  3,  2,  13, 12, 15, 14, 9,  8,  11, 10, 20,
    21, 22, 23, 16, 17, 18, 19, 0,  17, 10, 19, 24, 25, 26, 27, 23, 22, 21, 20, 19, 18, 17, 16, 3,  2,  9,  8,  27, 26,
    25, 24, 6,  7,  4,  5,  2,  3,  0,  1,  14, 15, 12, 13, 10, 11, 8,  9,  27, 26, 25, 24, 15, 22, 5,  20, 19, 18, 17,
    16, 23, 22, 21, 20, 10, 11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,  7,  4,  5,  9,  8,  11, 10, 13, 12, 15, 14,
    1,  0,  3,  2,  5,  4,  7,  6,  24, 25, 26, 27, 20, 13, 22, 7,  16, 17, 18, 19, 20, 21, 22, 23, 22, 23, 20, 21, 18,
    19, 16, 17, 18, 3,  16, 9,  26, 27, 24, 25, 7,  6,  5,  4,  3,  2,  1,  0,  15, 14, 13, 12, 11, 10, 9,  8,  4,  5,
    6,  7,  0,  1,  2,  3,  12, 13, 14, 15, 8,  9,  10, 11, 21, 20, 23, 22, 17, 16, 19, 18, 1,  16, 11, 18, 25, 24, 27,
    26, 13, 12, 15, 14, 9,  8,  11, 10, 5,  4,  7,  6,  1,  0,  3,  2,  0,  17, 10, 19, 24, 25, 26, 27, 20, 21, 22, 23,
    16, 17, 18, 19, 19, 2,  17, 8,  27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 14, 15, 12, 13, 10, 11, 8,  9,  6,
    7,  4,  5,  2,  3,  0,  1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 17, 16, 19, 18, 21, 20,
    23, 22, 25, 24, 27, 26, 21, 12, 23, 6,  18, 19, 16, 17, 22, 23, 20, 21, 26, 27, 24, 25, 14, 15, 4,  5,  3,  2,  1,
    0,  7,  6,  5,  4,  11, 10, 9,  8,  15, 14, 13, 12, 26, 27, 24, 25, 14, 23, 4,  21, 18, 19, 16, 17, 22, 23, 20, 21,
    11, 10, 9,  8,  15, 14, 13, 12, 3,  2,  1,  0,  7,  6,  5,  4,  8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,
    5,  6,  7,  25, 24, 27, 26, 21, 12, 23, 6,  17, 16, 19, 18, 21, 20, 23, 22, 23, 22, 21, 20, 19, 18, 17, 16, 19, 2,
    17, 8,  27, 26, 25, 24, 6,  7,  4,  5,  2,  3,  0,  1,  14, 15, 12, 13, 10, 11, 8,  9,  5,  4,  7,  6,  1,  0,  3,
    2,  13, 12, 15, 14, 9,  8,  11, 10, 20, 21, 22, 23, 16, 17, 18, 19, 0,  17, 10, 19, 24, 25, 26, 27, 12, 13, 14, 15,
    8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,  1,  16, 11, 18, 25, 24, 27, 26, 21, 20, 23, 22, 17, 16, 19, 18, 2,
    3,  8,  9,  26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,
    1,  0,  1,  0,  3,  2,  5,  4,  7,  6,  9,  8,  11, 10, 13, 12, 15, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 20, 13, 22, 7,  19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 15, 22, 5,  20, 2,  3,  0,  1,  6,  7,  4,  5,
    10, 11, 8,  9,  14, 15, 12, 13, 25, 24, 27, 26, 9,  16, 3,  18, 17, 16, 19, 18, 21, 20, 23, 22, 8,  9,  10, 11, 12,
    13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  11, 10, 9,  8,  15, 14, 13, 12, 3,  2,  1,  0,  7,  6,  5,  4,  26, 27,
    24, 25, 18, 11, 16, 1,  18, 19, 16, 17, 22, 23, 20, 21, 20, 21, 22, 23, 16, 17, 18, 19, 4,  5,  14, 15, 24, 25, 26,
    27, 5,  4,  7,  6,  1,  0,  3,  2,  13, 12, 15, 14, 9,  8,  11, 10, 6,  7,  4,  5,  2,  3,  0,  1,  14, 15, 12, 13,
    10, 11, 8,  9,  23, 22, 21, 20, 19, 18, 17, 16, 7,  6,  13, 12, 27, 26, 25, 24, 15, 14, 13, 12, 11, 10, 9,  8,  7,
    6,  5,  4,  3,  2,  1,  0,  6,  23, 12, 21, 26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 21, 4,  23, 14, 25, 24,
    27, 26, 21, 20, 23, 22, 17, 16, 19, 18, 12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,  2,  3,  0,
    1,  6,  7,  4,  5,  10, 11, 8,  9,  14, 15, 12, 13, 19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 11, 10, 1,  0,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 8,  9,  2,  3,  1,  0,  3,  2,  5,  4,  7,  6,  9,  8,  11, 10, 13,
    12, 15, 14, 24, 25, 26, 27, 8,  9,  2,  3,  16, 17, 18, 19, 20, 21, 22, 23, 9,  8,  11, 10, 13, 12, 15, 14, 1,  0,
    3,  2,  5,  4,  7,  6,  10, 11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,  7,  4,  5,  27, 26, 25, 24, 11, 10, 1,
    0,  19, 18, 17, 16, 23, 22, 21, 20, 21, 20, 23, 22, 17, 16, 19, 18, 21, 4,  23, 14, 25, 24, 27, 26, 4,  5,  6,  7,
    0,  1,  2,  3,  12, 13, 14, 15, 8,  9,  10, 11, 7,  6,  5,  4,  3,  2,  1,  0,  15, 14, 13, 12, 11, 10, 9,  8,  22,
    23, 20, 21, 18, 19, 16, 17, 6,  23, 12, 21, 26, 27, 24, 25, 14, 15, 12, 13, 10, 11, 8,  9,  6,  7,  4,  5,  2,  3,
    0,  1,  7,  6,  13, 12, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 4,  5,  14, 15, 24, 25, 26, 27, 20, 21, 22,
    23, 16, 17, 18, 19, 13, 12, 15, 14, 9,  8,  11, 10, 5,  4,  7,  6,  1,  0,  3,  2,  3,  2,  1,  0,  7,  6,  5,  4,
    11, 10, 9,  8,  15, 14, 13, 12, 18, 19, 16, 17, 22, 23, 20, 21, 26, 27, 24, 25, 18, 11, 16, 1,  17, 16, 19, 18, 21,
    20, 23, 22, 25, 24, 27, 26, 9,  16, 3,  18, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 6,  7,
    4,  5,  2,  3,  0,  1,  14, 15, 12, 13, 10, 11, 8,  9,  23, 22, 21, 20, 19, 18, 17, 16, 11, 10, 1,  0,  27, 26, 25,
    24, 20, 21, 22, 23, 16, 17, 18, 19, 8,  17, 2,  19, 24, 25, 26, 27, 5,  4,  7,  6,  1,  0,  3,  2,  13, 12, 15, 14,
    9,  8,  11, 10, 11, 10, 9,  8,  15, 14, 13, 12, 3,  2,  1,  0,  7,  6,  5,  4,  26, 27, 24, 25, 6,  23, 12, 21, 18,
    19, 16, 17, 22, 23, 20, 21, 25, 24, 27, 26, 21, 4,  23, 14, 17, 16, 19, 18, 21, 20, 23, 22, 8,  9,  10, 11, 12, 13,
    14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 20, 5,  22, 15, 1,  0,  3,
    2,  5,  4,  7,  6,  9,  8,  11, 10, 13, 12, 15, 14, 2,  3,  0,  1,  6,  7,  4,  5,  10, 11, 8,  9,  14, 15, 12, 13,
    19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 7,  22, 13, 20, 9,  16, 3,  18, 25, 24, 27, 26, 21, 20, 23, 22, 17,
    16, 19, 18, 12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,  15, 14, 13, 12, 11, 10, 9,  8,  7,  6,
    5,  4,  3,  2,  1,  0,  18, 11, 16, 1,  26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 7,  6,  5,  4,  3,  2,  1,
    0,  15, 14, 13, 12, 11, 10, 9,  8,  22, 23, 20, 21, 18, 19, 16, 17, 18, 11, 16, 1,  26, 27, 24, 25, 21, 20, 23, 22,
    17, 16, 19, 18, 9,  16, 3,  18, 25, 24, 27, 26, 4,  5,  6,  7,  0,  1,  2,  3,  12, 13, 14, 15, 8,  9,  10, 11, 10,
    11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,  7,  4,  5,  27, 26, 25, 24, 7,  6,  13, 12, 19, 18, 17, 16, 23, 22,
    21, 20, 24, 25, 26, 27, 20, 5,  22, 15, 16, 17, 18, 19, 20, 21, 22, 23, 9,  8,  11, 10, 13, 12, 15, 14, 1,  0,  3,
    2,  5,  4,  7,  6,  17, 16, 19, 18, 21, 20, 23, 22, 25, 24, 27, 26, 21, 4,  23, 14, 0,  1,  2,  3,  4,  5,  6,  7,
    8,  9,  10, 11, 12, 13, 14, 15, 3,  2,  1,  0,  7,  6,  5,  4,  11, 10, 9,  8,  15, 14, 13, 12, 18, 19, 16, 17, 22,
    23, 20, 21, 26, 27, 24, 25, 6,  23, 12, 21, 8,  17, 2,  19, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 13, 12,
    15, 14, 9,  8,  11, 10, 5,  4,  7,  6,  1,  0,  3,  2,  14, 15, 12, 13, 10, 11, 8,  9,  6,  7,  4,  5,  2,  3,  0,
    1,  19, 10, 17, 0,  27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 4,  5,  6,  7,  0,  1,  2,  3,  12, 13, 14, 15,
    8,  9,  10, 11, 21, 20, 23, 22, 17, 16, 19, 18, 13, 12, 7,  6,  25, 24, 27, 26, 22, 23, 20, 21, 18, 19, 16, 17, 14,
    15, 4,  5,  26, 27, 24, 25, 7,  6,  5,  4,  3,  2,  1,  0,  15, 14, 13, 12, 11, 10, 9,  8,  9,  8,  11, 10, 13, 12,
    15, 14, 1,  0,  3,  2,  5,  4,  7,  6,  24, 25, 26, 27, 0,  17, 10, 19, 16, 17, 18, 19, 20, 21, 22, 23, 27, 26, 25,
    24, 19, 2,  17, 8,  19, 18, 17, 16, 23, 22, 21, 20, 10, 11, 8,  9,  14, 15, 12, 13, 2,  3,  0,  1,  6,  7,  4,  5,
    18, 19, 16, 17, 22, 23, 20, 21, 26, 27, 24, 25, 2,  3,  8,  9,  3,  2,  1,  0,  7,  6,  5,  4,  11, 10, 9,  8,  15,
    14, 13, 12, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 17, 16, 19, 18, 21, 20, 23, 22, 25, 24,
    27, 26, 1,  0,  11, 10, 15, 22, 5,  20, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 14, 15, 12, 13, 10, 11, 8,
    9,  6,  7,  4,  5,  2,  3,  0,  1,  13, 12, 15, 14, 9,  8,  11, 10, 5,  4,  7,  6,  1,  0,  3,  2,  20, 13, 22, 7,
    24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 5,  4,  7,  6,  1,  0,  3,  2,  13, 12, 15, 14, 9,  8,  11, 10, 20,
    21, 22, 23, 16, 17, 18, 19, 20, 13, 22, 7,  24, 25, 26, 27, 23, 22, 21, 20, 19, 18, 17, 16, 15, 22, 5,  20, 27, 26,
    25, 24, 6,  7,  4,  5,  2,  3,  0,  1,  14, 15, 12, 13, 10, 11, 8,  9,  8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,
    3,  4,  5,  6,  7,  25, 24, 27, 26, 1,  0,  11, 10, 17, 16, 19, 18, 21, 20, 23, 22, 26, 27, 24, 25, 2,  3,  8,  9,
    18, 19, 16, 17, 22, 23, 20, 21, 11, 10, 9,  8,  15, 14, 13, 12, 3,  2,  1,  0,  7,  6,  5,  4,  19, 18, 17, 16, 23,
    22, 21, 20, 27, 26, 25, 24, 19, 2,  17, 8,  2,  3,  0,  1,  6,  7,  4,  5,  10, 11, 8,  9,  14, 15, 12, 13, 1,  0,
    3,  2,  5,  4,  7,  6,  9,  8,  11, 10, 13, 12, 15, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 0,  17, 10,
    19, 14, 15, 4,  5,  26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,
    3,  2,  1,  0,  12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  0,  1,  2,  3,  13, 12, 7,  6,  25, 24, 27, 26, 21,
    20, 23, 22, 17, 16, 19, 18};

    const pid_t pid = getpid();
    uintptr_t physical_address = 0;

    // SPDLOG_TRACE("getting physical address for virtual address (0x{:016x}, 0b{:b})", virtual_address,
    // virtual_address);

    if (virt_to_phys_user(&physical_address, pid, virtual_address)) {
        // SPDLOG_ERROR("error: virt_to_phys_user");
        return EXIT_FAILURE;
    };

    const auto computed_perm = compute_perm(physical_address);
    const auto physical_address_index = getIndex(physical_address);
    const auto base_sequence_index = computed_perm ^ physical_address_index;  /// XOR'ing.

    // assert(base_sequence_index < 4096 && "Base sequence must be lower than 4096!");
    const int cha_by_hashing = base_sequence[base_sequence_index];

    return cha_by_hashing;
}

/* Convert the given virtual address to physical using /proc/PID/pagemap.
 *
 * @param[out] paddr physical address
 * @param[in]  pid   process to convert for
 * @param[in] vaddr virtual address to get entry for
 * @return 0 for success, 1 for failure
 */
/// https://stackoverflow.com/a/46247716/4645121
/// Bottom line is, to ensure a more reliable result: for read-only mappings, read from every page at least once before
/// querying its PFN. For write-enabled pages, write into every page at least once before querying its PFN. SO, BEFORE
/// USING THIS FUNCTION MAKE SURE TO READ FROM OR WRITE TO THAT MEMORY ADDRESS.
int virt_to_phys_user(uintptr_t *paddr, pid_t pid, uintptr_t vaddr) {
    char pagemap_file[BUFSIZ];
    int pagemap_fd;

    snprintf(pagemap_file, sizeof(pagemap_file), "/proc/%ju/pagemap", (uintmax_t)pid);
    pagemap_fd = open(pagemap_file, O_RDONLY);
    if (pagemap_fd < 0) {
        // SPDLOG_ERROR("Could not open() file {}. Error: {}", pagemap_file, strerror(errno));
        return 1;
    }
    PagemapEntry entry;

    const auto pagemap_entry = pagemap_get_entry(&entry, pagemap_fd, vaddr);
    // SPDLOG_TRACE("pagemap entry: {}", pagemap_entry);

    if (pagemap_entry) {
        return 1;
    }
    close(pagemap_fd);

    *paddr = (entry.pfn * sysconf(_SC_PAGE_SIZE)) + (vaddr % sysconf(_SC_PAGE_SIZE));
    return 0;
}

uint64_t getIndex(uintptr_t physical_address) {
    /// 2^m = baseSequenceLength. m = 12 on SKX with 18 cores.
    /// Therefore "index" bits are 17:6. -> 12 bits in total represent index value.
    // SPDLOG_TRACE("{} is called for physical address 0x{:x} (0b{:b}).", __PRETTY_FUNCTION__, physical_address,
    // physical_address);

    physical_address = physical_address >> 6;
    // SPDLOG_TRACE("shifted right to 6 bits: 0b{:b}", physical_address);

    int index_mask = 0xFFF;  /// this FFF value makes the code non-portable to other machines. it is ideal that this
                             /// value is constructed using base sequence length.
    uint64_t index = physical_address & index_mask;
    // SPDLOG_TRACE("index after mask is applied: 0b{:b}", index);

    // SPDLOG_TRACE("physi -> 0b{:064b}", physical_address);
    // SPDLOG_TRACE("index -> 0b{:064b}, 0x{:x}, {}", index, index, index);

    return index;
}

std::vector<int> readBaseSequence(const std::string &filename) {
    std::vector<int> res;

    std::ifstream infile(filename);
    int a;
    while (infile >> a) {
        res.push_back(a);
    }

    // SPDLOG_TRACE("base sequence size: {}", res.size());
    assert(res.size() != 0);

    return res;
}

/* Parse the pagemap entry for the given virtual address.
 *
 * @param[out] entry      the parsed entry
 * @param[in]  pagemap_fd file descriptor to an open /proc/pid/pagemap file
 * @param[in]  vaddr      virtual address to get entry for
 * @return 0 for success, 1 for failure
 */
int pagemap_get_entry(PagemapEntry *entry, int pagemap_fd, uintptr_t vaddr) {
    size_t nread;
    ssize_t ret;
    uint64_t data;
    uintptr_t vpn;

    vpn = vaddr / sysconf(_SC_PAGE_SIZE);
    nread = 0;
    while (nread < sizeof(data)) {
        ret = pread(pagemap_fd, ((uint8_t *)&data) + nread, sizeof(data) - nread, vpn * sizeof(data) + nread);
        nread += ret;
        if (ret <= 0) {
            // SPDLOG_ERROR("pread() error: {}", strerror(errno));
            return 1;
        }
    }
    entry->pfn = data & (((uint64_t)1 << 55) - 1);
    entry->soft_dirty = (data >> 55) & 1;
    entry->file_page = (data >> 61) & 1;
    entry->swapped = (data >> 62) & 1;
    entry->present = (data >> 63) & 1;
    return 0;
}

static void stick_this_thread_to_core(int core_id) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores) {
        std::cerr << "error binding thread to core " << core_id << "\n";
        // SPDLOG_ERROR("error binding thread to core {}!", core_id);
        return;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();

    int res = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

    if (res == 0) {
        // std::cout << "thread bound to core " << core_id << std::endl;
        //        SPDLOG_INFO("Thread bound to core {} successfully.", core_id);
    } else {
        //        SPDLOG_ERROR("Error in binding this thread to core {}.", core_id);
    }
}

static const long CHA_MSR_PMON_CTRL_BASE = 0x0E01L;
static const long CHA_MSR_PMON_CTR_BASE = 0x0E08L;
static const int CHA_BASE = 0x10;

/// LLC_LOOKUP may be filtered by the cacheline state (using CHA filter registers).
static const unsigned int LLC_ANY_LOOKUP = 0x00401134;
static const unsigned int LLC_LOCAL_LOOKUP = 0x00403134;
static const unsigned int LLC_REMOTE_LOOKUP = 0x00409134;
static const unsigned int LLC_DATA_READ_LOOKUP = 0x00400334;
static const unsigned int LLC_WRITE_LOOKUP = 0x00400534;
static const unsigned int LLC_REMOTE_SNOOP_LOOKUP =
    0x00400934;  /// mccalping made these 0x0043, but this implies making reserved bit 1. why???

static const unsigned int FILTER0_ALL_LLC =
    (1 << 24) | (1 << 23) | (1 << 22) | (1 << 21) | (1 << 17);  /// for filtering only and all LLC events (FMESI).
static const unsigned int FILTER1_OFF = 0x0000003B;             /// 3B essentially turns off this filter.

static const int CACHE_LINE_SIZE = 64;
static const int NUM_SOCKETS = 2;
static const int NUM_CHA_BOXES = 28;

int getCoreCount() { return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN)); }

static std::map<int, int> getMsrFds() {
    // SPDLOG_TRACE(__PRETTY_FUNCTION__);

    std::map<int, int> fd_map;

    char filename[100];

    auto logical_core_count = getCoreCount();
    std::cout << "logical core count: " << logical_core_count << std::endl;
    // SPDLOG_TRACE("logical core count: {}", logical_core_count);

    for (auto i = 0; i < logical_core_count; ++i) {
        sprintf(filename, "/dev/cpu/%d/msr", i);

        int fd = open(filename, O_RDWR);

        if (fd >= 0) {
            // SPDLOG_TRACE("Opened fd: {}", fd);
            fd_map.insert({i, fd});
        } else if (fd == -1) {
            std::cerr << "Error on open(): " << strerror(errno) << '\n';
            // SPDLOG_ERROR("error on open(): {}", strerror(errno));
        }
    }

    // for (const auto &p : fd_map) {
    //    SPDLOG_TRACE("MSR fd of core {}: {}", p.first, p.second);
    //}

    return fd_map;
}

static void setAllUncoreRegisters(const std::vector<unsigned int> &vals) {
    // SPDLOG_TRACE(__PRETTY_FUNCTION__);

    // SPDLOG_TRACE("Setting all uncore registers with values: ");
    // for (auto val : vals) {
    //     SPDLOG_TRACE("{:x}", val);
    //}

    int processor_in_socket[NUM_SOCKETS];
    int logical_core_count = getCoreCount();
    processor_in_socket[0] = 0;
    processor_in_socket[1] = logical_core_count - 1;

    auto msr_fds = getMsrFds();

    for (auto socket = 0; socket < NUM_SOCKETS; ++socket) {
        for (auto cha = 0; cha < NUM_CHA_BOXES; ++cha) {
            int core = processor_in_socket[socket];

            for (auto i = 0u; i < vals.size(); ++i) {
                uint64_t val = vals[i];
                uint64_t offset = CHA_MSR_PMON_CTRL_BASE + (0x10 * cha) + i;

                ssize_t rc64 = pwrite(msr_fds[core], &val, sizeof(val), offset);
                if (rc64 == 8) {
                    // SPDLOG_TRACE("Configuring socket {}, CHA {}, by writing 0x{:x} to core {} (fd: {}), offset
                    // 0x{:x}.",
                    //             socket, cha, val, core, msr_fds[core], offset);
                } else {
                    std::cerr << "Error writing data to msr device\n";
                    // SPDLOG_ERROR("Error writing all data to MSR device on core {}, written {} bytes.", core, rc64);
                }
            }
        }
    }

    /// it is important to close this as well, otherwise we will have fd leak.
    // SPDLOG_TRACE("closing file descriptors of MSRs.");
    for (const auto &p : msr_fds) {
        int cpu = p.first;
        int to_be_closed = p.second;
        // SPDLOG_TRACE("closing fd {} of cpu {}.", to_be_closed, cpu);
        ::close(to_be_closed);
        // SPDLOG_TRACE("closed fd: {}", to_be_closed);
    }
}

std::pair<int, int> findCHAPerfCounter(long long *data) {
    // SPDLOG_TRACE(
    //    "-------------------------------------------------------------- FINDCHA STARTED "
    //    "-------------------------------------------------------------------");

    stick_this_thread_to_core(
        5);  /// if you stick thread to an even core then you addresses will have their coherency managed by a CHA that
             /// is at socket-0 since even cores at socket-0. The same is true for opposite case.

    long logical_core_count = sysconf(_SC_NPROCESSORS_ONLN);
    long proc_in_pkg[NUM_SOCKETS];
    proc_in_pkg[0] = 0;
    proc_in_pkg[1] = logical_core_count - 1;

    const long long iteration_count = 2000;  // --> 1000 seems to be working, but sometimes results in wrong cha.

    // SPDLOG_TRACE("number of iterations for flush step: {} million.", iteration_count / 1'000'000);
    auto msr_fds = getMsrFds();

    /// one of the counter control values would have sufficed but I do not want to modify setAllUncoreMethods function
    /// just for this purpose.
    unsigned int COUNTER_CONTROL0 = LLC_DATA_READ_LOOKUP;  /// I will only read this.
    unsigned int COUNTER_CONTROL1 = LLC_DATA_READ_LOOKUP;
    unsigned int COUNTER_CONTROL2 = LLC_DATA_READ_LOOKUP;
    unsigned int COUNTER_CONTROL3 = LLC_DATA_READ_LOOKUP;
    unsigned int FILTER0 =
        FILTER0_ALL_LLC;  /// important that filter0 takes this value since we will measure LLC lookup events.
    unsigned int FILTER1 = FILTER1_OFF;  /// should remain off on my tests.

    std::vector<unsigned int> vals{COUNTER_CONTROL0, COUNTER_CONTROL1, COUNTER_CONTROL2,
                                   COUNTER_CONTROL3, FILTER0,          FILTER1};
    setAllUncoreRegisters(vals);
    vals = {COUNTER_CONTROL0, COUNTER_CONTROL1, COUNTER_CONTROL2, COUNTER_CONTROL3};  /// I WONT READ FILTERS!

    struct MsrReadings {
        uint64_t before;
        uint64_t after;
    };

    std::map<std::pair<int, int>, MsrReadings> msr_readings;

    // SPDLOG_TRACE("---------------- FIRST READINGS ----------------");
    for (auto socket = 0; socket < NUM_SOCKETS; ++socket) {
        int core = proc_in_pkg[socket];

        for (auto cha = 0; cha < NUM_CHA_BOXES; ++cha) {
            uint64_t msr_val;
            uint64_t msr_num = CHA_MSR_PMON_CTR_BASE +
                               (CHA_BASE * cha);  // just read the first counter. all 4 are LLC_DATA_READ_LOOKU anyways.
            // SPDLOG_TRACE("Executing pread() --> fd: {}, offset: {:x}", msr_fds[core], msr_num);
            ssize_t rc64 = pread(msr_fds[core], &msr_val, sizeof(msr_val), msr_num);
            if (rc64 != sizeof(msr_val)) {
                std::cerr << "EXIT FAILURE rc64: " << rc64 << ", error: " << strerror(errno) << '\n';
                // SPDLOG_ERROR("EXIT FAILURE. rc64: {}", rc64);
                // SPDLOG_ERROR("error: {}", strerror(errno));
                exit(EXIT_FAILURE);
            } else {
                msr_readings[{socket, cha}].before = msr_val;
                // SPDLOG_TRACE("Read {} from socket {}, CHA {} on core {}, offset 0x{:x}.", msr_val, socket, cha, core,
                //              msr_num);
            }
        }
    }

    // SPDLOG_TRACE("Modifying data and then flushing immediately several times. This should take long.");

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    for (long long i = 0; i < iteration_count; ++i) {
        data[0] += 1;
        _mm_mfence();
        _mm_clflush(&data[0]);
        _mm_mfence();
    }
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    // SPDLOG_TRACE("Flush step took {} milliseconds.",
    //             std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

    // SPDLOG_TRACE("---------------- SECOND READINGS ----------------");
    for (auto socket = 0; socket < NUM_SOCKETS; ++socket) {
        int core = proc_in_pkg[socket];

        for (auto cha = 0; cha < NUM_CHA_BOXES; ++cha) {
            uint64_t msr_val;
            uint64_t msr_num = CHA_MSR_PMON_CTR_BASE + (CHA_BASE * cha);
            // SPDLOG_TRACE("Executing pread() --> fd: {}, offset: {:x}", msr_fds[core], msr_num);
            ssize_t rc64 = pread(msr_fds[core], &msr_val, sizeof(msr_val), msr_num);
            if (rc64 != sizeof(msr_val)) {
                std::cerr << "EXIT FAILURE rc64: " << rc64 << ", error: " << strerror(errno) << '\n';
                // SPDLOG_ERROR("EXIT FAILURE. rc64: {}", rc64);
                // SPDLOG_ERROR("error: {}", strerror(errno));
                exit(EXIT_FAILURE);
            } else {
                msr_readings[{socket, cha}].after = msr_val;
                // SPDLOG_TRACE("Read {} from socket {}, CHA {} on core {}, offset 0x{:x}.", msr_val, socket, cha, core,
                //             msr_num);
            }
        }
    }

    // SPDLOG_TRACE("---------------- ANALYZING ----------------");

    int assigned_cha = -1;
    int assigned_socket = -1;

    uint64_t max_diff = 0;
    for (const auto &[socket_cha_pair, msr_reading] : msr_readings) {
        const auto diff = msr_reading.after - msr_reading.before;
        // SPDLOG_TRACE("NUM_SOCKETS: {}, before: {}, after: {}, diff: {}, socket: {}, cha: {}", NUM_SOCKETS,
        //             msr_reading.before, msr_reading.after, diff, socket_cha_pair.first, socket_cha_pair.second);

        if (diff > max_diff) {
            max_diff = diff;
            // SPDLOG_TRACE("max_diff: {}", max_diff);

            const auto socket = socket_cha_pair.first;
            const auto cha = socket_cha_pair.second;
            assigned_cha = cha;
            assigned_socket = socket;

            // SPDLOG_TRACE("socket: {}, cha: {}", socket, cha);
        }
    }

    // SPDLOG_TRACE("closing file descriptors of MSRs.");
    for (const auto &p : msr_fds) {
        int cpu = p.first;
        int to_be_closed = p.second;
        // SPDLOG_TRACE("closing fd {} of cpu {}.", to_be_closed, cpu);
        ::close(to_be_closed);
        // SPDLOG_TRACE("closed fd: {}", to_be_closed);
    }

    // SPDLOG_TRACE(
    //     "-------------------------------------------------------------- FINDCHA ENDED "
    //     "-------------------------------------------------------------------");

    return {assigned_socket, assigned_cha};
}