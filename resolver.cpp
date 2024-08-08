#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"

int trace_resolve(c_cs_player* player, anim_record_t* record)
{
    constexpr float step{ 4.f };
    constexpr float range{ 20.f };
    int side { 0 };

    vec3_t eye_pos = HACKS->local->get_eye_position();
    vec3_t target_pos = player->origin();

    std::array<float, 3> damages = { 0.f, 0.f, 0.f };

    for (float yaw = record->eye_angles.y - range; yaw <= record->eye_angles.y + range; yaw += step)
    {
        vec3_t head_pos;
        math::angle_vectors(vec3_t(0.f, yaw, 0.f), head_pos);
        head_pos = target_pos + (head_pos * 25.f);

        c_trace_filter filter;
        filter.skip = player;

        c_game_trace trace;
        HACKS->engine_trace->trace_ray(ray_t(eye_pos, head_pos), MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

        if (trace.fraction < 0.97f)
            continue;

        vec3_t angles = math::calc_angle(target_pos, eye_pos);
        float delta = math::normalize_yaw(yaw - angles.y);

        if (delta < -10.f)
            damages[0] += trace.fraction;
        else if (delta > 10.f)
            damages[2] += trace.fraction;
        else
            damages[1] += trace.fraction;
    }

    int best_side = std::distance(damages.begin(), std::max_element(damages.begin(), damages.end()));

    switch (best_side)
    {
    case 0:
        side = 1;
        break;
    case 2:
        side = -1;
        break;
    default:
        side = 0;
        break;
    }

    return side;
}

void resolve(c_cs_player* player, anim_record_t* current) {
    if (player->is_bot() || !current)
        return;

    auto state = player->animstate();
    if (!state)
        return;

    auto& info = resolver_info[player->index()];
    bool walking{ player->is_walking() || state->adjust_started };
    bool can_use_anim{ current->on_ground && current->velocity_for_animfix.length_2d() > 0.1f };

    if (can_use_anim) {
        info.mode = "anim";

        float layer_deltas[3] = {
            abs(current->layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate - current->layers_orig[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate),
            abs(current->layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate - current->layers_left[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate),
            abs(current->layers[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate - current->layers_right[ANIMATION_LAYER_MOVEMENT_MOVE].playback_rate)
        };

        float adjust_layer_delta = abs(current->layers[ANIMATION_LAYER_ADJUST].playback_rate - current->layers_orig[ANIMATION_LAYER_ADJUST].playback_rate);

        int best_side = std::distance(layer_deltas, std::max_element(layer_deltas, layer_deltas + 3));

        if (best_side == 1 && layer_deltas[1] > layer_deltas[0] * 1.2f) {
            info.side = -1;
            info.resolved = true;
        }
        else if (best_side == 2 && layer_deltas[2] > layer_deltas[0] * 1.2f) {
            info.side = 1;
            info.resolved = true;
        }
        else if (adjust_layer_delta > 0.5f) {
            walking = true;
            info.resolved = false;
        }
        else {
            info.side = 0;
            info.resolved = false;
        }
    }
    else if (!walking) {
        info.mode = "lby";
        float lby_delta = math::angle_diff(player->lower_body_yaw(), state->abs_yaw);

        if (abs(lby_delta) > 35.0f) {
            info.side = (lby_delta > 0) ? 1 : -1;
            info.resolved = true;
        }
        else {
            info.side = 0;
            info.resolved = false;
        }
    }
    else {
		info.mode = "walk";
		info.side = trace_resolve(player, current);
		info.resolved = true;
	}
}
