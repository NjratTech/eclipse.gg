#pragma once
#include <vector>
#include <memory>
#include <algorithm>

#define MAX_HOOKS 100 // Предполагаемое максимальное количество хуков

class c_detour_hooks
{
private:
    struct hooks_t
    {
        void* target;
        void* detour;
        void* original;
    };

    std::vector<hooks_t> hooks{};

public:
    c_detour_hooks()
    {
        hooks.reserve(MAX_HOOKS);
        MH_Initialize();
    }

    ~c_detour_hooks()
    {
        unhook_all();
        MH_Uninitialize();
    }

    INLINE void add_hook(void* target, void* detour)
    {
        auto& hook = hooks.emplace_back();
        hook.target = target;
        hook.detour = detour;
        hook.original = nullptr;

        MH_STATUS status = MH_CreateHook(hook.target, hook.detour, &hook.original);
        if (status != MH_OK)
        {
            // Обработка ошибки
        }
    }

    INLINE void hook_all()
    {
        for (const auto& hook : hooks)
        {
            MH_STATUS status = MH_EnableHook(hook.target);
            if (status != MH_OK)
            {
                // Обработка ошибки
            }
        }
    }

    INLINE void unhook_all()
    {
        for (const auto& hook : hooks)
        {
            MH_STATUS status = MH_DisableHook(hook.target);
            if (status != MH_OK)
            {
                // Обработка ошибки
            }
        }
    }

    template <typename T = void*>
    INLINE T get_original(T detour)
    {
        auto iter = std::find_if(hooks.begin(), hooks.end(), [&](const hooks_t& hook)
            {
                return hook.detour == detour;
            });

        if (iter == hooks.end() || iter->original == nullptr)
            return nullptr;

        return static_cast<T>(iter->original);
    }
};

#ifdef _DEBUG
inline auto DETOUR_HOOKS = std::make_unique<c_detour_hooks>();
#else
CREATE_DUMMY_PTR(c_detour_hooks);
DECLARE_XORED_PTR(c_detour_hooks, GET_XOR_KEYUI32);

#define DETOUR_HOOKS XORED_PTR(c_detour_hooks)
#endif
