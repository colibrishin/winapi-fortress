#pragma once
#ifndef SIM_SHOT_HPP
#define SIM_SHOT_HPP

#include <cstdint>
#include <vector>

#include "SimData.hpp"
#include "SimGround.hpp"
#include "SimMath.hpp"
#include "../MapLayout.hpp"

#include <string>

namespace Fortress::Sim
{
	using PlayerID = int;

	struct SimCharacter
	{
		PlayerID id = 0;
		CharacterType type = CharacterType::Cannon;
		Vec2 position;            // centre
		float facing = 1.0f;      // +1 right, -1 left (Math::right / Math::left)
		float pitch = 0.0f;       // terrain slope under the character, radians
		float hp = character_full_hp;
		bool alive = true;
		int hits_this_turn = 0;   // previous hits for the consecutive hit bonus

		Box box() const { return {position, character_hitbox}; }
	};

	struct SimWorld
	{
		std::vector<SimGround> grounds;
		std::vector<SimCharacter> characters;  // kept in player id order
		float map_height = 0.0f;               // BattleScene::m_map_size.y

		SimCharacter* find(const PlayerID id)
		{
			for (auto& ch : characters)
			{
				if (ch.id == id)
				{
					return &ch;
				}
			}
			return nullptr;
		}

		// Height of the bounding box of all grounds, as evaluate_map_size().
		void update_map_height()
		{
			if (grounds.empty())
			{
				map_height = 0.0f;
				return;
			}

			float top = grounds.front().box().top_left().y;
			float bottom = grounds.front().box().bottom_right().y;
			for (const auto& g : grounds)
			{
				top = std::min(top, g.box().top_left().y);
				bottom = std::max(bottom, g.box().bottom_right().y);
			}
			map_height = static_cast<float>(static_cast<int>(bottom) - static_cast<int>(top));
		}
	};

	struct ShotInput
	{
		PlayerID shooter = 0;
		ShotKind kind = ShotKind::Main;
		float charge = 0.0f;      // clamped to [min_charge, max_charge]
		int wind = 0;             // px/s^2, already rounded by the server
		int volleys = 1;          // 2 with the DoubleShot item
		bool double_damage = false;
		bool teleport = false;    // Teleport item: shooter moves to where the nutshell lands
	};

	enum class ShotEventType : uint8_t
	{
		Spawn,       // projectile created: position
		Explosion,   // terrain destroyed: position, value = radius
		Damage,      // player hit: value = damage, hp = hp after
		Death,       // player hp reached 0
		Teleport,    // player moved: position
		End,         // projectile removed: position
	};

	struct ShotEvent
	{
		int tick = 0;
		ShotEventType type = ShotEventType::Spawn;
		int projectile = -1;
		PlayerID player = -1;
		Vec2 position;
		float value = 0.0f;
		float hp = 0.0f;
	};

	struct PathSample
	{
		int tick = 0;
		int projectile = 0;
		Vec2 position;
	};

	struct ShotResult
	{
		std::vector<PathSample> path;
		std::vector<ShotEvent> events;
		int end_tick = 0;
		bool timed_out = false;
	};

	constexpr int path_sample_interval_ticks = 2;  // 30 Hz
	constexpr int max_shot_ticks = 30 * ticks_per_second;

	namespace Detail
	{
		struct Projectile
		{
			int id = 0;
			ProjectileSpec spec{};
			Vec2 position;
			Vec2 direction;       // rigidBody::m_velocity
			Vec2 current_speed;   // rigidBody::m_curr_speed
			float gravity_speed = 0.0f;
			float wind = 0.0f;
			int hit_count = 0;
			int ticks_since_hit = 0;
			bool active = true;

			Box box() const { return {position, projectile_hitbox}; }

			bool in_cooldown() const
			{
				return hit_count != 0 && ticks_since_hit < hit_cooldown_ticks;
			}
		};

		// Direction and muzzle position, as character::fire and
		// character::initialize_projectile compute them.
		inline void muzzle(const SimCharacter& shooter, Vec2& position, Vec2& direction)
		{
			const float p = shooter.pitch;
			direction = Vec2{std::cos(p), std::sin(p)} * shooter.facing;

			const Vec2 top_forward = shooter.position + Vec2{shooter.facing, -1.0f} * (character_hitbox * 0.5f);
			const Vec2 forward = Vec2{shooter.facing, -1.0f} * projectile_hitbox;
			position = top_forward + forward.rotate(p);
		}

