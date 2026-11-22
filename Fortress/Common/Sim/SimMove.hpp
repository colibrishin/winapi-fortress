#pragma once
#ifndef SIM_MOVE_HPP
#define SIM_MOVE_HPP

// Character walking and falling, ported as-is from the client:
//   Common/characterCollision.cpp  (character::on_collision and helpers)
//   Common/rigidBody.cpp           (update, move, apply_gravity, is_collision)
//   Common/character.cpp           (update / move order)
//   Common/CharacterController.cpp (idle_state, move_state, default_state, move)
//   Common/BattleScene.cpp         (spawnpoints, movable, predicate_OOB)
// The client keeps predicting with that code and is corrected by the
// server, so this must behave the same, quirks included. Deviations and
// suspicious quirks are listed in SimMove_PORTING.md. Do not "fix" things
// here without changing the client too.

#include <algorithm>
#include <climits>
#include <cmath>

#include "SimData.hpp"
#include "SimGround.hpp"
#include "SimMath.hpp"
#include "SimShot.hpp"

namespace Fortress::Sim
{
	// Held movement key of a player for one tick (A = Left, D = Right).
	enum class MoveInput : uint8_t
	{
		None,
		Left,
		Right,
	};

	// Math::epsilon
	constexpr float move_epsilon = 0.001f;
	// CharacterController::move: mp used per second while moving.
	constexpr float mp_per_second = 20.0f;
	// character::check_angle: steepest walkable slope.
	constexpr float max_walk_degree = 80.0f;
	// rigidBody::apply_gravity stops falling when the feet are exactly at
	// EngineHandle::get_actual_max_y(), the client area height of the
	// window (EngineHandle::m_window_size is 800x600).
	constexpr float client_window_max_y = 600.0f;
	// rigidBody.hpp max_gravity_speed
	constexpr float max_gravity_speed = 1000000000.0f;

	// rigidBody::m_speed of each character (Client/CannonCharacter.hpp,
	// MissileCharacter.hpp, SecwindCharacter.hpp). Acceleration is {} for
	// all of them, so rigidBody::m_curr_speed always equals this while
	// moving and is not stored.
	inline Vec2 character_move_speed(const CharacterType type)
	{
		return type == CharacterType::Secwind ? Vec2{25.0f, 1.0f} : Vec2{20.0f, 1.0f};
	}

	namespace MoveDetail
	{
		// ---- Math::Vector2 behaviour (Common/vector2.hpp, Common/math.h)

		constexpr float pi = 3.14159265358979323846f;  // Math::PI (float)

		// Vector2::operator==: per component within epsilon; inf == inf.
		inline bool same(const Vec2& a, const Vec2& b)
		{
			constexpr float inf = std::numeric_limits<float>::infinity();
			if (a.x == inf && a.y == inf && b.x == inf && b.y == inf)
			{
				return true;
			}
			return std::fabs(a.x - b.x) <= move_epsilon && std::fabs(a.y - b.y) <= move_epsilon;
		}

		// Vector2::magnitude / normalized. Unlike Vec2::normalized a zero or
		// infinite vector gives NaN, which the client code relies on.
		inline float magnitude(const Vec2& v)
		{
			return std::sqrt(std::pow(v.x, 2.0f) + std::pow(v.y, 2.0f));
		}

		inline Vec2 normalized(const Vec2& v)
		{
			return {v.x / magnitude(v), v.y / magnitude(v)};
		}

		inline float unit_angle(const Vec2& v)
		{
			return std::atan2(v.y, v.x);
		}

		inline float local_inner_angle(const Vec2& v, const Vec2& other)
		{
			return std::atan((v.y - other.y) / (v.x - other.x));
		}

		inline float flip_radian_polarity(const float radian)
		{
			return radian < 0.0f ? pi + radian : pi - radian;
		}

		inline float to_degree(const float radian)
		{
			return radian * (180 / pi);
		}

		// Vector2::x_dir == Math::left
		inline bool x_dir_left(const Vec2& v)
		{
			return v.x < 0;
		}

		// ---- object / rigidBody accessors (Common/object.hpp, rigidBody.cpp)

