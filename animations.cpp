#include "globals.hpp"
#include "server_bones.hpp"
#include "animations.hpp"
#include "lagcomp.hpp"
#include "entlistener.hpp"
#include "resolver.hpp"
#include "threads.hpp"
#include "engine_prediction.hpp"
#include "ragebot.hpp"

void fix_velocity(anim_record_t* old_record, anim_record_t* last_record, anim_record_t* record, c_cs_player* player)
{
    auto state = player->animstate();
    if (!state)
        return;

    auto weapon = (c_base_combat_weapon*)(HACKS->entity_list->get_client_entity_handle(player->active_weapon()));
    if (!weapon)
        return;

    auto weapon_info = HACKS->weapon_system->get_weapon_data(weapon->item_definition_index());

    // Reset velocity on player teleport as server does
    if (player->effects().has(EF_NOINTERP) || player->no_interp_parity() != player->no_interp_parity_old()) {
        record->velocity.reset();
        return;
    }

    auto prev_record = last_record;
    auto time_delta = TICKS_TO_TIME(record->choke);

    // Calculate maximum speed based on player state and weapon
    auto max_speed = weapon && weapon_info ?
        std::max<float>((player->is_scoped() ? weapon_info->max_speed_alt : weapon_info->max_speed), 0.001f)
        : CS_PLAYER_SPEED_RUN;

    if (player->is_walking())
        max_speed *= CS_PLAYER_SPEED_WALK_MODIFIER;

    if (player->duck_amount() >= 1.f)
        max_speed *= CS_PLAYER_SPEED_DUCK_MODIFIER;

    // Calculate velocity based on previous record and time delta
    if (prev_record) {
        if (time_delta > 0.f)
            record->velocity = (record->origin - prev_record->origin) / time_delta;

        // Adjust velocity based on aliveloop layer if on ground
        if (record->flags.has(FL_ONGROUND) && prev_record->flags.has(FL_ONGROUND)) {
            auto& layer_aliveloop = prev_record->layers[ANIMATION_LAYER_ALIVELOOP];
            auto& new_layer_aliveloop = record->layers[ANIMATION_LAYER_ALIVELOOP];

            // Calculate animation speed based on aliveloop layer weight
            auto anim_speed = 0.f;
            if (layer_aliveloop.weight > 0.f && layer_aliveloop.weight < 0.1f
                && new_layer_aliveloop.playback_rate == layer_aliveloop.playback_rate) {
                auto anim_modifier = 0.35f * (1.0f - layer_aliveloop.weight);

                if (anim_modifier > 0.f && anim_modifier < 1.f)
                    anim_speed = max_speed * (anim_modifier + 0.55f);
            }

            // Adjust velocity based on animation speed
            auto length = record->velocity.length_2d();

            if (length > 0.1f && anim_speed > 0.0f) {
                anim_speed /= length;
                record->velocity.x *= anim_speed;
                record->velocity.y *= anim_speed;
            }
        }

        // Set Z velocity to 0 if on ground
        if (record->flags.has(FL_ONGROUND))
            record->velocity.z = 0.f;
    } else {
        // Calculate velocity based on movement layer if no previous record
        if (record->flags.has(FL_ONGROUND)) {
            auto& layer_movement = record->layers[ANIMATION_LAYER_MOVEMENT_MOVE];

            // Calculate animation speed based on movement layer weight
            auto anim_speed = 0.f;
            if (layer_movement.weight)
                anim_speed = layer_movement.weight;

            // Adjust velocity based on animation speed
            auto length = record->velocity.length_2d();

            if (length > 0.1f && anim_speed > 0.0f) {
                anim_speed /= length;
                record->velocity.x *= anim_speed;
                record->velocity.y *= anim_speed;
            }

            // Set Z velocity to 0 if on ground
            record->velocity.z = 0.f;
        }
    }

    // Detect fakewalking
    auto move = &record->layers[ANIMATION_LAYER_MOVEMENT_MOVE];
    auto lean = &record->layers[ANIMATION_LAYER_LEAN];

    if (prev_record) {
        auto prev_move = &prev_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE];
        if (record->choke >= 12
            && record->flags.has(FL_ONGROUND)
            && record->velocity.length_2d() > 0.1f
            && move->weight <= 0.1f
            && prev_move->cycle == move->cycle
            && move->playback_rate < 0.0001f) {
            record->fakewalking = true;
            record->velocity.reset();
        }
    }
}

