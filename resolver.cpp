#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"

float calculate_average_yaw(const float* yaw_cache, int size)
{
    float sin_sum = 0.0f, cos_sum = 0.0f;
    for (int i = 0; i < size; ++i)
    {
        sin_sum += std::sin(DEG2RAD(yaw_cache[i]));
        cos_sum += std::cos(DEG2RAD(yaw_cache[i]));
    }
    return RAD2DEG(std::atan2(sin_sum / size, cos_sum / size));
}

float calculate_average_deviation(const float* yaw_cache, int size)
{
    float sum = 0.0f;
    for (int i = 0; i < size - 1; ++i)
    {
        sum += std::fabsf(yaw_cache[i] - yaw_cache[i + 1]);
    }
    return sum / (size - 1);
}

float resolve_by_animations(c_cs_player* player, anim_record_t* current, anim_record_t* previous)
{
    float resolved_yaw = current->eye_angles.y;

    if (current->velocity.length_2d() <= 0.1f)
    {
        // Player is standing
        float delta = math::angle_diff(current->eye_angles.y, player->animstate()->abs_yaw);
        resolved_yaw = player->animstate()->abs_yaw + (delta > 0 ? -60.f : 60.f);
    }
    else
    {
        // Player is moving
        if (previous &&
            !((int)(current->layers[ANIMATION_LAYER_LEAN].weight * 1000.f)) &&
            ((int)(current->layers[ANIMATION_LAYER_MOVEMENT_MOVE].weight * 1000.f) ==
                (int)(previous->layers[ANIMATION_LAYER_MOVEMENT_MOVE].weight * 1000.f)))
        {
            float orig_delta = std::abs(current->layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate -
                current->resolve_layers[ANIMATION_LAYER_MOVEMENT_MOVE][0].playback_rate);

            float right_delta = std::abs(current->layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate -
                current->resolve_layers[ANIMATION_LAYER_MOVEMENT_MOVE][2].playback_rate);

            float left_delta = std::abs(current->layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate -
                current->resolve_layers[ANIMATION_LAYER_MOVEMENT_MOVE][1].playback_rate);

            if (orig_delta < right_delta || left_delta <= right_delta || (int)(right_delta * 1000.0f) == 0)
            {
                if (orig_delta >= left_delta && right_delta > left_delta && (int)(left_delta * 1000.0f) == 0)
                {
                    resolved_yaw = player->animstate()->abs_yaw + 60.f;
                }
            }
            else
            {
                resolved_yaw = player->animstate()->abs_yaw - 60.f;
            }
        }
    }

    return math::normalize_yaw(resolved_yaw);
}

int detect_freestand(c_cs_player* player)
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

void resolve(c_cs_player* player, resolver_info_t& info, anim_record_t* current, anim_record_t* prev_record)
{
    auto& misses = RAGEBOT->missed_shots[player->index()];
    if (misses > 1)
    {
        info.side = (misses % BRUTEFORCE_CYCLE_LENGTH) - 1;
        info.mode = XOR("brute");
    }
    else
    {
        float resolved_yaw = resolve_by_animations(player, current, prev_record);
        int freestand_side = detect_freestand(player);

        if (freestand_side != 0)
        {
            info.side = freestand_side;
            info.mode = XOR("freestand");
        }
        else
        {
            info.side = (resolved_yaw > player->animstate()->abs_yaw) ? 1 : -1;
            info.mode = XOR("anim");
        }
    }
    info.resolved = true;
}

void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* last)
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

    resolve(player, info, current, last);
}

void apply_side(c_cs_player* player, anim_record_t* current, int choke)
{
    auto& info = resolver_info[player->index()];
    if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || !info.resolved || player->is_teammate(false) || player->is_bot() || !g_cfg.rage.resolver)
        return;

    auto state = player->animstate();
    if (!state)
        return;

    float desync_angle = choke < 2 ? state->get_max_rotation() : 120.f;
    state->abs_yaw = math::normalize_yaw(player->eye_angles().y + desync_angle * info.side);
}
