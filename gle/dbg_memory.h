#pragma once
#define ENABLE_MEMORY_TRACKING
#ifdef ENABLE_MEMORY_TRACKING

#include <memory>
#include <unordered_map>
#include "alias.h"
#include <string>
#endif
#ifdef ENABLE_MEMORY_TRACKING
struct alloc_info {
    size_t count;
    size_t size;
};

class debug_memory_tracker {
public:
    debug_memory_tracker();

    ~debug_memory_tracker();

    std::unordered_map<std::string, alloc_info> s_allocation_info;

    inline static u64 s_UntrackedSize = 0;
    inline static debug_memory_tracker* s_instance = nullptr;
};

#define DEBUG_IMPL_ALLOC(X) void * operator new(size_t size) \
{                                                            \
if(debug_memory_tracker::s_instance->s_allocation_info.find(#X)==debug_memory_tracker::s_instance->s_allocation_info.end()) {debug_memory_tracker::s_instance->s_allocation_info.emplace(#X, alloc_info{0,0});} \
debug_memory_tracker::s_instance->s_allocation_info[#X].count++;                         \
debug_memory_tracker::s_instance->s_allocation_info[#X].size += size;                                                             \
return malloc (size);\
}\
void operator delete(void* p)\
{                                                            \
free(p);                                                     \
if(!debug_memory_tracker::s_instance) return;           \
debug_memory_tracker::s_instance->s_allocation_info[#X].count--;                         \
debug_memory_tracker::s_instance->s_allocation_info[#X].size -= sizeof(X);                                                                  \
}
#else
#define DEBUG_IMPL_ALLOC(X)
#endif