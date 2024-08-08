#include "globals.hpp"
#include "server_bones.hpp"
#include "animations.hpp"
#include "lagcomp.hpp"
#include "entlistener.hpp"
#include "resolver.hpp"
#include "threads.hpp"
#include "engine_prediction.hpp"
#include "ragebot.hpp"

// forward declarations for resolver
void resolve(c_cs_player* player, anim_record_t* current);

void fix_velocity(anim_record_t* old_record, anim_record_t* last_record, anim_record_t* record, c_cs_player* player)
{
	auto is_float_invalid = [](float value) -> bool
		{
			return isnan(value) || isinf(value);
		};

	auto state = player->animstate();
	if (!state)
		return;

	auto weapon = (c_base_combat_weapon*)(HACKS->entity_list->get_client_entity_handle(player->active_weapon()));
	if (!weapon)
		return;

	auto weapon_info = HACKS->weapon_system->get_weapon_data(weapon->item_definition_index());

	if (player->effects().has(EF_NOINTERP) || player->no_interp_parity() != player->no_interp_parity_old()) {
		record->velocity.reset();
		return;
	}

	auto prev_record = last_record;
	auto time_delta = TICKS_TO_TIME(record->choke);

	auto max_speed = weapon && weapon_info ?
		std::max<float>((player->is_scoped() ? weapon_info->max_speed_alt : weapon_info->max_speed), 0.001f)
		: CS_PLAYER_SPEED_RUN;

	if (prev_record && record->choke > 1)
	{
		auto animation_speed = 0.0f;

		auto origin_delta = player->origin() - prev_record->origin;
		auto time_difference = player->sim_time() - prev_record->sim_time;

		if (!origin_delta.valid())
		{
			auto was_in_ground = record->flags.has(FL_ONGROUND) && prev_record->flags.has(FL_ONGROUND);

			if (TIME_TO_TICKS(time_difference) > 1 && TIME_TO_TICKS(time_difference) < 20)
			{
				player->velocity() = origin_delta / time_difference;

				if (old_record)
				{
					auto origin_diff{ (prev_record->origin - old_record->origin) };
					auto choke_time{ 1.0f / TICKS_TO_TIME(prev_record->choke) };
					auto previous_velocity = vec3_t{ origin_diff.x * choke_time, origin_diff.y * choke_time, origin_diff.z * choke_time };

					if (!previous_velocity.valid())
					{
						auto real_velocity = player->velocity().length_2d();

						vec3_t velocity_angle, previous_velocity_angle;
						math::vector_angles(player->velocity(), velocity_angle);
						math::vector_angles(prev_record->velocity, previous_velocity_angle);

						auto delta = math::normalize_yaw(velocity_angle.y - previous_velocity_angle.y);
						auto velocity_direction = math::normalize_yaw(delta * 0.5f + velocity_angle.y);

						if (abs(delta) < 80.f)
						{
							vec3_t angle, direction;
							angle.y = velocity_direction;
							math::angle_vectors(angle, direction);

							player->velocity().x = direction.x * real_velocity;
							player->velocity().y = direction.y * real_velocity;
						}
					}
				}
			}

			if (record->flags.has(FL_ONGROUND))
			{
				if (record->layers[11].weight > 0.0f && record->layers[11].weight < 1.0f &&
					record->layers[11].weight != prev_record->layers[11].weight &&
					record->layers[11].playback_rate == prev_record->layers[11].playback_rate)
				{
					auto modifier = 0.35f * (1.0f - record->layers[11].weight);

					if (modifier > 0.0f && modifier < 1.0f)
						animation_speed = max_speed * (modifier + 0.55f);
				}

				if (animation_speed > 0.0f)
				{
					animation_speed /= player->velocity().length_2d();

					player->velocity().x *= animation_speed;
					player->velocity().y *= animation_speed;
				}
				else
				{
					auto weight_speed = record->layers[6].weight;

					if (weight_speed > 0.0f && weight_speed <= 0.1f)
					{
						if (record->flags.has(FL_DUCKING))
							weight_speed *= 0.34f;
						else if (player->is_walking())
							weight_speed *= 0.52f;

						if (weight_speed > 0.0f)
						{
							player->velocity().x = (player->velocity().x / player->velocity().length()) * max_speed * weight_speed;
							player->velocity().y = (player->velocity().y / player->velocity().length()) * max_speed * weight_speed;
						}
					}
				}

				player->velocity().z = 0.0f;
			}
			else
			{
				bool jumped = player->get_sequence_activity(record->layers[4].sequence) == ACT_CSGO_JUMP;

				if (record->layers[4].cycle == prev_record->layers[4].cycle &&
					record->layers[4].sequence == prev_record->layers[4].sequence ||
					record->layers[4].cycle >= prev_record->layers[4].cycle)
					jumped = false;

				float new_z = (origin_delta.z / TICKS_TO_TIME(record->choke));

				if (!jumped)
				{
					auto gravity = HACKS->convars.sv_gravity->get_float();
					gravity = (gravity * -1.f) * TICKS_TO_TIME(record->choke);
					new_z = ((origin_delta.z / TICKS_TO_TIME(record->choke)) - (gravity * 0.5f)) + gravity;

					player->velocity().z = std::min(new_z, HACKS->convars.sv_jump_impulse->get_float());
				}
				else if (prev_record->flags.has(FL_ONGROUND))
				{
					float time_in_air = std::max(HACKS->global_vars->interval_per_tick, record->layers[4].cycle / record->layers[4].playback_rate);

					auto gravity = (HACKS->convars.sv_gravity->get_float() * -1.f) * time_in_air;
					new_z = ((origin_delta.z / time_in_air) - (gravity * 0.5f)) + gravity;
					player->velocity().z = std::min(new_z, HACKS->convars.sv_jump_impulse->get_float());
				}
				else
				{
					float time_in_air = std::max(HACKS->global_vars->interval_per_tick, record->layers[4].cycle / record->layers[4].playback_rate);
					auto gravity_inverted = (HACKS->convars.sv_gravity->get_float() * -1.f);

					c_game_trace tr;
					c_trace_filter filter;
					filter.skip = player;

					vec3_t start = (prev_record->origin + player->origin()) * 0.5f;
					vec3_t end = vec3_t(player->origin().x, player->origin().y,
						abs((((gravity_inverted * 0.5f) * time_in_air) * time_in_air) +
							(HACKS->convars.sv_jump_impulse->get_float() * time_in_air)) - player->origin().z);

					HACKS->engine_trace->trace_ray(ray_t(start, end, player->bb_mins(), player->bb_maxs()), MASK_PLAYERSOLID, &filter, &tr);

					if (tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f)
					{
						new_z = ((player->origin().z - tr.end.z) / time_in_air) - ((gravity_inverted * time_in_air) * 0.5f);
						player->velocity().z = std::min(new_z, (gravity_inverted * time_in_air) + new_z);
					}
					else
					{
						player->velocity().z = (gravity_inverted * time_in_air) + HACKS->convars.sv_jump_impulse->get_float();
					}
				}
			}
		}
	}

	if (record->flags.has(FL_ONGROUND) && player->velocity().length() > 0.0f && record->layers[6].playback_rate <= 0.0f)
		player->velocity().reset();

	if (is_float_invalid(player->velocity().x) || is_float_invalid(player->velocity().y) || is_float_invalid(player->velocity().z))
		player->velocity().reset();

	if (record->flags.has(FL_ONGROUND) && player->velocity().length() < 0.1f &&
		record->layers[6].playback_rate != 0.f && record->layers[6].weight != 0.f)
		player->velocity() = vec3_t(1.1f, 0, 0);

	record->velocity = player->velocity();
}