INLINE matrix_t* get_matrix_side(anim_record_t* new_record, int side)
{
#ifndef LEGACY
	switch (side)
	{
	case -1:
		return &new_record->matrix_left;
	case 1:
		return &new_record->matrix_right;
	case 0:
		return &new_record->matrix_zero;
	}
#endif

	return &new_record->matrix_orig;
}

static INLINE void update_sides(bool should_update, c_cs_player* player, anims_t* anim, anim_record_t* new_record, anim_record_t* last_record, int side, c_studio_hdr* hdr, int* flags_base, int parent_count)
{
	auto state = player->animstate();
	if (!state)
		return;

	// Handle the case where there is no last record (first record or player coming out of dormant)
	if (!last_record)
	{
		// Set latest data as soon as possible
		state->primary_cycle = new_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].cycle;
		state->move_weight = new_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].weight;

		// Fix goalfeetyaw on spawn
		state->last_update_time = (new_record->sim_time - HACKS->global_vars->interval_per_tick);

		// Update ground state and landing flag
		if (player->flags().has(FL_ONGROUND)) {
			state->on_ground = true;
			state->landing = false;
		}

		// Handle jump animation
		auto last_update_time = state->last_update_time;
		auto layer_jump = &new_record->layers[ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL];
		if (player->get_sequence_activity(layer_jump->sequence) == ACT_CSGO_JUMP) {
			auto duration_in_air = new_record->sim_time - layer_jump->cycle / layer_jump->playback_rate;
			if (duration_in_air > last_update_time) {
				state->on_ground = false;
				player->pose_parameter()[6] = 0.f;
				state->duration_in_air = 0.f;
				state->last_update_time = duration_in_air;
			}
		}

		// Update strafe change
		state->strafe_change_cycle = new_record->layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].cycle;
		state->strafe_change_weight = new_record->layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].weight;
	}
	else
	{
		// Update primary cycle and move weight from the last record
		state->primary_cycle = last_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].cycle;
		state->move_weight = last_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].weight;

		// Update strafe change
		state->strafe_change_cycle = last_record->layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].cycle;
		state->strafe_change_weight = new_record->layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].weight;
	}

	// Apply animation updates if needed
	if (should_update)
	{
		// Handle the case where there is no last record or the choke is less than 2
		if (!last_record || new_record->choke < 2)
		{
			// Handle jump animation if the player is not on ground
			if (last_record && !player->flags().has(FL_ONGROUND))
			{
				auto layer_jump = &new_record->layers[ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL];
				auto old_layer_jump = &last_record->layers[ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL];

				if (layer_jump->weight > 0.f && layer_jump->cycle > 0.f)
				{
					if (player->get_sequence_activity(layer_jump->sequence) == ACT_CSGO_JUMP)
					{
						if (layer_jump->cycle != old_layer_jump->cycle || layer_jump->sequence != old_layer_jump->sequence && old_layer_jump->cycle > layer_jump->cycle)
						{
							player->pose_parameter()[6] = 0.f;
							state->duration_in_air = layer_jump->cycle / layer_jump->playback_rate;
						}
					}
				}
			}

			// Apply side-specific yaw or call resolver
			if (side != 1337)
				state->abs_yaw = math::normalize_yaw(new_record->eye_angles.y + state->get_max_rotation() * side);
			else
				resolver::apply_side(player, new_record, new_record->choke);

			// Update player origin, velocity, and force animation update
			player->set_abs_origin(player->origin());
			player->abs_velocity() = player->velocity() = new_record->velocity;
			player->force_update_animations(anim);
		}
		else
		{
			// Handle animation interpolation for choked packets
			auto choke_float = static_cast<float>(new_record->choke);

			auto simulation_time_tick = TIME_TO_TICKS(new_record->sim_time);
			auto prev_simulation_time = simulation_time_tick - new_record->choke;

			int land_time{}, jump_time{};
			if (last_record)
			{
				// Detect landing animation
				if (player->flags().has(FL_ONGROUND)) {
					auto layer_land = &new_record->layers[ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB];
					auto old_layer_land = &last_record->layers[ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB];

					if (!last_record->flags.has(FL_ONGROUND) && layer_land->weight != 0.f && old_layer_land->weight == 0.f) {
						auto sequence_activity = player->get_sequence_activity(layer_land->sequence);
						if (sequence_activity == ACT_CSGO_LAND_LIGHT || sequence_activity == ACT_CSGO_LAND_HEAVY)
							land_time = TIME_TO_TICKS(new_record->sim_time - (layer_land->cycle / layer_land->playback_rate)) + 1;
					}
				}
				else {
					// Detect jump animation
					auto layer_jump = &new_record->layers[ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL];
					auto old_layer_jump = &last_record->layers[ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL];

					if (layer_jump->cycle != old_layer_jump->cycle
						|| layer_jump->sequence != old_layer_jump->sequence
						&& layer_jump->cycle < old_layer_jump->cycle) {
						auto sequence_activity = player->get_sequence_activity(layer_jump->sequence);
						if (sequence_activity == ACT_CSGO_JUMP)
							jump_time = TIME_TO_TICKS(new_record->sim_time - (layer_jump->cycle / layer_jump->playback_rate)) + 1;
					}
				}
			}

			// Update lower body yaw and thirdperson recoil
			player->lower_body_yaw() = last_record->lby;
			player->thirdperson_recoil() = last_record->thirdperson_recoil;

			// Interpolate animation data over the choke duration
			for (int i = 0; i <= new_record->choke; ++i)
			{
				auto current_command_tick = prev_simulation_time + i;
				auto current_command_time = TICKS_TO_TIME(current_command_tick);

				auto current_float_tick = static_cast<float>(i);
				float lerp_step = current_float_tick / choke_float;

				// Handle landing animation
				if (current_command_tick == land_time)
				{
					player->flags().force(FL_ONGROUND);

					auto record_layer_land = &new_record->layers[ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB];
					auto layer_land = &player->animlayers()[ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB];
					layer_land->cycle = 0.f;
					layer_land->playback_rate = record_layer_land->playback_rate;
				}

				// Handle jump animation
				if (current_command_tick == jump_time - 1)
					player->flags().force(FL_ONGROUND);

				if (current_command_tick == jump_time)
				{
					player->flags().remove(FL_ONGROUND);

					auto record_layer_jump = &new_record->layers[ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL];
					auto layer_jump = &player->animlayers()[ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL];
					layer_jump->cycle = 0.f;
					layer_jump->playback_rate = record_layer_jump->playback_rate;
				}

				// Interpolate origin, velocity, and duck amount
				auto lerp_origin = math::lerp(lerp_step, last_record->origin, new_record->origin);
				auto lerp_velocity = math::lerp(lerp_step, last_record->velocity, new_record->velocity);
				auto lerp_duck_amount = math::lerp(lerp_step, last_record->duck_amt, new_record->duck_amt);

				player->origin() = lerp_origin;
				player->abs_velocity() = player->velocity() = lerp_velocity;
				player->duck_amount() = lerp_duck_amount;

				// Duck (crouch) fix (credits: estk)
				if (player->flags().has(FL_DUCKING))
				{
					player->view_offset().z = math::lerp(lerp_step, 46.0f, 36.0f);
				}
				else
				{
					player->view_offset().z = 64.0f - (player->duck_amount() * 28.0f);
				}

				// Handle shooting animation
				if (new_record->shooting)
				{
					player->eye_angles() = new_record->last_reliable_angle;

					if (new_record->last_shot_time <= current_command_time)
					{
						player->eye_angles() = new_record->eye_angles.normalized_angle();
						player->lower_body_yaw() = new_record->lby;
						player->thirdperson_recoil() = new_record->thirdperson_recoil;
					}
				}

				// Update player origin and simulation time
				player->set_abs_origin(player->origin());
				RESTORE(player->sim_time());
				player->sim_time() = current_command_time;

				// Apply side-specific yaw or call resolver
				auto max_rotation = 120.f;
				if (side != 1337)
					state->abs_yaw = math::normalize_yaw(player->eye_angles().y + max_rotation * side);
				else
					resolver::apply_side(player, new_record, new_record->choke);

				// Force animation update
				player->force_update_animations(anim);
			}
		}
	}

	// Update collision bounds
	auto collideable = player->get_collideable();
	if (collideable)
		offsets::set_collision_bounds.cast<void(__thiscall*)(void*, vec3_t*, vec3_t*)>()(collideable, &player->bb_mins(), &player->bb_maxs());

	// Store collision change information
	new_record->collision_change_origin = player->collision_change_origin();
	new_record->collision_change_time = player->collision_change_time();

	// Invalidate bone cache and update lean layers
	player->invalidate_bone_cache();
	{
		new_record->layers[ANIMATION_LAYER_LEAN].weight = player->animlayers()[ANIMATION_LAYER_LEAN].weight = 0.f;
		new_record->layers[ANIMATION_LAYER_LEAN].cycle = player->animlayers()[ANIMATION_LAYER_LEAN].cycle = 0.f;

		anim->setup_bones = false;

		// Get the correct matrix based on the side
		auto matrix_side = get_matrix_side(new_record, side);

		// Store simulated layers
		player->store_layers(matrix_side->layers);

		// Restore layers
		player->set_layers(new_record->layers);

		// Build bones for the selected side
		matrix_side->bone_builder.store(player, matrix_side->matrix, 0x7FF00, hdr, flags_base, parent_count);
		matrix_side->bone_builder.setup();
	}
}

