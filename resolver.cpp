#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"

void resolve(c_cs_player* player, anim_record_t* current, anim_record_t* last) {
    if (player->is_bot() || !current || !last)
        return;

    auto state = player->animstate();
    if (!state)
        return;

    auto& info = resolver_info[player->index()];
    bool can_use_anim = current->on_ground && current->velocity_for_animfix.length_2d() > 0.1f;

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
            info.side = (adjust_layer_delta > 1.0f) ? 1 : -1;
            info.resolved = true;
        }
        else {
            info.side = 0;
            info.resolved = false;
        }
    }
    else {
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
}
