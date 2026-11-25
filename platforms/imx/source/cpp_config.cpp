// Copyright (c) 2026 Polyend Tracker Community Edition Contributors
// SPDX-License-Identifier: MIT
//
// Minimal embedded C++ runtime hooks:
// - Global new/delete operators delegating to malloc/free
// - No-op __aeabi_atexit stub (firmware never exits)
// - Minimal __verbose_terminate_handler to avoid pulling in demangling/stdio

#include <cstddef>
#include <cstdlib>

void* operator new(std::size_t size)
{
    return std::malloc(size);
}

void* operator new[](std::size_t size)
{
    return std::malloc(size);
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    std::free(ptr);
}

extern "C" int __aeabi_atexit(void* /*object*/, void (*/*destructor*/)(void*), void* /*dso_handle*/)
{
    return 0;
}

#ifdef CPP_NO_HEAP
extern "C" void* malloc(std::size_t)
{
    return nullptr;
}

extern "C" void free(void*)
{
}
#endif

#ifndef CPP_USE_CPPLIBRARY_TERMINATE_HANDLER
namespace __gnu_cxx
{
void __verbose_terminate_handler()
{
    for (;;)
    {
    }
}
} // namespace __gnu_cxx
#endif
