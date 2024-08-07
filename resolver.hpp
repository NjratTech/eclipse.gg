#pragma once
#include "animations.hpp"

constexpr int MAX_TICKS = 3;
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

	// Reset resolver data
	void reset()
	{
		resolved = false;
		side = 0;
		legit_ticks = 0;
		fake_ticks = 0;
		mode = "";
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
}