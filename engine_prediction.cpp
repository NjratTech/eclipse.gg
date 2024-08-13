#include "globals.hpp"
#include "engine_prediction.hpp"
#include "exploits.hpp"
#include "predfix.hpp"

void c_engine_prediction::update()
{
	if (HACKS->client_state->signon_state < 6)
		return;

	HACKS->prediction->update(HACKS->client_state->delta_tick,
		HACKS->client_state->delta_tick > 0,
		HACKS->client_state->last_command_ack,
		HACKS->client_state->last_outgoing_command + HACKS->client_state->choked_commands);
}

void c_engine_prediction::start()
{
	if (!prediction_random_seed)
		prediction_random_seed = *offsets::prediction_random_seed.cast<int**>();

	if (!prediction_player)
		prediction_player = *offsets::prediction_player.cast<int**>();

	in_prediction = true;

	unpred_vars.store();
	initial_vars.store(HACKS->cmd);

	restore_vars[HACKS->cmd->command_number % 150].store(HACKS->cmd);

	HACKS->global_vars->curtime = TICKS_TO_TIME(HACKS->tickbase);
	HACKS->global_vars->frametime = HACKS->local->flags().has(FL_FROZEN) ? 0.f : HACKS->global_vars->interval_per_tick;

	*prediction_random_seed = MD5_PseudoRandom(HACKS->cmd->command_number) & 0x7FFFFFFF;
	*prediction_player = (int)HACKS->local;

	HACKS->prediction->in_prediction = true;
	HACKS->prediction->is_first_time_predicted = false;

	auto old_tickbase = HACKS->local->tickbase();

	std::memset(&move_data, 0, sizeof(c_move_data));
	HACKS->cmd->buttons.force(HACKS->local->button_forced());
	HACKS->cmd->buttons.remove(HACKS->local->button_disabled());

	auto& net_vars = networked_vars[HACKS->cmd->command_number % 150];
	net_vars.ground_entity = HACKS->local->ground_entity();

	HACKS->game_movement->start_track_prediction_errors(HACKS->local);
	HACKS->move_helper->set_host(HACKS->local);

	* (c_user_cmd**)((std::uintptr_t)HACKS->local + XORN(0x3348)) = HACKS->cmd;
	*(c_user_cmd*)((std::uintptr_t)HACKS->local + XORN(0x3298)) = *HACKS->cmd;

	HACKS->cmd->buttons.force(HACKS->local->button_forced());
	HACKS->cmd->buttons.remove(HACKS->local->button_disabled());

	const int buttons = HACKS->cmd->buttons.bits;
	const int local_buttons = *HACKS->local->buttons();
	const int buttons_changed = buttons ^ local_buttons;

	HACKS->local->button_last() = local_buttons;
	*HACKS->local->buttons() = buttons;
	HACKS->local->button_pressed() = buttons_changed & buttons;
	HACKS->local->button_released() = buttons_changed & (~buttons);

	HACKS->prediction->check_moving_ground(HACKS->local, HACKS->global_vars->frametime);
	HACKS->prediction->set_local_view_angles(HACKS->cmd->viewangles);

	HACKS->local->run_pre_think();
	HACKS->local->run_think();

	HACKS->prediction->setup_move(HACKS->local, HACKS->cmd, HACKS->move_helper, &move_data);
	HACKS->game_movement->process_movement(HACKS->local, &move_data);
	HACKS->prediction->finish_move(HACKS->local, HACKS->cmd, &move_data);

	HACKS->move_helper->process_impacts();

	HACKS->local->run_post_think();

	HACKS->game_movement->finish_track_prediction_errors(HACKS->local);
	HACKS->move_helper->set_host(nullptr);

	net_vars.store(HACKS->cmd);

	HACKS->local->tickbase() = old_tickbase;

	if (HACKS->weapon)
	{
		HACKS->weapon->update_accuracy_penalty();

		auto weapon_id = HACKS->weapon->item_definition_index();

		auto is_special_weapon = weapon_id == 9
			|| weapon_id == 11
			|| weapon_id == 38
			|| weapon_id == 40;

		HACKS->ideal_inaccuracy = 0.f;

		if (weapon_id == WEAPON_SSG08 && !HACKS->local->flags().has(FL_ONGROUND))
			HACKS->ideal_inaccuracy = 0.00875f;
		else if (HACKS->local->flags().has(FL_DUCKING))
		{
			if (is_special_weapon)
				HACKS->ideal_inaccuracy = HACKS->weapon_info->inaccuracy_crouch_alt;
			else
				HACKS->ideal_inaccuracy = HACKS->weapon_info->inaccuracy_crouch;
		}
		else if (is_special_weapon)
			HACKS->ideal_inaccuracy = HACKS->weapon_info->inaccuracy_stand_alt;
		else
			HACKS->ideal_inaccuracy = HACKS->weapon_info->inaccuracy_stand;
	}
}

