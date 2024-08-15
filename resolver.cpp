#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"
#include <numeric>

namespace resolver
{
	inline void prepare_jitter(c_cs_player* player, resolver_info_t& resolver_info, anim_record_t* current)
	{
		auto& jitter = resolver_info.jitter;
		auto diff = math::angle_diff(player->eye_angles().y, current->eye_angles.y);

		jitter.angles.push_back(diff);
		if (jitter.angles.size() > 5)
			jitter.angles.pop_front();

		jitter.is_jitter = false;
		if (jitter.angles.size() >= 3)
		{
			float avg = std::accumulate(jitter.angles.begin(), jitter.angles.end(), 0.0f) / jitter.angles.size();
			float variance = 0.0f;
			for (const auto& angle : jitter.angles)
				variance += std::pow(angle - avg, 2);
			variance /= jitter.angles.size();

			jitter.is_jitter = variance > 15.0f;
		}

		if (jitter.is_jitter)
			jitter.jitter_side = diff > 0 ? 1 : -1;
	}

	inline void freestand_resolve(c_cs_player* player, resolver_info_t& resolver_info)
	{
		constexpr float RANGE = 32.f;

		vec3_t src = player->get_eye_position();
		vec3_t forward, right, up;
		math::angle_vectors(player->eye_angles(), &forward, &right, &up);

		vec3_t left_end = src + (right * -RANGE);
		vec3_t right_end = src + (right * RANGE);

		float left_fraction = 0.f, right_fraction = 0.f;
		 
		for (float i = 0.f; i < 1.f; i += 0.1f)
		{
			vec3_t left_point = src + (left_end - src) * i;
			vec3_t right_point = src + (right_end - src) * i;

			c_game_trace left_trace, right_trace;
			ray_t left_ray, right_ray;

			left_ray.init(left_point, left_point + forward * 100.f);
			right_ray.init(right_point, right_point + forward * 100.f);

			HACKS->engine_trace->trace_ray(left_ray, MASK_SHOT_HULL | CONTENTS_HITBOX, nullptr, &left_trace);
			HACKS->engine_trace->trace_ray(right_ray, MASK_SHOT_HULL | CONTENTS_HITBOX, nullptr, &right_trace);

			left_fraction += left_trace.fraction;
			right_fraction += right_trace.fraction;
		}

		if (left_fraction > right_fraction)
			resolver_info.side = 1;
		else if (right_fraction > left_fraction)
			resolver_info.side = -1;
		else
			resolver_info.side = 0;

		resolver_info.mode = XOR("freestand");
		resolver_info.resolved = true;
		resolver_info.freestanding.updated = true;
		resolver_info.freestanding.update_time = HACKS->global_vars->curtime;
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
		if (!state)
		{
			if (info.resolved)
				info.reset();

			return;
		}

		auto hdr = player->get_studio_hdr();
		if (!hdr)
			return;

		if (current->choke < 2)
			info.add_legit_ticks();
		else
			info.add_fake_ticks();

		if (info.is_legit())
		{
			info.resolved = false;
			info.mode = XOR("no fake");
			return;
		}

		prepare_jitter(player, info, current);
		auto& jitter = info.jitter;
		if (jitter.is_jitter)
		{
			info.side = jitter.jitter_side;
			info.resolved = true;
			info.mode = XOR("jitter");
		}
		else
		{
			auto& misses = RAGEBOT->missed_shots[player->index()];
			if (misses > 0)
			{
				switch (misses % 4)
				{
				case 1:
					info.side = -1;
					break;
				case 2:
					info.side = 1;
					break;
				case 3:
					freestand_resolve(player, info);
					break;
				case 0:
					info.side = 0;
					break;
				}

				info.resolved = true;
				if (info.mode != XOR("freestand"))
					info.mode = XOR("brute");
			}
			else
			{
				freestand_resolve(player, info);
			}
		}
	}

	inline void apply_side(c_cs_player* player, anim_record_t* current, int choke)
	{
		auto& info = resolver_info[player->index()];
		if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || !info.resolved || info.side == 1337 || player->is_teammate(false))
			return;

		auto state = player->animstate();
		if (!state)
			return;

		float desync_angle = state->get_max_rotation();
		state->abs_yaw = math::normalize_yaw(player->eye_angles().y + desync_angle * info.side);
	}
}