void update_layer(c_cs_player* player, anim_record_t* current, int side)
{
	auto state = player->animstate();

	if (!state)
		return;

	// Get the correct yaw based on the side
	// Sides: 1 = left, 2 = right, 0 = center
	float yaw{ };
	switch (side) {
	case 1:
		yaw = player->eye_angles().y - 58.f;
		break;
	case 2:
		yaw = player->eye_angles().y + 58.f;
		break;
	default:
		yaw = player->eye_angles().y;
		break;
	}

	// Store layers
	state->abs_yaw = math::normalize_yaw(player->eye_angles().y + yaw);
	player->update_client_side_animation();
	player->store_layers(current->resolve_layers[side]);

}

void thread_collect_info(c_cs_player* player)
{
	// Check if the player's animation state is valid
	auto state = player->animstate();
	if (!state)
		return;

	auto index = player->index();
	auto anim = ANIMFIX->get_anims(index);
	auto backup = ANIMFIX->get_backup_record(index);

	// Reset state and animation if the player pointer doesn't match
	if (anim->ptr != player)
	{
		state->reset();
		anim->reset();
		backup->reset();
		anim->ptr = player;
		anim->update_anims = anim->setup_bones = true;
		return;
	}

	// Check if the player's studio header is valid
	auto hdr = player->get_studio_hdr();
	if (!hdr)
	{
		state->reset();
		anim->reset();
		backup->reset();
		anim->ptr = player;
		anim->update_anims = anim->setup_bones = true;
		return;
	}

	// Reset state and animation if the player is not alive
	if (!player->is_alive())
	{
		state->reset();
		anim->reset();
		backup->reset();
		return;
	}

	// Handle player dormancy
	if (player->dormant())
	{
		anim->old_simulation_time = 0.f;
		anim->dormant_ticks = 0;
		anim->records.clear();
		backup->reset();
		anim->update_anims = anim->setup_bones = true;
		return;
	}

	// Increment dormant ticks if less than 3
	if (anim->dormant_ticks < 3)
		++anim->dormant_ticks;

	// Return if the player's simulation time hasn't changed
	if (player->sim_time() == player->old_sim_time())
		return;

	// Reset state if the player's spawn time has changed
	if (anim->old_spawn_time != player->spawn_time())
	{
		state->player = player;
		state->reset();
		anim->old_spawn_time = player->spawn_time();
		return;
	}

	auto max_ticks_size = HACKS->tick_rate;

	// Store initial layers and update weapon layers
	player->store_layers(resolver_info[player->index()].initial_layers);
	player->update_weapon_dispatch_layers();

	anim_record_t* last_record = nullptr;
	anim_record_t* old_record = nullptr;

	// Get the last and old records if they exist
	if (!anim->records.empty())
	{
		last_record = &anim->records.front();
		if (anim->records.size() >= 2)
			old_record = &anim->records[1];
	}

	// Create a new record for the current animation state
	auto& new_record = anim->records.emplace_front();
	new_record.dormant = anim->dormant_ticks < 2;
	new_record.store(player);
	new_record.shifting = false;

	// Setup layers
	update_layer(player, &new_record, 1);
	update_layer(player, &new_record, 2);
	update_layer(player, &new_record, 0);

	// Fix velocity and handle animation layers if a last record exists
	if (last_record)
	{
		fix_velocity(old_record, last_record, &new_record, player);

		auto layer_alive_loop = &new_record.layers[ANIMATION_LAYER_ALIVELOOP];
		auto old_layer_alive_loop = &last_record->layers[ANIMATION_LAYER_ALIVELOOP];

		auto current_playback_rate = layer_alive_loop->playback_rate;
		auto previous_playback_rate = old_layer_alive_loop->playback_rate;

		auto current_cycle = layer_alive_loop->cycle;
		auto previous_cycle = static_cast<int>(old_layer_alive_loop->cycle / (HACKS->global_vars->interval_per_tick * previous_playback_rate) + 0.5f);

		int cycle = (current_playback_rate == previous_playback_rate)
			? static_cast<int>(current_cycle / (current_playback_rate * HACKS->global_vars->interval_per_tick) + 0.5f)
			: static_cast<int>(previous_cycle + ((current_cycle / current_playback_rate + (1.f - old_layer_alive_loop->cycle) / previous_playback_rate) / HACKS->global_vars->interval_per_tick + 0.5f));

		int layer_delta = cycle - previous_cycle;
		if (layer_delta <= 18)
			new_record.choke = std::max(layer_delta, 1);

		new_record.shooting = new_record.last_shot_time > last_record->sim_time && new_record.last_shot_time <= last_record->sim_time;
		if (!new_record.shooting)
			new_record.last_reliable_angle = player->eye_angles();

		new_record.choke = std::clamp(new_record.choke, 0, 16);
	}

	auto bone_flags_base = hdr->bone_flags().base();
	auto bone_parent_count = hdr->bone_parent_count();

	// Prepare animation layers
	for (int i = 0; i < 13; i++)
	{
		auto layer = &player->animlayers()[i];
		layer->owner = player;
		layer->studio_hdr = player->get_studio_hdr();
	}

	backup->store(player);

	{
		resolver::prepare_side(player, &new_record, last_record);

		math::memcpy_sse(&anim->old_animstate, player->animstate(), sizeof(anim->old_animstate));
		for (int i = -1; i < 2; ++i)
		{
			update_sides(true, player, anim, &new_record, last_record, i, hdr, bone_flags_base, bone_parent_count);
			math::memcpy_sse(player->animstate(), &anim->old_animstate, sizeof(anim->old_animstate));
		}

		update_sides(true, player, anim, &new_record, last_record, 1337, hdr, bone_flags_base, bone_parent_count);
	}

	// Handle lag compensation
	if (!HACKS->cl_lagcomp0)
	{
		if (last_record)
		{
			if (last_record->sim_time > new_record.sim_time)
			{
				anim->last_valid_time = new_record.sim_time + std::abs(last_record->sim_time - new_record.sim_time) + TICKS_TO_TIME(1);
				new_record.shifting = true;
			}
			else
			{
				if (anim->last_valid_time > new_record.sim_time)
					new_record.shifting = true;
			}

			if (new_record.sim_time < new_record.old_sim_time) {
				new_record.break_lc = true;
			}
		}
	}
	else {
		new_record.shifting = false;
		new_record.break_lc = false;
	}

	backup->restore(player);

	// Maintain the size of the records list
	if (anim->records.size() > max_ticks_size - 1)
		anim->records.pop_back();
}