		inline bool hits_ground(const SimWorld& world, const Projectile& prj)
		{
			Vec2 points[9];
			prj.box().hit_points(points);

			for (const auto& ground : world.grounds)
			{
				if (!ground.box().overlaps(prj.box()))
				{
					continue;
				}

				for (const auto& p : points)
				{
					if (ground.is_solid_global(p))
					{
						return true;
					}
				}
			}

			return false;
		}

		inline bool hits_character(const SimWorld& world, const Projectile& prj)
		{
			for (const auto& ch : world.characters)
			{
				if (ch.alive && ch.box().overlaps(prj.box()))
				{
					return true;
				}
			}

			return false;
		}
	}

	// Runs one turn's shot to completion and records what happened. The
	// world (terrain, hp, positions) is updated in place. Deterministic:
	// same world + input gives the same result.
	inline ShotResult simulate_shot(SimWorld& world, const ShotInput& input)
	{
		using Detail::Projectile;

		ShotResult result;
		SimCharacter* shooter = world.find(input.shooter);

		if (shooter == nullptr || !shooter->alive)
		{
			return result;
		}

		const CharacterType shooter_type = shooter->type;
		const ProjectileSpec spec = projectile_spec(shooter_type, input.kind);
		const float charge = std::min(max_charge, std::max(min_charge, input.charge));

		std::vector<Projectile> projectiles;
		int volleys_fired = 0;
		int next_in_volley = 0;
		int next_spawn_tick = 0;
		int tick = 0;

		const auto emit = [&](const ShotEventType type, const int prj, const PlayerID player, const Vec2& pos,
		                      const float value = 0.0f, const float hp = 0.0f)
		{
			result.events.push_back({tick, type, prj, player, pos, value, hp});
		};

		const auto explode = [&](Projectile& prj)
		{
			for (auto& ground : world.grounds)
			{
				ground.destroy_circle(prj.position, prj.spec.radius);
			}
			emit(ShotEventType::Explosion, prj.id, -1, prj.position, prj.spec.radius);

			if (prj.spec.damage <= 0.0f)
			{
				return;
			}

			Vec2 points[9];
			prj.box().hit_points(points);

			for (auto& ch : world.characters)
			{
				if (!ch.alive)
				{
					continue;
				}

				// scene::is_in_range_nearest: closest pair of projectile hit
				// point and the character's boundary point within the radius.
				float best = prj.spec.radius;
				bool in_range = false;
				Vec2 near_point;

				for (const auto& p : points)
				{
					const Vec2 n = ch.box().nearest_boundary_point(p);
					const float d = (p - n).magnitude();
					if (d <= best)
					{
						best = d;
						near_point = n;
						in_range = true;
					}
				}

				if (!in_range)
				{
					continue;
				}

				const float damage = calculate_damage(
					prj.spec, ch.type, ch.hits_this_turn, input.double_damage, prj.position, near_point);

				ch.hp = std::max(0.0f, ch.hp - damage);
				ch.hits_this_turn++;
				emit(ShotEventType::Damage, prj.id, ch.id, ch.position, damage, ch.hp);

				if (ch.hp <= 0.0f)
				{
					ch.alive = false;
					emit(ShotEventType::Death, prj.id, ch.id, ch.position);
				}
			}
		};

		const auto finish = [&](Projectile& prj, const bool landed)
		{
			prj.active = false;
			emit(ShotEventType::End, prj.id, -1, prj.position, landed ? 1.0f : 0.0f);
			result.path.push_back({tick, prj.id, prj.position});

			if (input.teleport && input.kind == ShotKind::Nutshell && landed && shooter->alive)
			{
				shooter->position = prj.position;
				emit(ShotEventType::Teleport, prj.id, shooter->id, prj.position);
			}
		};

		const auto any_active = [&]
		{
			for (const auto& p : projectiles)
			{
				if (p.active)
				{
					return true;
				}
			}
			return false;
		};

		for (; tick < max_shot_ticks; ++tick)
		{
			// Spawn: a volley fires fire_count projectiles 0.1 s apart; the next
			// volley (DoubleShot) starts once the previous one is gone.
			if (volleys_fired < input.volleys && tick >= next_spawn_tick && shooter->alive)
			{
				if (next_in_volley < spec.fire_count)
				{
					Projectile prj;
					prj.id = static_cast<int>(projectiles.size());
					prj.spec = spec;
					Detail::muzzle(*shooter, prj.position, prj.direction);
					prj.current_speed = spec.speed * charge;
					prj.wind = static_cast<float>(input.wind);
					projectiles.push_back(prj);

					emit(ShotEventType::Spawn, prj.id, shooter->id, prj.position);
					result.path.push_back({tick, prj.id, prj.position});

					next_in_volley++;
					next_spawn_tick = tick + multi_shot_interval_ticks;
				}

				if (next_in_volley == spec.fire_count && !any_active())
				{
					volleys_fired++;
					next_in_volley = 0;
					next_spawn_tick = tick + 1;
				}
			}

			for (auto& prj : projectiles)
			{
				if (!prj.active)
				{
					continue;
				}

				prj.ticks_since_hit++;

				// Guided missile: steer toward the nearest other character whose
				// boundary is within range of the leading bottom corner.
				if (prj.spec.guided)
				{
					const bool going_left = prj.direction.x < 0.0f;
					const Box b = prj.box();
					const Vec2 lead = going_left ? Vec2{b.top_left().x, b.bottom_right().y} : b.bottom_right();

					const SimCharacter* nearest = nullptr;
					float nearest_distance = 0.0f;

					for (const auto& ch : world.characters)
					{
						if (!ch.alive || (lead - ch.box().nearest_boundary_point(lead)).magnitude() > guided_lock_range)
						{
							continue;
						}

						const float d = (ch.position - lead).magnitude();
						if (nearest == nullptr || d < nearest_distance)
						{
							nearest = &ch;
							nearest_distance = d;
						}
					}

					if (nearest != nullptr && nearest->id != input.shooter)
					{
						const Box t = nearest->box();
						const Vec2 target = going_left ? Vec2{t.top_left().x, t.bottom_right().y} : t.bottom_right();
						prj.direction = (target - lead).normalized();
					}
				}

				// Collisions at the current position.
				if (Detail::hits_character(world, prj) && !prj.in_cooldown())
				{
					prj.hit_count++;
					prj.ticks_since_hit = 0;
					explode(prj);
				}
				else if (prj.hit_count < prj.spec.max_hit_count && Detail::hits_ground(world, prj))
				{
					prj.hit_count++;
					prj.ticks_since_hit = 0;
					explode(prj);
				}

				if (prj.hit_count >= prj.spec.max_hit_count)
				{
					finish(prj, true);
					continue;
				}

				// Integrate (rigidBody::move, apply_gravity, projectile::flying).
				prj.position += prj.direction * prj.current_speed * tick_seconds;

				prj.gravity_speed += gravity * tick_seconds * 0.5f;
				prj.position.y += prj.gravity_speed * tick_seconds;
				prj.gravity_speed += gravity * tick_seconds * 0.5f;

				const bool with_wind = (prj.direction.x < 0.0f) == (prj.wind < 0.0f);
				prj.current_speed.x += (with_wind ? 1.0f : -1.0f) * std::fabs(prj.wind) * tick_seconds;

				if (prj.position.y > world.map_height + out_of_bounds_margin)
				{
					finish(prj, false);
					continue;
				}

				if (tick % path_sample_interval_ticks == 0)
				{
					result.path.push_back({tick, prj.id, prj.position});
				}
			}

			// A shooter killed by its own blast fires no further projectiles.
			if ((volleys_fired >= input.volleys || !shooter->alive) && !any_active())
			{
				break;
			}
		}

		if (tick >= max_shot_ticks)
		{
			result.timed_out = true;
			for (auto& prj : projectiles)
			{
				if (prj.active)
				{
					finish(prj, false);
				}
			}
		}

		result.end_tick = tick;
		return result;
	}

