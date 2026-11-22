// Tests for the headless simulation core (Common/Sim).
//
// This translation unit includes only Sim headers and the standard library.
// The #error at the bottom makes the build fail if a Sim header starts
// pulling in Windows, Winsock or GDI+ headers.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../Common/Sim/SimShot.hpp"
#include "Check.hpp"

using namespace Fortress::Sim;
using SimTests::check;
using SimTests::check_near;

namespace
{
	std::string source_dir()
	{
		std::string file = __FILE__;
		const auto slash = file.find_last_of("\\/");
		return slash == std::string::npos ? std::string(".") : file.substr(0, slash);
	}

	// Flat 1000x100 ground whose top surface is y = 50.
	SimWorld flat_world()
	{
		SimWorld world;
		world.grounds.emplace_back(Vec2{0.0f, 100.0f}, 1000, 100);
		world.update_map_height();
		return world;
	}

	SimCharacter standing(const PlayerID id, const CharacterType type, const float x, const float facing = 1.0f)
	{
		SimCharacter ch;
		ch.id = id;
		ch.type = type;
		ch.position = {x, 25.0f};  // feet on y = 50
		ch.facing = facing;
		return ch;
	}

	int count(const ShotResult& r, const ShotEventType type, const PlayerID player = -2)
	{
		int n = 0;
		for (const auto& e : r.events)
		{
			n += e.type == type && (player == -2 || e.player == player);
		}
		return n;
	}

	float total_damage(const ShotResult& r, const PlayerID victim)
	{
		float sum = 0.0f;
		for (const auto& e : r.events)
		{
			if (e.type == ShotEventType::Damage && e.player == victim)
			{
				sum += e.value;
			}
		}
		return sum;
	}