		inline Vec2 bottom(const SimCharacter& ch) { return ch.position + Vec2{0.0f, character_hitbox.y / 2}; }
		inline Vec2 left(const SimCharacter& ch) { return ch.position - Vec2{character_hitbox.x / 2, 0.0f}; }
		inline Vec2 right(const SimCharacter& ch) { return ch.position + Vec2{character_hitbox.x / 2, 0.0f}; }
		inline Vec2 bottom_left(const SimCharacter& ch) { return ch.position + Vec2{-character_hitbox.x / 2, character_hitbox.y / 2}; }
		inline Vec2 bottom_right(const SimCharacter& ch) { return ch.position + Vec2{character_hitbox.x / 2, character_hitbox.y / 2}; }

		// get_offset() == Math::left (m_offset is the facing)
		inline bool facing_left(const SimCharacter& ch) { return same({ch.facing, 0.0f}, {-1.0f, 0.0f}); }
		// get_velocity_offset() == Math::left
		inline bool velocity_left(const SimCharacter& ch) { return ch.velocity.x < 0; }

		inline Vec2 offset_forward_position(const SimCharacter& ch) { return facing_left(ch) ? left(ch) : right(ch); }
		inline Vec2 offset_backward_position(const SimCharacter& ch) { return facing_left(ch) ? right(ch) : left(ch); }
		inline Vec2 offset_bottom_forward_position(const SimCharacter& ch)
		{
			return facing_left(ch) ? bottom_left(ch) : bottom_right(ch);
		}

		// rigidBody::is_moving_toward
		inline bool is_moving_toward(const SimCharacter& ch, const SimGround& ground)
		{
			const Vec2 dir = normalized(ch.position - ground.center());
			return velocity_left(ch) != x_dir_left(dir);
		}

		enum class CollisionCode : uint8_t
		{
			None,
			Boundary,
			Inside,
		};

		// rigidBody::is_collision(left = this, right = other).
		inline CollisionCode is_collision(const Box& l, const Box& r)
		{
			const float l_left = l.center.x - l.size.x / 2;
			const float l_right = l.center.x + l.size.x / 2;
			const float l_top = l.center.y - l.size.y / 2;
			const float l_bottom = l.center.y + l.size.y / 2;
			const float r_left = r.center.x - r.size.x / 2;
			const float r_right = r.center.x + r.size.x / 2;
			const float r_top = r.center.y - r.size.y / 2;
			const float r_bottom = r.center.y + r.size.y / 2;

			const bool right_inside = l_right - r_left > move_epsilon + 1.0f;
			const bool left_inside = l_left - r_right < move_epsilon + 1.0f;
			const bool bottom_inside = l_bottom - r_top > move_epsilon + 1.0f;
			const bool top_inside = l_top - r_bottom < move_epsilon + 1.0f;

			if (right_inside && left_inside && bottom_inside && top_inside)
			{
				return CollisionCode::Inside;
			}

			const bool right_boundary = l_right - r_left > move_epsilon;
			const bool left_boundary = l_left - r_right < move_epsilon;
			const bool bottom_boundary = l_bottom - r_top > move_epsilon;
			const bool top_boundary = l_top - r_bottom < move_epsilon;

			if (right_boundary && left_boundary && bottom_boundary && top_boundary)
			{
				return CollisionCode::Boundary;
			}

			return CollisionCode::None;
		}

		// ---- character (Common/characterCollision.cpp)

		// character::check_angle. Only limits slopes while moving right: for
		// a left velocity the degree is negated and always passes. NaN
		// (position = vector_inf) fails.
		inline bool check_angle(const SimCharacter& ch, const Vec2& position)
		{
			const Vec2 unit = normalized(position - bottom(ch));
			const float radian = unit_angle(unit);
			const float positional_radian = unit.x < 0 ? flip_radian_polarity(radian) : radian;
			const float reset_polarity = std::fabs(positional_radian);
			const float velocity_radian = velocity_left(ch) ? -reset_polarity : reset_polarity;
			const float degree = to_degree(velocity_radian);

			return degree <= max_walk_degree;
		}