static INLINE void update_sides(bool should_update, c_cs_player* player, anims_t* anim, anim_record_t* new_record, anim_record_t* last_record, c_studio_hdr* hdr, int* flags_base, int parent_count)
{
	auto state = player->animstate();
	if (!state)
		return;

	float original_poses[24];
	c_animation_layers original_layers[13];
	player->store_poses(original_poses);
	player->store_layers(original_layers);

	const auto backup_lower_body_yaw_target = player->lower_body_yaw();
	const auto backup_duck_amount = player->duck_amount();
	const auto backup_flags = player->flags();
	const auto backup_velocity = player->velocity();

	auto backup_curtime = HACKS->global_vars->curtime;
	auto backup_frametime = HACKS->global_vars->frametime;

	HACKS->global_vars->curtime = new_record->sim_time;
	HACKS->global_vars->frametime = HACKS->global_vars->interval_per_tick;

	if (!last_record)
	{
		state->primary_cycle = new_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].cycle;
		state->move_weight = new_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].weight;
		state->last_update_time = (new_record->sim_time - HACKS->global_vars->interval_per_tick);

		if (player->flags().has(FL_ONGROUND)) {
			state->on_ground = true;
			state->landing = false;
		}
	}
	else
	{
		state->primary_cycle = last_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].cycle;
		state->move_weight = last_record->layers[ANIMATION_LAYER_MOVEMENT_MOVE].weight;
		state->strafe_sequence = last_record->layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].sequence;
		state->strafe_change_weight = last_record->layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].weight;
		state->strafe_change_cycle = last_record->layers[ANIMATION_LAYER_MOVEMENT_STRAFECHANGE].cycle;
		state->acceleration_weight = last_record->layers[ANIMATION_LAYER_LEAN].cycle;
	}

	if (should_update)
	{
		if (!last_record || new_record->choke < 2)
		{
			resolve(player, new_record/*, last_record*/);

			player->set_abs_origin(player->origin());
			player->effects().remove(0x1000u);
			player->abs_velocity() = player->velocity() = new_record->velocity;
			player->force_update_animations(anim);
		}
		else
		{
			auto choke_float = static_cast<float>(new_record->choke);
			auto simulation_time_tick = TIME_TO_TICKS(new_record->sim_time);
			auto prev_simulation_time = simulation_time_tick - new_record->choke;

			for (int i = 0; i <= new_record->choke; ++i)
			{
				auto current_command_tick = prev_simulation_time + i;
				auto current_command_time = TICKS_TO_TIME(current_command_tick);

				auto current_float_tick = static_cast<float>(i);
				float lerp_step = current_float_tick / choke_float;

				auto lerp_origin = math::lerp(lerp_step, last_record->origin, new_record->origin);
				auto lerp_velocity = math::lerp(lerp_step, last_record->velocity, new_record->velocity);
				auto lerp_duck_amount = math::lerp(lerp_step, last_record->duck_amt, new_record->duck_amt);

				player->origin() = lerp_origin;
				player->effects().remove(0x1000u);
				player->abs_velocity() = player->velocity() = lerp_velocity;
				player->duck_amount() = lerp_duck_amount;

				player->set_abs_origin(player->origin());

				RESTORE(player->sim_time());
				player->sim_time() = current_command_time;

				resolve(player, new_record/*, last_record*/);
				player->force_update_animations(anim);
			}
		}
	}

	auto collideable = player->get_collideable();
	if (collideable)
		offsets::set_collision_bounds.cast<void(__thiscall*)(void*, vec3_t*, vec3_t*)>()(collideable, &player->bb_mins(), &player->bb_maxs());

	new_record->collision_change_origin = player->collision_change_origin();
	new_record->collision_change_time = player->collision_change_time();

	player->invalidate_bone_cache();
	{
		new_record->layers[ANIMATION_LAYER_LEAN].weight = player->animlayers()[ANIMATION_LAYER_LEAN].weight = 0.f;
		new_record->layers[ANIMATION_LAYER_LEAN].cycle = player->animlayers()[ANIMATION_LAYER_LEAN].cycle = 0.f;

		anim->setup_bones = false;

		auto matrix_side = &new_record->matrix_orig;

		player->store_layers(matrix_side->layers);
		player->set_layers(new_record->layers);

		matrix_side->bone_builder.store(player, matrix_side->matrix, 0x7FF00, hdr, flags_base, parent_count);
		matrix_side->bone_builder.setup();
	}

	player->set_poses(original_poses);
	player->set_layers(original_layers);
}