	// Builds the grounds of a map from Common/MapLayout.hpp. cloud_mask is
	// the path of the cloud mask BMP (the layout's path is relative to the
	// client's working directory). Returns false if a mask cannot be read.
	template <size_t N>
	bool build_map(SimWorld& world, const Map::Layout::GroundEntry (&layout)[N], const std::string& cloud_mask = {})
	{
		using Map::Layout::GroundShape;
		world.grounds.clear();

		for (const auto& entry : layout)
		{
			const Vec2 center{entry.x, entry.y};
			const int width = static_cast<int>(entry.width);
			const int height = static_cast<int>(entry.height);

			switch (entry.shape)
			{
			case GroundShape::Flat:
				world.grounds.emplace_back(center, width, height);
				break;
			case GroundShape::StairwayRampLeft:
			case GroundShape::StairwayRampRight:
				world.grounds.push_back(
					SimGround::stairway(center, width, height, entry.shape == GroundShape::StairwayRampRight));
				break;
			case GroundShape::Cloud:
			{
				SimGround cloud(center, 1, 1);
				if (!SimGround::from_mask_bmp(cloud_mask, center, cloud))
				{
					return false;
				}
				world.grounds.push_back(std::move(cloud));
				break;
			}
			}
		}

		world.update_map_height();
		return true;
	}

	inline void build_desert_map(SimWorld& world)
	{
		build_map(world, Map::Layout::desert);
	}
}

#endif // SIM_SHOT_HPP