	uint32_t fnv(uint32_t h, const void* data, const size_t size)
	{
		const auto* p = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			h = (h ^ p[i]) * 16777619u;
		}
		return h;
	}

	uint32_t hash_result(const ShotResult& r, const SimWorld& world)
	{
		uint32_t h = 2166136261u;
		for (const auto& s : r.path)
		{
			h = fnv(h, &s.tick, sizeof(s.tick));
			h = fnv(h, &s.projectile, sizeof(s.projectile));
			h = fnv(h, &s.position, sizeof(s.position));
		}
		for (const auto& e : r.events)
		{
			h = fnv(h, &e.tick, sizeof(e.tick));
			h = fnv(h, &e.type, sizeof(e.type));
			h = fnv(h, &e.projectile, sizeof(e.projectile));
			h = fnv(h, &e.player, sizeof(e.player));
			h = fnv(h, &e.position, sizeof(e.position));
			h = fnv(h, &e.value, sizeof(e.value));
			h = fnv(h, &e.hp, sizeof(e.hp));
		}
		for (const auto& g : world.grounds)
		{
			const uint32_t c = g.checksum();
			h = fnv(h, &c, sizeof(c));
		}
		return h;
	}

	void test_circle_destruction()
	{
		SimGround ground({0.0f, 0.0f}, 200, 200);  // top-left (-100, -100)
		ground.destroy_circle({0.0f, 0.0f}, 10.0f);

		check(ground.destroyed_count() == 316, "r=10 circle destroys exactly the 316 pixels with centre inside");

		int min_x = 1000, max_x = -1, min_y = 1000, max_y = -1;
		bool symmetric = true;
		for (int y = 0; y < 200; ++y)
		{
			for (int x = 0; x < 200; ++x)
			{
				if (!ground.is_destroyed(x, y))
				{
					continue;
				}
				min_x = std::min(min_x, x);
				max_x = std::max(max_x, x);
				min_y = std::min(min_y, y);
				max_y = std::max(max_y, y);
				symmetric = symmetric && ground.is_destroyed(199 - x, y) && ground.is_destroyed(x, 199 - y);
			}
		}

		check(min_x == 90 && max_x == 109 && min_y == 90 && max_y == 109, "circle spans 90..109 on both axes");
		check(symmetric, "circle is mirror-symmetric left/right and top/bottom");

		SimGround big({0.0f, 0.0f}, 200, 200);
		big.destroy_circle({0.0f, 0.0f}, 50.0f);
		check(big.destroyed_count() == 7860, "r=50 circle destroys 7860 pixels");

		SimGround edge({0.0f, 0.0f}, 200, 200);
		edge.destroy_circle({-100.0f, -100.0f}, 10.0f);
		check(edge.destroyed_count() == 316 / 4, "circle at the corner is clipped to one quadrant");
	}

	void test_ground_shapes()
	{
		const SimGround right = SimGround::stairway({0.0f, 0.0f}, 500, 100, true);
		const SimGround left = SimGround::stairway({0.0f, 0.0f}, 500, 100, false);

		// Stairway cuts 125 pixels in row 0, one fewer per row: 125 + ... + 26.
		check(right.destroyed_count() == 7550 && left.destroyed_count() == 7550, "stairway cut size");
		check(right.is_destroyed(0, 0) && right.is_destroyed(124, 0) && !right.is_destroyed(125, 0), "right ramp row 0");
		check(right.is_destroyed(0, 99) && !right.is_destroyed(26, 99), "right ramp last row");
		check(left.is_destroyed(499, 0) && left.is_destroyed(375, 0) && !left.is_destroyed(374, 0), "left ramp row 0");

		SimGround cloud({0.0f, 0.0f}, 1, 1);
		const std::string mask = source_dir() + "/../Client/resources/misc/tiles/cloud-mask.bmp";
		const bool loaded = SimGround::from_mask_bmp(mask, {0.0f, 0.0f}, cloud);

		// Expected counts come from decoding the BMP independently (Python).
		check(loaded, "cloud mask loads: " + mask);
		check(cloud.width() == 1894 && cloud.height() == 342, "cloud mask size 1894x342");
		check(cloud.destroyed_count() == 244730, "cloud mask has 244730 black (hole) pixels");

		int top_row = 0;
		for (int x = 0; x < cloud.width(); ++x)
		{
			top_row += cloud.is_destroyed(x, 0);
		}
		check(top_row == 1882, "cloud mask rows are read top-down (1882 holes in the top row)");
	}

	void test_direct_hit_and_turn_bonus()
	{
		// Precision cannon (radius 15) aimed down at a player right in front:
		// the projectile overlaps the victim on the first tick.
		const auto make = []
		{
			SimWorld w = flat_world();
			w.characters.push_back(standing(1, CharacterType::Cannon, 0.0f));
			w.characters.back().pitch = 0.5f;
			w.characters.push_back(standing(2, CharacterType::Missile, 100.0f));
			return w;
		};

		ShotInput input;
		input.shooter = 1;
		input.kind = ShotKind::Sub;
		input.charge = 100.0f;

		SimWorld world = make();
		const ShotResult first = simulate_shot(world, input);
		const float d1 = total_damage(first, 2);

		check(!first.timed_out, "direct hit finishes");
		check(count(first, ShotEventType::Damage, 2) == 1 && d1 > 0.0f, "victim takes one damage event");
		check(count(first, ShotEventType::Damage, 1) == 0, "shooter outside the 15px blast is unhurt");
		check_near(world.find(2)->hp, character_full_hp - d1, 1e-4f, "victim hp reduced by the damage");
		check(world.find(2)->hits_this_turn == 1, "victim hit counter incremented");

		// Same shot again in the same turn: consecutive hit bonus +25%.
		const ShotResult second = simulate_shot(world, input);
		check_near(total_damage(second, 2), d1 * 1.25f, 1e-3f, "second hit in a turn gets the 25% bonus");

		// Double damage on a fresh turn doubles the first hit.
		SimWorld doubled = make();
		input.double_damage = true;
		check_near(total_damage(simulate_shot(doubled, input), 2), d1 * 2.0f, 1e-3f, "double damage doubles");
	}

	void test_multi_shot_and_double_damage_scope()
	{
		const auto make = []
		{
			SimWorld w = flat_world();
			w.characters.push_back(standing(1, CharacterType::Secwind, 0.0f));
			w.characters.back().pitch = 0.5f;
			w.characters.push_back(standing(2, CharacterType::Cannon, 100.0f));
			return w;
		};

		ShotInput input;
		input.shooter = 1;
		input.kind = ShotKind::Sub;  // 3 projectiles
		input.charge = 100.0f;

		SimWorld plain_world = make();
		const ShotResult plain = simulate_shot(plain_world, input);

		std::vector<int> spawn_ticks;
		for (const auto& e : plain.events)
		{
			if (e.type == ShotEventType::Spawn)
			{
				spawn_ticks.push_back(e.tick);
			}
		}

		check(spawn_ticks == std::vector<int>{0, 6, 12}, "multi-shot spawns 3 projectiles 0.1 s (6 ticks) apart");
		check(count(plain, ShotEventType::Damage, 2) == 3, "all 3 projectiles hit");

		SimWorld doubled_world = make();
		input.double_damage = true;
		const ShotResult doubled = simulate_shot(doubled_world, input);
		check_near(total_damage(doubled, 2), total_damage(plain, 2) * 2.0f, 1e-3f,
			"double damage applies to every projectile of the turn");

		ShotInput twice = input;
		twice.double_damage = false;
		twice.volleys = 2;
		SimWorld double_shot_world = make();
		const ShotResult double_shot = simulate_shot(double_shot_world, twice);
		check(count(double_shot, ShotEventType::Spawn) == 6, "DoubleShot fires the volley twice");
	}

	void test_out_of_bounds()
	{
		SimWorld world = flat_world();
		world.characters.push_back(standing(1, CharacterType::Cannon, 450.0f));

		ShotInput input;
		input.shooter = 1;
		input.charge = 250.0f;

		const ShotResult r = simulate_shot(world, input);

		check(!r.timed_out, "off-map shot ends before the time limit");
		check(count(r, ShotEventType::Explosion) == 0, "off-map shot does not explode");
		check(!r.events.empty() && r.events.back().type == ShotEventType::End && r.events.back().value == 0.0f,
			"off-map shot ends as not landed");
		check(r.path.back().position.y > world.map_height + out_of_bounds_margin, "ends below map height + 500");
	}

	void test_guided_missile()
	{
		const auto make = []
		{
			SimWorld w = flat_world();
			w.characters.push_back(standing(1, CharacterType::Missile, 0.0f));
			w.characters.push_back(standing(2, CharacterType::Cannon, 200.0f));
			return w;
		};

		ShotInput input;
		input.shooter = 1;
		input.charge = 100.0f;

		// The main missile has the same speed but no homing: it flies over.
		input.kind = ShotKind::Main;
		SimWorld unguided_world = make();
		check(count(simulate_shot(unguided_world, input), ShotEventType::Damage, 2) == 0,
			"unguided missile passes over the target");

		input.kind = ShotKind::Sub;
		SimWorld guided_world = make();
		check(count(simulate_shot(guided_world, input), ShotEventType::Damage, 2) == 1,
			"guided missile homes in on the target");
	}

	void test_teleport_and_self_kill()
	{
		SimWorld world = flat_world();
		world.characters.push_back(standing(1, CharacterType::Cannon, 0.0f));
		world.characters.back().pitch = 0.3f;

		ShotInput input;
		input.shooter = 1;
		input.kind = ShotKind::Nutshell;
		input.charge = 50.0f;
		input.teleport = true;

		const ShotResult r = simulate_shot(world, input);
		const ShotEvent* teleport = nullptr;
		for (const auto& e : r.events)
		{
			if (e.type == ShotEventType::Teleport)
			{
				teleport = &e;
			}
		}

		check(teleport != nullptr, "nutshell landing teleports the shooter");
		check(teleport && world.find(1)->position == teleport->position, "shooter moved to the landing point");
		check(count(r, ShotEventType::Damage) == 0, "nutshell does no damage");

		SimWorld suicide = flat_world();
		suicide.characters.push_back(standing(1, CharacterType::Cannon, 0.0f));
		suicide.characters.back().hp = 1.0f;
		suicide.characters.back().pitch = 1.2f;  // straight into the ground next to itself

		ShotInput down;
		down.shooter = 1;
		down.charge = 50.0f;
		down.volleys = 2;
		const ShotResult s = simulate_shot(suicide, down);

		check(count(s, ShotEventType::Death, 1) == 1, "shooter killed by its own blast");
		check(!s.timed_out && count(s, ShotEventType::Spawn) == 1, "dead shooter fires no second volley");
	}

	void test_desert_golden()
	{
		const auto run = []
		{
			SimWorld w;
			build_desert_map(w);
			w.characters.push_back(standing(1, CharacterType::Cannon, -450.0f));
			w.characters.back().position.y = -75.0f;  // on the left stairway
			w.characters.push_back(standing(2, CharacterType::Secwind, 450.0f, -1.0f));
			w.characters.back().position.y = -75.0f;

			ShotInput input;
			input.shooter = 1;
			input.charge = 120.0f;
			input.wind = 20;
			const ShotResult r = simulate_shot(w, input);
			return std::make_pair(hash_result(r, w), r);
		};

		const auto [h1, r1] = run();

		// Another room's shot in between must not affect the next run.
		SimWorld other = flat_world();
		other.characters.push_back(standing(7, CharacterType::Secwind, 0.0f));
		ShotInput noise;
		noise.shooter = 7;
		noise.kind = ShotKind::Sub;
		noise.charge = 200.0f;
		simulate_shot(other, noise);

		const auto [h2, r2] = run();

		check(!r1.timed_out && count(r1, ShotEventType::Explosion) >= 1, "desert shot lands");
		check(h1 == h2, "same world and input give the same result");

		// Golden value for this scenario. If the simulation is changed on
		// purpose, update it and say why in the commit message.
		constexpr uint32_t golden = 0xf352e771u;
		if (!check(h1 == golden, "desert shot matches the golden hash"))
		{
			std::cout << "  actual hash 0x" << std::hex << h1 << std::dec << ", end tick " << r1.end_tick << std::endl;
		}
	}
}

void run_sim_core_tests()
{
	test_circle_destruction();
	test_ground_shapes();
	test_direct_hit_and_turn_bonus();
	test_multi_shot_and_double_damage_scope();
	test_out_of_bounds();
	test_guided_missile();
	test_teleport_and_self_kill();
	test_desert_golden();
}

#if defined(_WINDOWS_) || defined(_WINSOCK2API_) || defined(_WINSOCKAPI_) || defined(_GDIPLUS_H)
#error "Common/Sim headers must not include Windows, Winsock or GDI+ headers"
#endif