void c_animation_fix::update_enemies()
{
	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		THREAD_POOL->add_task(thread_collect_info, player);
	});
	THREAD_POOL->wait_all();
}

INLINE void modify_eye_pos(vec3_t& pos, matrix3x4_t* matrix)
{
	auto state = HACKS->local->animstate();
	if (!state)
		return;

	auto ground_entity = HACKS->entity_list->get_client_entity_handle(HACKS->local->ground_entity());
	if (state->landing || state->anim_duck_amount != 0.f || ground_entity == 0)
	{
		auto bone_pos = matrix[8].get_origin();
		const auto bone_z = bone_pos.z + 1.7f;
		if (pos.z > bone_z)
		{
			const auto view_modifier = std::clamp((fabsf(pos.z - bone_z) - 4.f) * 0.16666667f, 0.f, 1.f);
			const auto view_modifier_sqr = view_modifier * view_modifier;
			pos.z += (bone_z - pos.z) * (3.f * view_modifier_sqr - 2.f * view_modifier_sqr * view_modifier);
		}
	}
}

vec3_t c_animation_fix::get_eye_position(float pitch_angle)
{
	auto anim_state = HACKS->local->animstate();
	if (!anim_state)
		return {};

	auto pose_param_body_pitch = XORN(12);
	auto anims_data = &anims[HACKS->local->index()];
	auto local_anims_data = &local_anims;
	auto initial_vars = ENGINE_PREDICTION->get_initial_vars();

	// Save and temporarily set the absolute origin
	auto old_abs_origin = HACKS->local->get_abs_origin();
	HACKS->local->set_abs_origin(initial_vars->origin);

	auto eye_pos = local_anims_data->eye_pos;

	// Restore pose parameters
	RESTORE(HACKS->local->pose_parameter()[pose_param_body_pitch]);

	float pose = 0.f;
	float normalized_pitch = pitch_angle;

	if (normalized_pitch > 180.f)
		normalized_pitch -= 360.f;

	normalized_pitch = std::clamp(normalized_pitch, -90.f, 90.f);
	normalized_pitch = HACKS->local->studio_set_pose_parameter(pose_param_body_pitch, normalized_pitch, pose);

	HACKS->local->pose_parameter()[pose_param_body_pitch] = pose;

	// Set the absolute angles
	HACKS->local->set_abs_angles({ 0.f, anim_state->abs_yaw, 0.f });

	auto hdr = HACKS->local->get_studio_hdr();
	if (hdr)
	{
		auto bone_flags_base = hdr->bone_flags().base();
		auto bone_parent_count = hdr->bone_parent_count();
		local_anims_data->bone_builder.store(HACKS->local, HACKS->local->bone_cache().base(), 0x7FF00, hdr, bone_flags_base, bone_parent_count);
		local_anims_data->bone_builder.setup();

		clamp_bones_info_t info{};
		info.store(HACKS->local);
		local_anims_data->bone_builder.clamp_bones_in_bbox(HACKS->local, HACKS->local->bone_cache().base(), 0x7FF00, HACKS->tickbase, HACKS->local->eye_angles(), info);

		modify_eye_pos(eye_pos, HACKS->local->bone_cache().base());
	}

	// Restore original absolute origin
	HACKS->local->set_abs_origin(old_abs_origin);
	return eye_pos;
}

