#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PagemapEntry {
    uint64_t pfn : 55;
    unsigned int soft_dirty : 1;
    unsigned int file_page : 1;
    unsigned int swapped : 1;
    unsigned int present : 1;
};

uint64_t compute_perm(uintptr_t physical_address);
uintptr_t getPhysicalAddress(uintptr_t virtual_address);
int findCHAByHashing(uintptr_t virtual_address);
int virt_to_phys_user(uintptr_t* paddr, pid_t pid, uintptr_t vaddr);
uint64_t getIndex(uintptr_t physical_address);
std::vector<int> readBaseSequence(const std::string& filename);
int pagemap_get_entry(PagemapEntry* entry, int pagemap_fd, uintptr_t vaddr);

int getCoreCount();
std::pair<int, int> findCHAPerfCounter(long long* data);