#pragma once
#include "animations.hpp"

constexpr int CACHE_SIZE = 2;
constexpr int YAW_CACHE_SIZE = 8;
constexpr auto MAX_TICKS = 3;

struct resolver_info_t
{
    bool resolved{};
    int side{};

    int legit_ticks{};
    int fake_ticks{};

    INLINE void add_legit_ticks()
    {
        if (legit_ticks < MAX_TICKS)
            ++legit_ticks;
        else
            fake_ticks = 0;
    }

    INLINE void add_fake_ticks()
    {
        if (fake_ticks < MAX_TICKS)
            ++fake_ticks;
        else
            legit_ticks = 0;
    }

    INLINE bool is_legit()
    {
        return legit_ticks > fake_ticks;
    }

    std::string mode{};
    c_animation_layers initial_layers[13]{};

    struct jitter_info_t
    {
        bool is_jitter{};
        int jitter_side{};
        std::deque<float> angles;

        __forceinline void reset()
        {
            is_jitter = false;
            jitter_side = 0;
            angles.clear();
        }
    } jitter;

    struct freestanding_t
    {
        bool updated{};
        int side{};
        float update_time{};

        inline void reset()
        {
            updated = false;
            side = 0;
            update_time = 0.f;
        }
    } freestanding{};

    anim_record_t record{};

    inline void reset()
    {
        resolved = false;
        side = 0;
        legit_ticks = 0;
        fake_ticks = 0;
        mode = "";
        freestanding.reset();
        jitter.reset();
        record.reset();

        for (auto& i : initial_layers)
            i = {};
    }
};

inline resolver_info_t resolver_info[65]{};

namespace resolver
{
    INLINE void reset()
    {
        for (auto& i : resolver_info)
            i.reset();
    }

    extern void prepare_jitter(c_cs_player* player, resolver_info_t& resolver_info, anim_record_t* current);
    extern void freestand_resolve(c_cs_player* player, resolver_info_t& resolver_info);
    extern void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* last);
    extern void apply_side(c_cs_player* player, anim_record_t* current, int choke);
}
