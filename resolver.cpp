#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"

// TODO: improve jitter fix && add freestand resolve

namespace resolver
{
	inline void prepare_jitter(c_cs_player* player, resolver_info_t& resolver_info, anim_record_t* current)
	{
		auto& jitter = resolver_info.jitter;

		auto diff{ math::angle_diff(player->eye_angles().y, current->eye_angles.y) };

		if (diff == 0) {
			if (jitter.static_ticks < 3)
				jitter.static_ticks++;
			else
				jitter.jitter_ticks = 0;
		}
		else {
			if (jitter.jitter_ticks < 3)
				jitter.jitter_ticks++;
			else
				jitter.static_ticks = 0;
		}

		jitter.is_jitter = jitter.jitter_ticks > jitter.static_ticks;

		if (jitter.is_jitter) {
			jitter.jitter_side = diff > 0 ? 1 : -1;
		}
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
				switch (misses % 3)
				{
				case 1:
					info.side = -1;
					break;
				case 2:
					info.side = 1;
					break;
				case 0:
					info.side = 0;
					break;
				}

				info.resolved = true;
				info.mode = XOR("brute");
			}
			else
			{
				info.side = 0;
				info.mode = XOR("static");

				info.resolved = true;
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

		float desync_angle = choke < 2 ? state->get_max_rotation() : 120.f;
		state->abs_yaw = math::normalize_yaw(player->eye_angles().y + desync_angle * info.side);
	}
}