		// character::search_ground
		inline Vec2 search_ground(
			const SimCharacter& ch, const SimGround& ground, const Vec2& start_position, const bool offset_left,
			const bool reverse)
		{
			const int start_y = reverse ? static_cast<int>(character_hitbox.y) / 2 : 0;
			const int end_y = reverse ? 0 : static_cast<int>(character_hitbox.y);

			for (int y = start_y; reverse ? (y >= end_y) : (y < end_y); reverse ? y-- : y++)
			{
				const Vec2 current_position = start_position + Vec2{0.0f, static_cast<float>(y)};
				const Vec2 next_position = ground.safe_parallel_surface_global(current_position, offset_left) + current_position;

				const Vec2 unit = normalized(next_position - bottom(ch));
				const float radian = unit_angle(unit);

				if (same(unit, Vec2{}) || std::isnan(radian))
				{
					continue;
				}

				return next_position;
			}

			return vector_inf;
		}

		// character::get_forward_ground
		inline Vec2 get_forward_ground(const SimCharacter& ch, const SimGround& ground, const bool reverse = false)
		{
			const bool uphilling = is_moving_toward(ch, ground);
			const Vec2 ray_start_pos = uphilling ? offset_backward_position(ch) : offset_forward_position(ch);
			// uphilling: get_offset(); otherwise get_backward_offset()
			const bool offset_left = uphilling ? facing_left(ch) : !facing_left(ch);

			return search_ground(ch, ground, ray_start_pos, offset_left, reverse);
		}

		// character::get_next_velocity. Uses the facing (get_offset), not
		// the velocity. First looks for the highest intact pixel up to 99 px
		// above the feet in the nearest column ahead, then for the nearest
		// one below in the next 9 columns.
		inline Vec2 get_next_velocity(const SimCharacter& ch, const Vec2& local_position_bottom, const SimGround& ground)
		{
			const bool left_facing = facing_left(ch);

			for (int x = 1; x < 100; x++)
			{
				for (int y = 99; y >= 1; --y)
				{
					const Vec2 local_new_pos = {
						local_position_bottom.x + static_cast<float>(left_facing ? -x : x),
						local_position_bottom.y - static_cast<float>(y)};

					const Vec2 diff = local_new_pos - local_position_bottom;
					const Vec2 unit = normalized(diff);
					const GroundState ground_check = ground.safe_is_destroyed(local_new_pos);

					if (std::isnan(unit.x) || std::isnan(unit.y) || ground_check == GroundState::OutOfBound)
					{
						continue;
					}

					if (same(unit, Vec2{}))
					{
						continue;
					}

					if (ground_check == GroundState::NotDestroyed)
					{
						return unit;
					}
				}
			}

			for (int i = 1; i < 10; i++)
			{
				for (int j = 1; j < 100; ++j)
				{
					const Vec2 local_new_pos = {
						local_position_bottom.x + static_cast<float>(left_facing ? -i : i),
						local_position_bottom.y + static_cast<float>(j)};

					const Vec2 diff = local_new_pos - local_position_bottom;
					const Vec2 unit = normalized(diff);
					const GroundState ground_check = ground.safe_is_destroyed(local_new_pos);

					if (std::isnan(unit.x) || std::isnan(unit.y))
					{
						continue;
					}

					if (same(unit, Vec2{}) || ground_check == GroundState::OutOfBound)
					{
						continue;
					}

					if (ground_check == GroundState::NotDestroyed)
					{
						return unit;
					}
				}
			}

			return vector_inf;
		}

		// character::ground_walk
		inline void ground_walk(SimCharacter& ch, const CollisionCode collision, const SimGround& ground)
		{
			const Vec2 bottom_local_position = ground.to_top_left_local_position(bottom(ch));
			const GroundState bottom_check = ground.safe_is_destroyed(bottom_local_position);

			if (collision != CollisionCode::Inside || ch.state != CharacterState::Move)
			{
				return;
			}

			const Vec2 candidate = get_next_velocity(ch, bottom_local_position, ground);

			if (same(candidate, Vec2{}))
			{
				return;
			}

			if (!same(candidate, vector_inf))
			{
				ch.velocity = candidate;
			}

			if (!same(candidate, vector_inf) && bottom_check == GroundState::NotDestroyed)
			{
				if (ground.safe_is_object_stuck_global(bottom(ch)))
				{
					const Vec2 delta = ground.safe_nearest_surface(bottom(ch));
					ch.position = ch.position - delta;
				}
			}

			// check ground stiffness
			const Vec2 mid_ground = get_forward_ground(ch, ground, false);

			if (is_moving_toward(ch, ground) && !check_angle(ch, mid_ground))
			{
				ch.velocity = {};
			}
		}