void setup_layers(c_cs_player* player, anim_record_t* new_record) {
	auto state = player->animstate();
	if (!state || !new_record)
		return;

	// Setup layers shit
	c_animation_state old_state{ };
	std::memcpy(&old_state, state, sizeof(c_animation_state));
	state->abs_yaw = math::normalize_yaw(player->eye_angles().y + 58.f);
	player->update_client_side_animation();
	player->store_layers(new_record->layers_right);

	state->abs_yaw = math::normalize_yaw(player->eye_angles().y - 58.f);
	player->update_client_side_animation();
	player->store_layers(new_record->layers_left);

	state->abs_yaw = math::normalize_yaw(player->eye_angles().y);
	player->update_client_side_animation();
	player->store_layers(new_record->layers_orig);

	state->abs_yaw = math::normalize_yaw(player->eye_angles().y + 29.f);
	player->update_client_side_animation();
	player->store_layers(new_record->layers_low);
	std::memcpy(state, &old_state, sizeof(c_animation_state));
}

void thread_collect_info(c_cs_player* player)
{
	auto state = player->animstate();
	if (!state)
		return;

	auto index = player->index();

	auto anim = ANIMFIX->get_anims(index);
	auto backup = ANIMFIX->get_backup_record(index);
	if (anim->ptr != player)
	{
		state->reset();
		anim->reset();
		backup->reset();
		anim->ptr = player;
		anim->update_anims = anim->setup_bones = true;
		return;
	}

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

	if (!player->is_alive())
	{
		state->reset();
		anim->reset();
		backup->reset();
		return;
	}

	if (player->dormant())
	{
		anim->old_simulation_time = 0.f;
		anim->dormant_ticks = 0;
		anim->records.clear();
		backup->reset();

		anim->update_anims = anim->setup_bones = true;
		return;
	}

	if (anim->dormant_ticks < 3)
		++anim->dormant_ticks;

	if (player->sim_time() == player->old_sim_time())
		return;

	if (anim->old_spawn_time != player->spawn_time())
	{
		state->player = player;
		state->reset();

		anim->old_spawn_time = player->spawn_time();
		return;
	}

	auto max_ticks_size = HACKS->tick_rate;

	player->store_layers(resolver_info[player->index()].initial_layers);
	player->update_weapon_dispatch_layers();

	anim_record_t* last_record = nullptr;
	anim_record_t* old_record = nullptr;
	if (!anim->records.empty())
	{
		last_record = &anim->records.front();

		if (anim->records.size() >= 2)
			old_record = &anim->records[1];
	}

	auto& new_record = anim->records.emplace_front();
	new_record.dormant = anim->dormant_ticks < 2;
	new_record.store(player);
	new_record.shifting = false;

	if (last_record)
	{
		setup_layers(player, &new_record);
		fix_velocity(old_record, last_record, &new_record, player);

		auto layer_alive_loop = &new_record.layers[ANIMATION_LAYER_ALIVELOOP];
		auto old_layer_alive_loop = &last_record->layers[ANIMATION_LAYER_ALIVELOOP];

		auto current_playback_rate = layer_alive_loop->playback_rate;
		auto previous_playback_rate = old_layer_alive_loop->playback_rate;

		auto current_cycle = layer_alive_loop->cycle;
		auto previous_cycle = static_cast<int>(old_layer_alive_loop->cycle / (HACKS->global_vars->interval_per_tick * previous_playback_rate) + 0.5f);

		auto cycle = 0;

		if (current_playback_rate == previous_playback_rate)
			cycle = static_cast<int>(current_cycle / (current_playback_rate * HACKS->global_vars->interval_per_tick) + 0.5f);
		else
			cycle = static_cast<int>(previous_cycle + ((current_cycle / current_playback_rate + (1.f - old_layer_alive_loop->cycle) / previous_playback_rate) / HACKS->global_vars->interval_per_tick + 0.5f));

		auto layer_delta = cycle - previous_cycle;
		if (layer_delta <= 18)
			new_record.choke = std::max(layer_delta, 1);

		new_record.shooting = new_record.last_shot_time > last_record->sim_time && new_record.last_shot_time <= last_record->sim_time;
		if (!new_record.shooting)
			new_record.last_reliable_angle = player->eye_angles();

		new_record.choke = std::clamp(new_record.choke, 0, 16);
	}

	auto bone_flags_base = hdr->bone_flags().base();
	auto bone_parent_count = hdr->bone_parent_count();

	bool should_update = true;

	for (int i = 0; i < 13; i++)
	{
		auto layer = &player->animlayers()[i];
		layer->owner = player;
		layer->studio_hdr = player->get_studio_hdr();
	}

	backup->store(player);
	{
		math::memcpy_sse(&anim->old_animstate, player->animstate(), sizeof(anim->old_animstate));
		update_sides(should_update, player, anim, &new_record, last_record, hdr, bone_flags_base, bone_parent_count);
		math::memcpy_sse(player->animstate(), &anim->old_animstate, sizeof(anim->old_animstate));
	}

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
		}
	}
	else
		new_record.shifting = false;

	backup->restore(player);

	if (anim->records.size() > max_ticks_size - 1)
		anim->records.erase(anim->records.end() - 1);
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