void c_engine_prediction::update_viewmodel_info(c_user_cmd* cmd)
{
	auto viewmodel = (c_cs_player*)(HACKS->entity_list->get_client_entity_handle(HACKS->local->viewmodel()));
	if (!viewmodel)
		return;

	auto& current_viewmodel = viewmodel_info[cmd->command_number % 150];
	current_viewmodel.store(cmd, viewmodel);

	if (cmd->weapon_select)
	{
		auto previous_command = (cmd->command_number - 1);
		auto previous_viewmodel = viewmodel_info[previous_command % 150];

		if (previous_viewmodel.cmd_tick == previous_command && previous_viewmodel.model_index != viewmodel->model_index())
		{
			auto round_sequence = (previous_viewmodel.new_sequence_parity + 1) & 7;
			if (((round_sequence + 1) & 7) == current_viewmodel.new_sequence_parity)
			{
				current_viewmodel.new_sequence_parity = round_sequence;
				viewmodel->new_sequence_parity() = round_sequence;
			}
		}
	}
}

void c_engine_prediction::fix_viewmodel(int stage)
{
	if (stage != XORN(FRAME_NET_UPDATE_POSTDATAUPDATE_START))
		return;

	if (!HACKS->in_game)
		return;

	if (!HACKS->local || !HACKS->local->is_alive() || !HACKS->weapon)
		return;

	auto viewmodel = (c_base_entity*)(HACKS->entity_list->get_client_entity_handle(HACKS->local->viewmodel()));
	if (!viewmodel)
		return;

	auto command = HACKS->client_state->command_ack;
	auto current_viewmodel = &viewmodel_info[command % 150];
	if (current_viewmodel->cmd_tick != command)
		return;

	auto cycle = viewmodel->cycle();
	auto old_cycle = viewmodel->old_cycle();
	auto anim_time = viewmodel->anim_time();
	auto old_anim_time = viewmodel->old_anim_time();

	if (anim_time != old_anim_time
		&& cycle != old_cycle
		&& cycle == 0.f && anim_time == HACKS->global_vars->curtime)
	{
		if (current_viewmodel->active_weapon == HACKS->weapon
			&& current_viewmodel->sequence == viewmodel->sequence()
			&& current_viewmodel->animation_parity == viewmodel->animation_parity()
			&& current_viewmodel->new_sequence_parity == viewmodel->new_sequence_parity()
			&& current_viewmodel->model_index == viewmodel->model_index()
			|| current_viewmodel->looking_at_weapon == HACKS->local->looking_at_weapon())
		{
			viewmodel->anim_time() = viewmodel->old_anim_time();
			viewmodel->cycle() = viewmodel->old_cycle();
		}
	}
}

void c_engine_prediction::repredict()
{
	if (!HACKS->weapon)
		return;

	auto old_tickbase = HACKS->local->tickbase();
	std::memset(&move_data, 0, sizeof(c_move_data));

	HACKS->game_movement->start_track_prediction_errors(HACKS->local);

	move_data.forwardmove = HACKS->cmd->forwardmove;
	move_data.sidemove = HACKS->cmd->sidemove;
	move_data.upmove = HACKS->cmd->upmove;
	move_data.buttons = HACKS->cmd->buttons.bits;
	move_data.view_angles = HACKS->cmd->viewangles;
	move_data.angles = HACKS->cmd->viewangles;

	HACKS->move_helper->set_host(HACKS->local);
	HACKS->prediction->setup_move(HACKS->local, HACKS->cmd, HACKS->move_helper, &move_data);
	HACKS->game_movement->process_movement(HACKS->local, &move_data);
	HACKS->prediction->finish_move(HACKS->local, HACKS->cmd, &move_data);
	HACKS->game_movement->finish_track_prediction_errors(HACKS->local);
	HACKS->move_helper->set_host(nullptr);

	if (HACKS->weapon)
		HACKS->weapon->update_accuracy_penalty();
}

void c_engine_prediction::end()
{
	in_prediction = false;

	* (c_user_cmd**)((std::uintptr_t)HACKS->local + XORN(0x3348)) = nullptr;

	* prediction_random_seed = -1;
	*prediction_player = 0;

	unpred_vars.restore();

	HACKS->game_movement->reset();
}