// Tests for character movement (Common/Sim/SimMove.hpp), the port of the
// client's walking/falling code. Expected values are worked out by hand
// from the client code where that is practical (comments show how);
// trajectories over ramps are checked against the ramp geometry and a
// golden hash taken from this port.
//
// Like SimCoreTests.cpp this translation unit includes only Sim headers and
// the standard library (see the #error at the bottom).

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../Common/Sim/SimMove.hpp"
#include "Check.hpp"

using namespace Fortress::Sim;
using SimTests::check;
using SimTests::check_near;

namespace
{
	constexpr float one_tick_walk = 20.0f / 60.0f;  // speed.x * dt for cannon/missile
	constexpr float one_tick_mp = 20.0f / 60.0f;    // CharacterController::move

	SimCharacter make(const PlayerID id, const CharacterType type = CharacterType::Cannon)
	{
		SimCharacter ch;
		ch.id = id;
		ch.type = type;
		return ch;
	}

	// Desert map with players 1 (cannon) and 2 (secwind) at their spawn.
	SimWorld desert_world()
	{
		SimWorld w;
		build_desert_map(w);
		w.characters = {make(1), make(2, CharacterType::Secwind)};
		spawn_characters(w);
		return w;
	}

	// Flat 1000x100 ground, top surface y = 50, map height 100.
	SimWorld flat_world(const float x)
	{
		SimWorld w;
		w.grounds.emplace_back(Vec2{0.0f, 100.0f}, 1000, 100);
		w.update_map_height();
		w.characters = {make(1)};
		w.characters[0].position = {x, 25.0f};
		return w;
	}

	void run(SimWorld& w, const PlayerID current, const MoveInput input, const int ticks)
	{
		for (int i = 0; i < ticks; ++i)
		{
			step_world(w, current, input, true);
		}
	}

