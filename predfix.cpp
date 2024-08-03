#include "globals.hpp"
#include "predfix.hpp"

// fix all wrong values for predicted netvars
void c_prediction_fix::store(int tick)
{
	auto netvars = &compressed_netvars[tick % 150];
	netvars->store(tick);
}

void c_prediction_fix::fix_netvars(int tick)
{
	pred_error_occured = false;

	auto netvars = &compressed_netvars[tick % 150];
	if (netvars->cmd_number != tick)
		return;

	auto aim_punch_diff = HACKS->local->aim_punch_angle() - netvars->aimpunch;
	auto view_punch_diff = HACKS->local->view_punch_angle() - netvars->viewpunch;
	auto aim_punch_vel_diff = HACKS->local->aim_punch_angle_vel() - netvars->aimpunch_vel;
	auto view_offset_diff = HACKS->local->view_offset() - netvars->viewoffset;
	auto fall_velocity_diff = HACKS->local->fall_velocity() - netvars->fall_velocity;
	auto duck_diff = HACKS->local->duck_amount() - netvars->duck_amount;

	{
		if (abs(aim_punch_diff.x) <= 0.03125f && abs(aim_punch_diff.y) <= 0.03125f && abs(aim_punch_diff.z) <= 0.03125f)
			HACKS->local->aim_punch_angle() = netvars->aimpunch;
		else
			pred_error_occured = true;

		if (std::abs(view_punch_diff.x) <= 0.03125f && std::abs(view_punch_diff.y) <= 0.03125f && std::abs(view_punch_diff.z) <= 0.03125f)
			HACKS->local->view_punch_angle() = netvars->viewpunch;
		else
			pred_error_occured = true;

		if (std::abs(aim_punch_vel_diff.x) <= 0.03125f && std::abs(aim_punch_vel_diff.y) < 0.03125f && std::abs(aim_punch_vel_diff.z) <= 0.03125f)
			HACKS->local->aim_punch_angle_vel() = netvars->aimpunch_vel;
		else
			pred_error_occured = true;

		if (std::abs(view_offset_diff.z) <= 0.065f)
			HACKS->local->view_offset() = netvars->viewoffset;

		if (std::abs(fall_velocity_diff) <= 0.5f)
			HACKS->local->fall_velocity() = netvars->fall_velocity;

		if (abs(duck_diff) < 0.03125f) {
			HACKS->local->duck_amount() = netvars->duck_amount;
			HACKS->local->duck_speed() = netvars->duck_speed;
		}
	}
}