		// character::ground_gravity
		inline void ground_gravity(SimCharacter& ch, const SimGround& ground)
		{
			const GroundState bottom_check = ground.safe_is_destroyed(ground.to_top_left_local_position(bottom(ch)));

			if (bottom_check == GroundState::NotDestroyed)
			{
				ch.gravity_speed = 0.0f;  // reset_current_gravity_speed
				ch.gravity = false;
				ch.grounded = true;
			}
			else if (bottom_check == GroundState::Destroyed || bottom_check == GroundState::OutOfBound)
			{
				ch.gravity = true;
				ch.grounded = false;
				ch.pitch = 0.0f;
			}
		}

		// character::ground_pitching
		inline void ground_pitching(SimCharacter& ch, const SimGround& ground)
		{
			const bool left_facing = facing_left(ch);
			const bool uphilling = ground.safe_is_object_stuck_global(offset_bottom_forward_position(ch));
			Vec2 delta{};

			// Only downhill is measured from the current position; uphill
			// measures from the back corner. search_vector is the backward
			// offset, or the facing when uphilling.
			Vec2 next_surface = offset_bottom_forward_position(ch);
			Vec2 search_vector = left_facing ? Vec2{1.0f, 0.0f} : Vec2{-1.0f, 0.0f};

			if (uphilling)
			{
				next_surface = left_facing ? bottom_right(ch) : bottom_left(ch);
				search_vector = {ch.facing, 0.0f};
			}

			for (int x = 0; x < static_cast<int>(character_hitbox.x); ++x)
			{
				delta = ground.safe_orthogonal_surface_global(
					next_surface + search_vector * static_cast<float>(x),
					static_cast<int>(character_hitbox.y / 2));

				if (!same(delta, vector_inf))
				{
					// Always the backward direction, also when uphilling where
					// the probe went forward (ported as-is).
					delta += (left_facing ? Vec2{1.0f, 0.0f} : Vec2{-1.0f, 0.0f}) * static_cast<float>(x);
					break;
				}
			}

			if (same(delta, vector_inf))
			{
				ch.pitch = 0.0f;
				return;
			}

			next_surface += delta;
			ch.pitch = local_inner_angle(next_surface, bottom(ch));
		}

		// character::on_collision for a ground.
		inline void on_ground_collision(SimCharacter& ch, const CollisionCode collision, const SimGround& ground)
		{
			const Vec2 left_local_position = ground.to_top_left_local_position(left(ch));
			const Vec2 right_local_position = ground.to_top_left_local_position(right(ch));
			const Vec2 ground_hitbox = {static_cast<float>(ground.width()), static_cast<float>(ground.height())};

			const GroundState left_check = ground.safe_is_destroyed(left_local_position + ground_hitbox);
			const GroundState right_check = ground.safe_is_destroyed(right_local_position);
			const Vec2 orthogonal_surface = ground.safe_orthogonal_surface_global(bottom(ch), -1, -1);

			// This ground is under the feet: the "main" ground.
			if (!same(orthogonal_surface, vector_inf))
			{
				// (TurnEnd snapping is not modelled, see SimMove_PORTING.md.)
				ground_walk(ch, collision, ground);
				ground_gravity(ch, ground);
				ground_pitching(ch, ground);
				return;
			}

			if (ch.state == CharacterState::Move &&
				((left_check == GroundState::NotDestroyed && velocity_left(ch)) ||
				 (right_check == GroundState::NotDestroyed && !velocity_left(ch))))
			{
				ch.velocity = {};
			}
			else if (ch.state == CharacterState::Move)
			{
				const Vec2 bottom_local_position = ground.to_top_left_local_position(bottom(ch));
				const Vec2 next_velocity = get_next_velocity(ch, bottom_local_position, ground);
				const bool is_toward = is_moving_toward(ch, ground);

				if (is_toward && !same(next_velocity, vector_inf))
				{
					ch.velocity = next_velocity;
				}
			}
		}

		// ---- rigidBody::move + CharacterController::move (character::move)
		inline void move(SimCharacter& ch, const float dt)
		{
			if (!same(ch.velocity, Vec2{0.0f, 0.0f}))
			{
				ch.position += ch.velocity * character_move_speed(ch.type) * dt;
			}

			if (ch.mp < move_epsilon)
			{
				return;
			}

			if (std::fabs(ch.velocity.x) > move_epsilon)
			{
				ch.mp -= mp_per_second * dt;
			}
		}

