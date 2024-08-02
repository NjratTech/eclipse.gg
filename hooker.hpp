#pragma once

#include "hooker_interface.hpp"
#include "detour_hooks.hpp"
#include "vmt_hooks.hpp"
namespace hooker
{
    INLINE void add_detour(std::uint64_t target, void* detour)
    {
        DETOUR_HOOKS->add_hook(reinterpret_cast<void*>(target), detour);
    }

    //INLINE void add_vmt(const std::uint64_t target, PLH::VFuncMap& swap)
    //{
    //    VMT_HOOKS->add_hook(target, swap);
    //}

    INLINE void hook()
    {
        DETOUR_HOOKS->hook_all();
        VMT_HOOKS->hook_all();
    }

    INLINE void unhook()
    {
        DETOUR_HOOKS->unhook_all();
        VMT_HOOKS->unhook_all();
    }

    template <typename T = void*>
    INLINE T get_original(T detour)
    {
        return DETOUR_HOOKS->get_original<T>(detour);
    }

    // VMT get_original оставлен без изменений
    template <typename T = void*>
    INLINE T get_original(void* target, const int& index, T hook)
    {
        return VMT_HOOKS->get_original<T>(target, index, hook);
    }
}