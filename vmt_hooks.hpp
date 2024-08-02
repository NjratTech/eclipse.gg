#pragma once
#include <vector>
#include <memory>
#include <unordered_map>

class c_vmt_hooks
{
private:
    struct hook_info
    {
        void** original_vmt;
        std::unique_ptr<void* []> new_vmt;
        std::unordered_map<int, void*> original_functions;
    };

    std::vector<hook_info> hooks;

public:
    c_vmt_hooks()
    {
        hooks.reserve(MAX_HOOKS);
    }

    INLINE void add_hook(void* target, int index, void* new_function)
    {
        auto it = std::find_if(hooks.begin(), hooks.end(), [target](const hook_info& info) {
            return info.original_vmt == *reinterpret_cast<void***>(target);
            });

        if (it == hooks.end())
        {
            hook_info new_hook;
            new_hook.original_vmt = *reinterpret_cast<void***>(target);

            // Подсчет количества методов в VMT
            int method_count = 0;
            while (new_hook.original_vmt[method_count])
                method_count++;

            new_hook.new_vmt = std::make_unique<void* []>(method_count);
            std::memcpy(new_hook.new_vmt.get(), new_hook.original_vmt, method_count * sizeof(void*));

            it = hooks.insert(hooks.end(), std::move(new_hook));
        }

        it->original_functions[index] = it->new_vmt[index];
        it->new_vmt[index] = new_function;
    }

    INLINE void hook_all()
    {
        for (auto& hook : hooks)
        {
            *reinterpret_cast<void***>(hook.original_vmt) = hook.new_vmt.get();
        }
    }

    INLINE void unhook_all()
    {
        for (auto& hook : hooks)
        {
            *reinterpret_cast<void***>(hook.original_vmt) = hook.original_vmt;
        }
    }

    template <typename T = void*>
    INLINE T get_original(void* target, int index)
    {
        auto it = std::find_if(hooks.begin(), hooks.end(), [target](const hook_info& info) {
            return info.original_vmt == *reinterpret_cast<void***>(target);
            });

        if (it != hooks.end() && it->original_functions.count(index))
        {
            return reinterpret_cast<T>(it->original_functions[index]);
        }

        return nullptr;
    }
};

#ifdef _DEBUG
inline auto VMT_HOOKS = std::make_unique<c_vmt_hooks>();
#else
CREATE_DUMMY_PTR(c_vmt_hooks);
DECLARE_XORED_PTR(c_vmt_hooks, GET_XOR_KEYUI32);

#define VMT_HOOKS XORED_PTR(c_vmt_hooks)
#endif