		// rigidBody::apply_gravity
		inline void apply_gravity(SimCharacter& ch, const float dt)
		{
			const float diff_y = bottom(ch).y - client_window_max_y;

			if (std::fabs(diff_y) <= move_epsilon)
			{
				ch.gravity_speed = 0.0f;
				return;
			}

			if (ch.gravity_speed <= max_gravity_speed)
			{
				ch.gravity_speed += gravity * dt * 0.5f;
				ch.position += Vec2{0.0f, ch.gravity_speed * dt};
				ch.gravity_speed += gravity * dt * 0.5f;
			}
		}

		// ---- CharacterController (input handling and states)

		enum class Key : uint8_t
		{
			None,
			Down,      // first tick held; Input::getKey is false here
			Pressing,  // Input::getKey
			Up,        // Input::getKeyUp
		};

		inline Key key_state(const bool held, const bool was_held)
		{
			if (held)
			{
				return was_held ? Key::Pressing : Key::Down;
			}
			return was_held ? Key::Up : Key::None;
		}

		// BattleScene::movable (predicate_OOB is its negation). map_height is
		// the map's height, not its bottom coordinate.
		inline bool map_movable(const SimWorld& world, const Vec2& position)
		{
			return position.y <= world.map_height + out_of_bounds_margin;
		}

		// CharacterController::default_state
		inline void default_state(const SimWorld& world, SimCharacter& ch)
		{
			if (ch.hp <= 0.0f)
			{
				ch.state = CharacterState::Death;
			}

			if (!map_movable(world, offset_bottom_forward_position(ch)))
			{
				ch.state = CharacterState::Death;
			}
		}

		// rigidBody::move_left/right/stop (CharacterController parts only play sounds)
		inline void move_left(SimCharacter& ch)
		{
			ch.facing = -1.0f;
			ch.velocity = {-1.0f, 0.0f};
		}

		inline void move_right(SimCharacter& ch)
		{
			ch.facing = 1.0f;
			ch.velocity = {1.0f, 0.0f};
		}

		inline void stop(SimCharacter& ch)
		{
			ch.velocity = {};
		}

		// The A/D part shared by idle_state and move_state.
		inline void handle_move_keys(SimCharacter& ch, const Key a, const Key d)
		{
			if (a == Key::Pressing)
			{
				ch.state = CharacterState::Move;
				move_left(ch);
			}
			else if (d == Key::Pressing)
			{
				ch.state = CharacterState::Move;
				move_right(ch);
			}
			else if (a == Key::Up || d == Key::Up)
			{
				ch.state = CharacterState::Idle;
				stop(ch);
			}
		}

		// CharacterController::update for the states movement can be in.
		inline void controller_update(
			const SimWorld& world, SimCharacter& ch, const Key a, const Key d, const bool movable)
		{
			switch (ch.state)
			{
			case CharacterState::Idle:  // idle_state
				default_state(world, ch);
				if (movable)
				{
					handle_move_keys(ch, a, d);
				}
				break;
			case CharacterState::Move:  // move_state
				default_state(world, ch);
				if (movable)
				{
					if (!map_movable(world, offset_bottom_forward_position(ch)))
					{
						ch.state = CharacterState::Idle;
					}
					handle_move_keys(ch, a, d);
				}
				break;
			case CharacterState::Death:  // death_state: set_unmovable() does not stop (state is not Move)
				ch.state = CharacterState::Dead;
				break;
			case CharacterState::Dead:   // dead_state
				break;
			}
		}
	}

