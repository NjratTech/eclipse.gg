#pragma once
#include "animations.hpp"

constexpr int CACHE_SIZE = 2;
constexpr int YAW_CACHE_SIZE = 8;
constexpr auto MAX_TICKS = 3;

// Constants for jitter detection
constexpr float JITTER_DIFF_THRESHOLD = 10.0f;
constexpr int JITTER_MAX_TICKS = 5;

// Constants for bruteforce resolver
constexpr int BRUTEFORCE_CYCLE_LENGTH = 3;

// Structure to store resolver information for each player
struct resolver_info_t
{
	// Resolver state
	bool resolved = false;
	int side = 0;

	// Tick counters for legit and fake lag
	int legit_ticks = 0;
	int fake_ticks = 0;

	// Resolver mode
	std::string mode = "";

	// Initial layers
	c_animation_layers initial_layers[13]{};

	INLINE void add_legit_ticks()
	{
		if (legit_ticks < MAX_TICKS)
			++legit_ticks;
		else
			fake_ticks = 0;
	}

	INLINE void add_fake_ticks()
	{
		if (fake_ticks < MAX_TICKS)
			++fake_ticks;
		else
			legit_ticks = 0;
	}

	INLINE bool is_legit()
	{
		return legit_ticks > fake_ticks;
	}

	// Jitter information
	struct jitter_info_t
	{
		bool is_jitter = false;

		// Yaw cache for jitter detection
		float yaw_cache[YAW_CACHE_SIZE] = {};
		int yaw_cache_offset = 0;

		// Tick counters for static and jitter states
		int static_ticks = 0;
		int jitter_ticks = 0;

		// Reset jitter information
		void reset()
		{
			is_jitter = false;
			yaw_cache_offset = 0;
			static_ticks = 0;
			jitter_ticks = 0;
			std::memset(yaw_cache, 0, sizeof(yaw_cache));
		}
	} jitter;

	// Reset resolver data
	void reset()
	{
		resolved = false;
		side = 0;
		legit_ticks = 0;
		fake_ticks = 0;
		mode = "";
		jitter.reset();
	}
};

inline resolver_info_t resolver_info[65]{};

namespace resolver
{
	INLINE void reset()
	{
		for (auto& i : resolver_info)
			i.reset();
	}

	extern void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* last);
	extern void apply_side(c_cs_player* player, anim_record_t* current, int choke);
}