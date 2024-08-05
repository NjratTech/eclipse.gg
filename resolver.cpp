#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"

namespace resolver
{
    constexpr int EXTENDED_YAW_CACHE_SIZE = 16;
    constexpr float JITTER_THRESHOLD = 5.0f;
    constexpr float FREESTAND_THRESHOLD = 35.0f;

    inline float calculate_average_yaw(const float* yaw_cache, int size)
    {
        float sin_sum = 0.0f, cos_sum = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            sin_sum += std::sin(DEG2RAD(yaw_cache[i]));
            cos_sum += std::cos(DEG2RAD(yaw_cache[i]));
        }
        return RAD2DEG(std::atan2(sin_sum / size, cos_sum / size));
    }

    inline float calculate_average_deviation(const float* yaw_cache, int size)
    {
        float sum = 0.0f;
        for (int i = 0; i < size - 1; ++i)
        {
            sum += std::fabsf(yaw_cache[i] - yaw_cache[i + 1]);
        }
        return sum / (size - 1);
    }

    inline void prepare_jitter(c_cs_player* player, resolver_info_t& resolver_info, anim_record_t* current)
    {
        auto& jitter = resolver_info.jitter;

        jitter.yaw_cache[jitter.yaw_cache_offset % EXTENDED_YAW_CACHE_SIZE] = current->eye_angles.y;
        jitter.yaw_cache_offset = (jitter.yaw_cache_offset + 1) % EXTENDED_YAW_CACHE_SIZE;

        float avg_deviation = calculate_average_deviation(jitter.yaw_cache, EXTENDED_YAW_CACHE_SIZE);
        jitter.is_jitter = avg_deviation > JITTER_THRESHOLD;
    }

    // wtf lol
    // TODO: normal animlayer resolver
    // for example: orig_delta = abs(serv_layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate - layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate); and etc.
    inline int detect_side_from_animations(c_cs_player* player, anim_record_t* current)
    {
        auto& layers = current->layers;
        float delta_935 = layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate - layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].playback_rate;
        float delta_937 = layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate - layers[ANIMATION_LAYER_LEAN].playback_rate;

        if (std::abs(delta_935) > 0.5f || std::abs(delta_937) > 0.5f)
        {
            return (delta_935 > 0.f || delta_937 > 0.f) ? 1 : -1;
        }
        return 0;
    }

    inline int detect_freestand(c_cs_player* player)
    {
        if (!HACKS->local)
            return 0;

        vec3_t local_pos = HACKS->local->get_eye_position();
        vec3_t enemy_pos = player->get_eye_position();
        vec3_t enemy_dir = (local_pos - enemy_pos).normalized();

        vec3_t left_dir = enemy_dir.cross(vec3_t(0, 0, 1)).normalized();
        vec3_t right_dir = vec3_t(0, 0, 1).cross(enemy_dir).normalized();

        vec3_t left_probe = enemy_pos + left_dir * 40.f;
        vec3_t right_probe = enemy_pos + right_dir * 40.f;

        c_game_trace left_trace, right_trace;
        ray_t left_ray(enemy_pos, left_probe), right_ray(enemy_pos, right_probe);

        HACKS->engine_trace->trace_ray(left_ray, MASK_SHOT, nullptr, &left_trace);
        HACKS->engine_trace->trace_ray(right_ray, MASK_SHOT, nullptr, &right_trace);

        float left_fraction = left_trace.fraction;
        float right_fraction = right_trace.fraction;

        if (std::abs(left_fraction - right_fraction) > 0.2f)
        {
            return (left_fraction > right_fraction) ? -1 : 1;
        }

        return 0;
    }

    inline void resolve_jitter(c_cs_player* player, resolver_info_t& info, anim_record_t* current)
    {
        auto& misses = RAGEBOT->missed_shots[player->index()];
        if (misses > 0)
        {
            info.side = (misses % 3) - 1; // Cycle through -1, 0, 1
        }
        else
        {
            float avg_yaw = calculate_average_yaw(info.jitter.yaw_cache, EXTENDED_YAW_CACHE_SIZE);
            float diff = math::normalize_yaw(current->eye_angles.y - avg_yaw);
            info.side = (diff > 0.f) ? -1 : 1;
        }

        info.resolved = true;
        info.mode = XOR("jitter");
    }

    inline void resolve_static(c_cs_player* player, resolver_info_t& info, anim_record_t* current)
    {
        auto& misses = RAGEBOT->missed_shots[player->index()];
        if (misses > 0)
        {
            info.side = (misses % BRUTEFORCE_CYCLE_LENGTH) - 1;
            info.mode = XOR("brute");
        }
        else
        {
            int anim_side = detect_side_from_animations(player, current);
            int freestand_side = detect_freestand(player);

            if (anim_side != 0)
            {
                info.side = anim_side;
                info.mode = XOR("anim");
            }
            else if (freestand_side != 0)
            {
                info.side = freestand_side;
                info.mode = XOR("freestand");
            }
            else
            {
                info.side = 0;
                info.mode = XOR("static");
            }
        }
        info.resolved = true;
    }

    inline void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* last)
    {
        auto& info = resolver_info[player->index()];

        if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || player->is_bot() || !g_cfg.rage.resolver)
        {
            if (info.resolved)
                info.reset();
            return;
        }

        auto state = player->animstate();
        if (!state || !player->get_studio_hdr())
        {
            if (info.resolved)
                info.reset();
            return;
        }

        current->choke < 2 ? info.add_legit_ticks() : info.add_fake_ticks();

        if (info.is_legit())
        {
            info.resolved = false;
            info.mode = XOR("no fake");
            return;
        }

        prepare_jitter(player, info, current);

        if (info.jitter.is_jitter)
        {
            resolve_jitter(player, info, current);
        }
        else
        {
            resolve_static(player, info, current);
        }
    }

    inline void apply_side(c_cs_player* player, anim_record_t* current, int choke)
    {
        auto& info = resolver_info[player->index()];
        if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || !info.resolved || player->is_teammate(false))
            return;

        auto state = player->animstate();
        if (!state)
            return;

        float desync_angle = choke < 2 ? state->get_max_rotation() : 120.f;
        state->abs_yaw = math::normalize_yaw(player->eye_angles().y + desync_angle * info.side);
    }
}