	float bottom_y(const SimCharacter& ch)
	{
		return ch.position.y + 25.0f;
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

	uint32_t hash_characters(const SimWorld& w)
	{
		uint32_t h = 2166136261u;
		for (const auto& c : w.characters)
		{
			const uint8_t flags[5] = {
				static_cast<uint8_t>(c.state), c.gravity, c.grounded, c.alive,
				static_cast<uint8_t>((c.left_held ? 1 : 0) | (c.right_held ? 2 : 0))};
			h = fnv(h, &c.position, sizeof(c.position));
			h = fnv(h, &c.velocity, sizeof(c.velocity));
			h = fnv(h, &c.gravity_speed, sizeof(c.gravity_speed));
			h = fnv(h, &c.mp, sizeof(c.mp));
			h = fnv(h, &c.pitch, sizeof(c.pitch));
			h = fnv(h, &c.facing, sizeof(c.facing));
			h = fnv(h, flags, sizeof(flags));
		}
		return h;
	}

	void test_spawn()
	{
		// BattleScene::spawnpoints on the desert: grounds span x -550..550.
		// Player 1: x = -550 + 150 = -400, dropped onto left_upper's top
		// (y = -50) -> centre y = -75. Player 2: x = 550 - 550 / 2 = 275 on
		// right_upper -> (275, -75).
		SimWorld w = desert_world();
		check_near(w.characters[0].position.x, -400.0f, 0.0f, "spawn p1 x");
		check_near(w.characters[0].position.y, -75.0f, 0.0f, "spawn p1 y");
		check_near(w.characters[1].position.x, 275.0f, 0.0f, "spawn p2 x");
		check_near(w.characters[1].position.y, -75.0f, 0.0f, "spawn p2 y");

		// Three players (ported as-is): interval 550 / 3 = 183, p2 at
		// 550 - 183 = 367, p3 at -550 - 183 = -733 where no ground is, so
		// it stays at y = -1000.
		SimWorld w3;
		build_desert_map(w3);
		w3.characters = {make(1), make(2), make(3)};
		spawn_characters(w3);
		check_near(w3.characters[1].position.x, 367.0f, 0.0f, "spawn 3 players: p2 x");
		check_near(w3.characters[1].position.y, -75.0f, 0.0f, "spawn 3 players: p2 y");
		check(w3.characters[2].position.x == -733.0f && w3.characters[2].position.y == -1000.0f,
			"spawn 3 players: p3 has no ground below (client quirk)");

		// Feet exactly on the surface do not collide (bottom - top > epsilon
		// fails), so the first tick falls one gravity step:
		// gs = 14.7 / 60 / 2, y += gs / 60, gs doubles. The second tick
		// collides (Boundary), ground_gravity stops gravity and grounds it.
		const float g_half = 14.7f / 60.0f * 0.5f;
		step_world(w, 1, MoveInput::None, true);
		check_near(w.characters[0].position.y, -75.0f + g_half / 60.0f, 1e-5f, "settle: first tick falls one step");
		check_near(w.characters[0].gravity_speed, 2.0f * g_half, 1e-5f, "settle: gravity speed after one step");
		check(!w.characters[0].grounded, "settle: not grounded after the first tick");

		step_world(w, 1, MoveInput::None, true);
		const SimCharacter& c = w.characters[0];
		check(c.grounded && !c.gravity && c.gravity_speed == 0.0f, "settle: grounded on the second tick");
		check_near(c.position.y, -75.0f + g_half / 60.0f, 1e-5f, "settle: no snap to the surface");
		check(c.state == CharacterState::Idle && c.alive, "settle: idle and alive");
	}

	void test_walk_flat()
	{
		SimWorld w = desert_world();
		run(w, 1, MoveInput::None, 2);
		const float y0 = w.characters[0].position.y;

		// Holding D for 62 ticks: tick 0 is the key-down edge (getKey is
		// false), tick 1 enters Move and sets the velocity after moving,
		// ticks 2..61 move. Penetration is below 1 px (Boundary, not Inside)
		// so ground_walk leaves the velocity at (1, 0): 60 x 20/60 px.
		run(w, 1, MoveInput::Right, 62);
		const SimCharacter& c = w.characters[0];
		check_near(c.position.x, -400.0f + 60.0f * one_tick_walk, 1e-3f, "walk flat: x after 62 ticks");
		check_near(c.position.y, y0, 1e-5f, "walk flat: y unchanged");
		check_near(c.mp, character_full_mp - 60.0f * one_tick_mp, 2e-3f, "walk flat: mp 20 per second");
		check(c.grounded && c.state == CharacterState::Move && c.facing == 1.0f, "walk flat: grounded, moving right");

		// Releasing: the key-up tick still moves (move() runs before the
		// controller), then Idle + stop.
		run(w, 1, MoveInput::None, 1);
		check_near(w.characters[0].position.x, -400.0f + 61.0f * one_tick_walk, 1e-3f, "walk flat: key-up tick moves");
		check(w.characters[0].state == CharacterState::Idle && w.characters[0].velocity == Vec2{},
			"walk flat: stopped after key up");
		run(w, 1, MoveInput::None, 10);
		check_near(w.characters[0].position.x, -400.0f + 61.0f * one_tick_walk, 1e-3f, "walk flat: stays after stop");

		// Secwind walks left at 25 px/s.
		SimWorld w2 = desert_world();
		run(w2, 2, MoveInput::None, 2);
		run(w2, 2, MoveInput::Left, 32);
		check_near(w2.characters[1].position.x, 275.0f - 30.0f * 25.0f / 60.0f, 1e-3f, "walk flat: secwind left 25 px/s");
		check(w2.characters[1].facing == -1.0f, "walk flat: facing left");

		// Only the current, movable player reacts to input.
		SimWorld w3 = desert_world();
		run(w3, 1, MoveInput::Right, 30);
		check(w3.characters[1].position.x == 275.0f && w3.characters[1].state == CharacterState::Idle,
			"walk flat: other player ignores the input");
		SimWorld w4 = desert_world();
		for (int i = 0; i < 30; ++i)
		{
			step_world(w4, 1, MoveInput::Right, false);
		}
		check(w4.characters[0].position.x == -400.0f && w4.characters[0].mp == character_full_mp,
			"walk flat: unmovable player ignores the input");
	}

	void test_walk_inside_ground()
	{
		// Feet 1.5 px deep (Inside): ground_walk asks get_next_velocity,
		// which finds the highest intact pixel in the next column whose
		// truncated row is >= 0: local (501, 1.5 - 2 = -0.5) -> row 0, so
		// the velocity becomes normalize(1, -2). The bottom is stuck, and
		// safe_nearest_surface finds no destroyed pixel above, so it only
		// lifts by the fractional part (0.5 px). Then
		// move: x += 20 / sqrt(5) / 60, y -= 1 * 2 / sqrt(5) / 60.
		SimWorld w = flat_world(0.0f);
		SimCharacter& c = w.characters[0];
		c.position.y = 26.5f;
		c.state = CharacterState::Move;
		c.velocity = {1.0f, 0.0f};
		c.right_held = true;

		step_world(w, 1, MoveInput::Right, true);
		check_near(c.position.x, 20.0f / std::sqrt(5.0f) / 60.0f, 1e-5f, "inside: x uses the 1:-2 slope");
		check_near(c.position.y, 26.0f - 2.0f / std::sqrt(5.0f) / 60.0f, 1e-5f, "inside: lifted by 0.5 px only");
		check(c.grounded && c.velocity == Vec2{1.0f, 0.0f}, "inside: grounded, key resets velocity");
	}

	void test_ramps()
	{
		// Walk player 1 right from its spawn over the whole desert:
		// left_upper (top -50), down its ramp, mid_lower (top 50), up the
		// right_upper ramp, onto right_upper (top -50).
		// Ramp surfaces from Stairway::initialize: left ramp y = x + 126 for
		// x in [-175, -76]; right ramp y = 125 - x for x in [76, 175].
		SimWorld w = desert_world();
		run(w, 1, MoveInput::None, 2);

		float max_down_pitch = 0.0f;
		float min_up_pitch = 0.0f;
		float worst_left = 0.0f;
		float worst_right = 0.0f;
		bool monotonic_x = true;
		float last_x = w.characters[0].position.x;

		for (int t = 0; t < 2600; ++t)
		{
			step_world(w, 1, MoveInput::Right, true);
			const SimCharacter& c = w.characters[0];
			const float x = c.position.x;

			monotonic_x = monotonic_x && x >= last_x;
			last_x = x;

			if (x > -165.0f && x < -85.0f)
			{
				max_down_pitch = std::max(max_down_pitch, c.pitch);
				worst_left = std::max(worst_left, std::fabs(bottom_y(c) - (x + 126.0f)));
			}
			if (x > 85.0f && x < 165.0f)
			{
				min_up_pitch = std::min(min_up_pitch, c.pitch);
				worst_right = std::max(worst_right, std::fabs(bottom_y(c) - (125.0f - x)));
			}
			if (t == 1700)
			{
				check_near(bottom_y(c), 50.0f, 0.5f, "ramps: on mid_lower between the ramps");
			}
		}

		const SimCharacter& c = w.characters[0];
		check(monotonic_x, "ramps: x never goes backwards");
		check(max_down_pitch > 0.6f, "ramps: positive pitch walking down the left ramp");
		check(min_up_pitch < -0.6f, "ramps: negative pitch walking up the right ramp");
		check(worst_left < 4.0f, "ramps: feet follow the left ramp (" + std::to_string(worst_left) + " px)");
		check(worst_right < 4.0f, "ramps: feet follow the right ramp (" + std::to_string(worst_right) + " px)");
		check(c.position.x > 200.0f && std::fabs(bottom_y(c) - -50.0f) < 1.5f, "ramps: reached right_upper");
		check(c.alive && c.state == CharacterState::Move, "ramps: still walking");

		// And back: player 2 walks left down the right ramp to mid_lower.
		SimWorld w2 = desert_world();
		run(w2, 2, MoveInput::None, 2);
		float min_pitch = 0.0f;
		for (int t = 0; t < 1000; ++t)
		{
			step_world(w2, 2, MoveInput::Left, true);
			min_pitch = std::min(min_pitch, w2.characters[1].pitch);
		}
		check(min_pitch < -0.6f, "ramps: walking left down the right ramp has negative pitch (dy/dx)");
		check_near(bottom_y(w2.characters[1]), 50.0f, 0.5f, "ramps: p2 reached mid_lower");
	}

	void test_mp()
	{
		// CharacterController::move only stops *consuming* mp below
		// epsilon; nothing stops the walk (ported as-is). Start at 0.5:
		// 0.5 -> 0.1667 -> -0.1667, then no more consumption.
		SimWorld w = desert_world();
		run(w, 1, MoveInput::None, 2);
		w.characters[0].mp = 0.5f;
		run(w, 1, MoveInput::Right, 2);  // key down + enter Move
		run(w, 1, MoveInput::Right, 1);
		check_near(w.characters[0].mp, 0.5f - one_tick_mp, 1e-5f, "mp: first moving tick");
		run(w, 1, MoveInput::Right, 1);
		check_near(w.characters[0].mp, 0.5f - 2.0f * one_tick_mp, 1e-5f, "mp: goes negative once");
		const float x = w.characters[0].position.x;
		run(w, 1, MoveInput::Right, 30);
		check_near(w.characters[0].mp, 0.5f - 2.0f * one_tick_mp, 1e-5f, "mp: no consumption below epsilon");
		check_near(w.characters[0].position.x, x + 30.0f * one_tick_walk, 1e-3f, "mp: walking continues without mp");

		// finish_turn refills mp and stops.
		finish_turn(w.characters[0]);
		check(w.characters[0].mp == character_full_mp && w.characters[0].state == CharacterState::Idle &&
			w.characters[0].velocity == Vec2{}, "mp: finish_turn refills, stops and idles");
	}

	void test_crater_fall()
	{
		// Crater of radius 40 under the feet: column x = 0 is destroyed for
		// rows 0..39 (pixel centres within the circle), row 40 (y = 90) is
		// intact. ground_gravity sees the destroyed pixel and turns gravity
		// on; velocity-Verlet with constant g gives exactly
		// drop = g t^2 / 2 -> 7.35 px after 60 ticks.
		SimWorld w = flat_world(0.0f);
		w.characters.push_back(make(2));
		w.characters[1].position = {300.0f, 25.0f};
		run(w, 2, MoveInput::None, 2);  // settle; player 2 is current, 1 only falls
		const float y0 = w.characters[0].position.y;

		w.grounds[0].destroy_circle({0.0f, 50.0f}, 40.0f);
		run(w, 2, MoveInput::None, 60);
		const SimCharacter& c = w.characters[0];
		check_near(c.position.y - y0, 0.5f * gravity * 1.0f * 1.0f, 1e-3f, "crater: free fall g t^2 / 2");
		check_near(c.gravity_speed, gravity * 1.0f, 1e-3f, "crater: gravity speed g t");
		check(c.gravity && !c.grounded, "crater: falling");

		run(w, 2, MoveInput::None, 200);
		check(c.grounded && !c.gravity, "crater: landed");
		check(bottom_y(c) >= 90.0f && bottom_y(c) < 91.0f, "crater: feet in row 40 (" + std::to_string(bottom_y(c)) + ")");
		check(c.alive && c.state == CharacterState::Idle, "crater: alive");
		check_near(w.characters[1].position.y, 25.0f + 14.7f / 120.0f / 60.0f, 1e-5f, "crater: other player unaffected");

		// A hole through the whole ground: the column below the feet has no
		// intact pixel, so on_collision does not treat the ground as the
		// "main" ground (no ground_gravity), but the boxes still overlap so
		// on_nocollison does not run either: it hovers (client quirk).
		SimWorld h = flat_world(0.0f);
		run(h, 1, MoveInput::None, 2);
		const float hover_y = h.characters[0].position.y;
		h.grounds[0].destroy_circle({0.0f, 100.0f}, 120.0f);
		run(h, 1, MoveInput::None, 300);
		check(h.characters[0].position.y == hover_y && !h.characters[0].gravity && h.characters[0].alive,
			"crater: hovers over a hole through the whole ground (client quirk)");

		// Off the ground box nothing collides: it falls and dies once the
		// bottom forward corner is below map height (100) + 500.
		SimWorld d = flat_world(600.0f);
		int death_tick = -1;
		float before = 0.0f;
		float at = 0.0f;
		for (int t = 0; t < 2000 && death_tick < 0; ++t)
		{
			before = bottom_y(d.characters[0]);
			if (step_character(d, d.characters[0], MoveInput::None, false))
			{
				death_tick = t;
				at = bottom_y(d.characters[0]);
			}
		}
		// Falling 550 px from rest at 14.7 px/s^2 takes sqrt(2 * 550 / 14.7)
		// = 8.65 s, so the death tick is 519 (the check runs after the move).
		check(death_tick == 519, "crater: falls out and dies at tick 519 (" + std::to_string(death_tick) + ")");
		check(before <= 600.0f && at > 600.0f, "crater: dies at map height + 500");
		check(!d.characters[0].alive && d.characters[0].state == CharacterState::Death, "crater: Death state");
		step_character(d, d.characters[0], MoveInput::None, false);
		check(d.characters[0].state == CharacterState::Dead, "crater: Dead the tick after");
	}

	void test_walk_off_edge()
	{
		// Player 1 walks left off the desert (left edge x = -550). Map
		// height is 200, so the death line is y = 700 for the bottom-left
		// corner.
		SimWorld w = desert_world();
		run(w, 1, MoveInput::None, 2);
		SimCharacter& c = w.characters[0];

		int ticks = 0;
		while (!is_out_of_bounds(w, c) && ticks < 5000)
		{
			step_world(w, 1, MoveInput::Left, true);
			++ticks;
		}
		check(ticks < 5000 && c.position.x < -575.0f, "edge: walked off and fell below the death line");

		// Quirk: while the key is held move_state overrides Death with
		// Idle (map->movable fails) and then Move again, so it never dies.
		run(w, 1, MoveInput::Left, 10);
		check(c.alive && c.state == CharacterState::Move && is_out_of_bounds(w, c),
			"edge: holding the key keeps it alive below the line (client quirk)");

		// Key up: move_state sets Death, then Idle, then Idle + stop.
		check(!step_character(w, c, MoveInput::None, true) && c.state == CharacterState::Idle,
			"edge: key-up tick does not die yet");
		check(c.velocity == Vec2{}, "edge: key up stops");
		check(step_character(w, c, MoveInput::None, true) && c.state == CharacterState::Death && !c.alive,
			"edge: dies on the next idle tick");
		step_character(w, c, MoveInput::None, true);
		check(c.state == CharacterState::Dead, "edge: then Dead");
	}

	void test_quirks()
	{
		using namespace MoveDetail;

		// check_angle limits the slope only for a right velocity.
		SimCharacter c = make(1);
		c.position = {0.0f, 0.0f};
		c.velocity = {1.0f, 0.0f};
		const Vec2 straight_up = {0.0f, -10.0f};    // bottom is (0, 25)
		const Vec2 shallow = {10.0f, 25.0f - 1.0f};
		check(!check_angle(c, straight_up) && check_angle(c, shallow), "check_angle: 80 degree limit moving right");
		c.velocity = {-1.0f, 0.0f};
		check(check_angle(c, straight_up), "check_angle: no limit moving left (client quirk)");
		check(!check_angle(c, vector_inf), "check_angle: nothing found fails");

		// Touching another character counts as a collision, so
		// on_nocollison does not re-enable gravity: it hangs in the air.
		SimWorld w;
		w.characters = {make(1), make(2)};
		w.characters[0].position = {0.0f, -500.0f};
		w.characters[1].position = {40.0f, -500.0f};
		w.characters[0].gravity = false;
		step_character(w, w.characters[0], MoveInput::None, false);
		check(w.characters[0].position.y == -500.0f && !w.characters[0].gravity,
			"overlapping characters do not start falling (client quirk)");
	}

	void test_determinism()
	{
		const auto play = []
		{
			SimWorld w = desert_world();
			w.grounds[1].destroy_circle({-20.0f, 60.0f}, 35.0f);

			struct Segment { PlayerID who; MoveInput input; int ticks; };
			const Segment script[] = {
				{1, MoveInput::None, 5}, {1, MoveInput::Right, 900}, {1, MoveInput::None, 120},
				{1, MoveInput::Left, 200}, {1, MoveInput::Right, 3}, {1, MoveInput::None, 1},
				{2, MoveInput::Left, 700}, {2, MoveInput::None, 60}, {2, MoveInput::Right, 50},
			};

			for (const auto& s : script)
			{
				run(w, s.who, s.input, s.ticks);
			}
			return hash_characters(w);
		};

		const uint32_t h1 = play();
		const uint32_t h2 = play();
		check(h1 == h2, "move: same inputs give the same result");

		// Golden value taken from this port (no client run). Update it only
		// together with an intended change of the movement rules.
		constexpr uint32_t golden = 0x5991f228u;
		if (!check(h1 == golden, "move: desert script matches the golden hash"))
		{
			std::cout << "  actual hash 0x" << std::hex << h1 << std::dec << std::endl;
		}
	}
}

void run_sim_move_tests()
{
	test_spawn();
	test_walk_flat();
	test_walk_inside_ground();
	test_ramps();
	test_mp();
	test_crater_fall();
	test_walk_off_edge();
	test_quirks();
	test_determinism();
}

#if defined(_WINDOWS_) || defined(_WINSOCK2API_) || defined(_WINSOCKAPI_) || defined(_GDIPLUS_H)
#error "Common/Sim headers must not include Windows, Winsock or GDI+ headers"
#endif