void c_animation_fix::update_local() {
	// Validate delta tick
	if (HACKS->client_state->delta_tick == -1)
		return;

	// Get animation state
	auto state = HACKS->local->animstate();
	if (!state)
		return;

	auto anim = &anims[HACKS->local->index()];
	if (anim->ptr != HACKS->local) {
		anim->reset();
		local_anims.reset();
		state->reset();
		anim->ptr = HACKS->local;
		return;
	}

	// Check spawn time and reset if changed
	if (anim->old_spawn_time != HACKS->local->spawn_time()) {
		state->reset();
		local_anims.reset();
		anim->ptr = HACKS->local;
		anim->old_spawn_time = HACKS->local->spawn_time();
		return;
	}

	anim->update_anims = false;
	anim->setup_bones = false;
	anim->dormant_ticks = 1;

	// Backup current variables
	RESTORE(HACKS->local->render_angles());
	RESTORE(HACKS->local->eye_angles());
	RESTORE(HACKS->global_vars->curtime);
	RESTORE(HACKS->global_vars->frametime);

	// Update global variables
	HACKS->global_vars->curtime = TICKS_TO_TIME(HACKS->tickbase);
	HACKS->global_vars->frametime = HACKS->global_vars->interval_per_tick;

	// Update viewmodel
	auto viewmodel = (c_base_entity*)(HACKS->entity_list->get_client_entity_handle(HACKS->local->viewmodel()));
	if (viewmodel) {
		offsets::update_all_viewmodel_addons.cast<int(__fastcall*)(void*)>()(viewmodel);
	}

	// **Store initial layers and poses**
	HACKS->local->store_layers(local_anims.backup_layers);
	HACKS->local->store_poses(&local_anims.poses);

	vec3_t eye_angles{};
	if (HACKS->cmd->command_number >= HACKS->shot_cmd &&
		HACKS->shot_cmd >= HACKS->cmd->command_number - HACKS->client_state->choked_commands) {
		eye_angles = HACKS->input->get_user_cmd(HACKS->shot_cmd)->viewangles;
	}
	else {
		eye_angles = HACKS->cmd->viewangles;
	}

	HACKS->local->render_angles() = HACKS->local->eye_angles() = eye_angles;

	local_anims.old_vars.store(HACKS->cmd);
	{
		auto unpred_vars = ENGINE_PREDICTION->get_initial_vars();
		unpred_vars->restore(true);

		// Ensure animation state consistency
		if (state->last_update_time == HACKS->global_vars->curtime)
			state->last_update_time = HACKS->global_vars->curtime + HACKS->global_vars->interval_per_tick;

		if (state->last_update_frame == HACKS->global_vars->framecount)
			state->last_update_frame = HACKS->global_vars->framecount - 1;

		// **Update animation state**
		state->update(HACKS->local->eye_angles());
	}
	local_anims.old_vars.restore(true);

	auto hdr = HACKS->local->get_studio_hdr();
	if (hdr) {
		auto bone_flags_base = hdr->bone_flags().base();
		auto bone_parent_count = hdr->bone_parent_count();
		const auto& abs_origin = HACKS->local->get_abs_origin();

		if (*HACKS->send_packet) {
			// **Rebuild bones**
			local_anims.bone_builder.store(HACKS->local, local_anims.matrix, 0x7FF00, hdr, bone_flags_base, bone_parent_count);
			local_anims.bone_builder.setup();

			auto speed_portion_walk = state->speed_as_portion_of_walk_top_speed;
			auto speed_portion_duck = state->speed_as_portion_of_crouch_top_speed;
			auto transition = state->walk_run_transition;
			auto duck_amount = state->anim_duck_amount;

			local_anims.foot_yaw = state->abs_yaw;
			local_anims.aim_matrix_width_range = math::lerp(std::clamp(speed_portion_walk, 0.f, 1.f), 1.f, math::lerp(transition, 0.8f, 0.5f));

			if (duck_amount > 0)
				local_anims.aim_matrix_width_range = math::lerp(duck_amount * std::clamp(speed_portion_duck, 0.f, 1.f), local_anims.aim_matrix_width_range, 0.5f);

			local_anims.max_desync_range = state->aim_yaw_max * local_anims.aim_matrix_width_range;
			math::change_bones_position(local_anims.matrix, 128, abs_origin, {});
		}
	}

	// **Restore initial layers and poses after updating bones**
	HACKS->local->set_layers(local_anims.backup_layers);
	HACKS->local->set_poses(&local_anims.poses);
}