	// One client frame of one character, dt = DeltaTime:
	// character::update = rigidBody::update (collide with every other
	// character, then every ground, in scene order; move; gravity) followed
	// by CharacterController::update (state + held keys). `movable` is
	// CharacterController::is_movable_localplayer(): true only for the
	// current player while it may move; everyone else passes None/false and
	// only falls. Returns true if the character died this tick (state became
	// Death, which is when the client reports its own death); alive is
	// cleared then.
	inline bool step_character(
		SimWorld& world, SimCharacter& ch, const MoveInput input, const bool movable, const float dt = tick_seconds)
	{
		using namespace MoveDetail;

		// Input::checkKeyState
		const Key a = key_state(input == MoveInput::Left, ch.left_held);
		const Key d = key_state(input == MoveInput::Right, ch.right_held);
		ch.left_held = input == MoveInput::Left;
		ch.right_held = input == MoveInput::Right;

		// rigidBody::update. Scene objects are the characters (player id
		// order) followed by the grounds (map order). Touching another
		// character only counts as "collided" (character::on_collision
		// ignores non-ground bodies), which suppresses on_nocollison.
		bool collided = false;

		for (const auto& other : world.characters)
		{
			if (&other != &ch && is_collision(ch.box(), other.box()) != CollisionCode::None)
			{
				collided = true;
			}
		}

		for (const auto& ground : world.grounds)
		{
			const CollisionCode code = is_collision(ch.box(), ground.box());

			if (code != CollisionCode::None)
			{
				collided = true;
				on_ground_collision(ch, code, ground);
			}
		}

		if (!collided)
		{
			// character::on_nocollison
			ch.gravity = true;
			ch.grounded = false;
		}

		move(ch, dt);

		if (ch.gravity)
		{
			apply_gravity(ch, dt);
		}

		const bool was_dying = ch.state == CharacterState::Death || ch.state == CharacterState::Dead;
		controller_update(world, ch, a, d, movable);

		if (!was_dying && ch.state == CharacterState::Death)
		{
			ch.alive = false;
			return true;
		}

		return false;
	}

	// One frame of every character in scene (player id) order. Only
	// `current` gets the input and may move.
	inline void step_world(
		SimWorld& world, const PlayerID current, const MoveInput input, const bool current_movable,
		const float dt = tick_seconds)
	{
		for (auto& ch : world.characters)
		{
			const bool is_current = ch.id == current;
			step_character(world, ch, is_current ? input : MoveInput::None, is_current && current_movable, dt);
		}
	}

	// Round::apply_turn_change for the player whose turn ended:
	// set_unmovable() stops it if it is in Move, finish_replay() refills mp
	// and returns it to Idle unless it is dying or dead.
	inline void finish_turn(SimCharacter& ch)
	{
		if (ch.state == CharacterState::Move)
		{
			MoveDetail::stop(ch);
		}

		ch.mp = character_full_mp;

		if (ch.state != CharacterState::Death && ch.state != CharacterState::Dead)
		{
			ch.state = CharacterState::Idle;
		}
	}

	// The fall-death condition of CharacterController::default_state
	// (BattleScene::predicate_OOB at the bottom forward corner).
	inline bool is_out_of_bounds(const SimWorld& world, const SimCharacter& ch)
	{
		return !MoveDetail::map_movable(world, MoveDetail::offset_bottom_forward_position(ch));
	}

	// BattleScene::spawnpoints. world.characters must be in player id order
	// (the client iterates a std::map<PlayerID, ...>). The first player
	// goes 150 px right of the leftmost ground edge, the second at
	// right edge - right edge / n; each is dropped from y = -1000 onto the
	// first ground (map order) with an intact pixel in its column.
	inline void spawn_characters(SimWorld& world)
	{
		if (world.characters.empty())
		{
			return;  // the client divides by the player count
		}

		int ground_x_min = INT_MAX;
		int ground_x_max = INT_MIN;

		for (const auto& ground : world.grounds)
		{
			ground_x_min = std::min(ground_x_min, static_cast<int>(ground.top_left().x));
			ground_x_max = std::max(ground_x_max, static_cast<int>(ground.center().x + static_cast<float>(ground.width()) / 2));
		}

		const int x_interval = ground_x_max / static_cast<int>(world.characters.size());
		int pivot = ground_x_min;
		int x_current = pivot + 150;

		for (auto& ch : world.characters)
		{
			ch.position = {static_cast<float>(x_current), -1000.0f};

			for (const auto& ground : world.grounds)
			{
				const Vec2 delta = ground.safe_orthogonal_surface_global(ch.position);

				if (MoveDetail::same(delta, vector_inf))
				{
					continue;
				}

				ch.position += delta;
				ch.position = ch.position - Vec2{0.0f, character_hitbox.y / 2};
				break;
			}

			pivot = pivot == ground_x_min ? ground_x_max : ground_x_min;
			x_current = pivot - x_interval;
		}
	}
}

#endif // SIM_MOVE_HPP
