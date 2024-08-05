#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"

namespace resolver
{
    // Prepare jitter information for a player
    inline void prepare_jitter(c_cs_player* player, resolver_info_t& resolver_info, anim_record_t* current)
    {
        auto& jitter = resolver_info.jitter;

        // Update yaw cache
        jitter.yaw_cache[jitter.yaw_cache_offset % YAW_CACHE_SIZE] = current->eye_angles.y;
        jitter.yaw_cache_offset = (jitter.yaw_cache_offset + 1) % YAW_CACHE_SIZE;

        // Reset tick counters
        jitter.static_ticks = 0;
        jitter.jitter_ticks = 0;

        // Analyze yaw cache for jitter
        for (int i = 0; i < YAW_CACHE_SIZE - 1; ++i)
        {
            float diff = std::fabsf(jitter.yaw_cache[i] - jitter.yaw_cache[i + 1]);

            if (diff <= 0.f)
            {
                if (jitter.static_ticks < JITTER_MAX_TICKS)
                    jitter.static_ticks++;
            }
            else if (diff >= JITTER_DIFF_THRESHOLD)
            {
                if (jitter.jitter_ticks < JITTER_MAX_TICKS)
                    jitter.jitter_ticks++;
            }
        }

        // Determine if jitter is present
        jitter.is_jitter = jitter.jitter_ticks > jitter.static_ticks;
    }

    // Prepare resolver information for a player
    inline void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* last)
    {
        auto& info = resolver_info[player->index()];

        // Check if resolver should be active
        if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || player->is_bot() || !g_cfg.rage.resolver)
        {
            if (info.resolved)
                info.reset();

            return;
        }

        // Get animation state
        auto state = player->animstate();
        if (!state)
        {
            if (info.resolved)
                info.reset();

            return;
        }

        // Get studio header
        auto hdr = player->get_studio_hdr();
        if (!hdr)
            return;

        // Update tick counters based on choke
        if (current->choke < 2)
            info.add_legit_ticks();
        else
            info.add_fake_ticks();

        // Check if player is using legit lag
        if (info.is_legit())
        {
            info.resolved = false;
            info.mode = XOR("no fake");
            return;
        }

        // Prepare jitter information
        prepare_jitter(player, info, current);
        auto& jitter = info.jitter;

        // Resolve jitter
        if (jitter.is_jitter)
        {
            auto& misses = RAGEBOT->missed_shots[player->index()];
            if (misses > 0)
            {
                info.side = 1337; // Special side for jitter
            }
            else
            {
                // Calculate average yaw from recent yaw values
                float first_angle = math::normalize_yaw(jitter.yaw_cache[YAW_CACHE_SIZE - 1]);
                float second_angle = math::normalize_yaw(jitter.yaw_cache[YAW_CACHE_SIZE - 2]);
                float avg_yaw = math::normalize_yaw(RAD2DEG(std::atan2f(
                    (std::sin(DEG2RAD(first_angle)) + std::sin(DEG2RAD(second_angle))) / 2.f,
                    (std::cos(DEG2RAD(first_angle)) + std::cos(DEG2RAD(second_angle))) / 2.f
                )));

                // Determine side based on yaw difference
                float diff = math::normalize_yaw(current->eye_angles.y - avg_yaw);
                info.side = (diff > 0.f) ? -1 : 1;
            }

            info.resolved = true;
            info.mode = XOR("jitter");
        }
        else
        {
            // Resolve static or bruteforce
            auto& misses = RAGEBOT->missed_shots[player->index()];
            if (misses > 0)
            {
                // Bruteforce resolver
                info.side = (misses % BRUTEFORCE_CYCLE_LENGTH) - 1;
                info.resolved = true;
                info.mode = XOR("brute");
            }
            else
            {
                // Static resolver
                info.side = 0;
                info.mode = XOR("static");
                info.resolved = true;
            }
        }
    }

    // Apply resolver side to animation state
    inline void apply_side(c_cs_player* player, anim_record_t* current, int choke)
    {
        auto& info = resolver_info[player->index()];
        if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || !info.resolved || info.side == 1337 || player->is_teammate(false))
            return;

        // Get animation state
        auto state = player->animstate();
        if (!state)
            return;

        // Calculate desync angle based on choke
        float desync_angle = choke < 2 ? state->get_max_rotation() : 120.f;
        state->abs_yaw = math::normalize_yaw(player->eye_angles().y + desync_angle * info.side);
    }
}