void c_animation_fix::render_matrices(c_cs_player* player)
{
	// force matrix pos, fix jitter attachments, etc
	auto force_bone_cache = [&](matrix3x4_t* matrix)
	{
		RESTORE(HACKS->global_vars->curtime);

		// process attachments at correct timings
		HACKS->global_vars->curtime = player->sim_time();

		player->invalidate_bone_cache();
		player->interpolate_moveparent_pos();
		player->set_bone_cache(matrix);
		player->attachments_helper();
	};

	auto anim = &anims[player->index()];
	auto local_anim = &local_anims;

	if (!player->is_alive())
	{
		anim->setup_bones = anim->clamp_bones_in_bbox = true;
		return;
	}

	if (player == HACKS->local)
	{
		const auto& abs_origin = player->get_abs_origin();

		// adjust render matrix pos
		math::change_bones_position(local_anim->matrix, 128, {}, abs_origin);

		force_bone_cache(local_anim->matrix);

		// restore matrix pos
		math::change_bones_position(local_anim->matrix, 128, abs_origin, {});
		return;
	}

	if (!anim->ptr || anim->ptr != player || anim->dormant_ticks < 1)
	{
		anim->setup_bones = anim->clamp_bones_in_bbox = true;
		return;
	}

	if (!anim->records.empty())
	{
		static matrix3x4_t render_matrix[128]{};

		auto record = &anim->records.front();

		// copy matrix for render
		math::memcpy_sse(render_matrix, record->matrix_orig.matrix, sizeof(render_matrix));

		// adjust render matrix pos
		math::change_bones_position(render_matrix, 128, record->origin, player->get_render_origin());

		force_bone_cache(render_matrix);

		math::change_bones_position(render_matrix, 128, player->get_render_origin(), record->origin);
	}
	else
		anim->setup_bones = anim->clamp_bones_in_bbox = true;
}

void c_animation_fix::restore_all()
{
	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			auto anim = get_anims(player->index());
			auto backup = get_backup_record(player->index());

			if (anim->ptr != player)
				return;

			if (!anim->ptr)
				return;

			if (!player->is_alive())
				return;

			if (player->dormant())
				return;

			auto& layer = player->animlayers()[ANIMATION_LAYER_ALIVELOOP];
			if (layer.cycle == anim->old_aliveloop_cycle && layer.playback_rate == anim->old_aliveloop_rate)
				return;

			if (player->sim_time() == player->old_sim_time())
				return;

			backup->restore(player);
		});
}