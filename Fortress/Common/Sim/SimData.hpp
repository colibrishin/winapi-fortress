#pragma once
#ifndef SIM_DATA_HPP
#define SIM_DATA_HPP

// Gameplay rules for the simulation. Character and projectile numbers come
// from Common/PropertyTable.hpp, the same table the client classes read
// through Client/CharacterProperties.hpp.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "SimMath.hpp"
#include "../PropertyTable.hpp"

namespace Fortress::Sim
{
	enum class CharacterType : uint8_t
	{
		Cannon,
		Missile,
		Secwind,
	};

	enum class ShotKind : uint8_t
	{
		Main,
		Sub,
		Nutshell,
	};

	struct ProjectileSpec
	{
		Vec2 speed;      // multiplied by the charge, component-wise with the direction
		float damage;
		float radius;    // blast radius: terrain destruction and damage falloff
		float penetration;
		int max_hit_count;
		int fire_count;  // projectiles per volley, 0.1 s apart
		bool guided;
	};

	namespace Table = Object::Property::Table;

	constexpr Vec2 character_hitbox = {Table::character_hitbox[0], Table::character_hitbox[1]};
	constexpr Vec2 projectile_hitbox = {Table::projectile_hitbox[0], Table::projectile_hitbox[1]};

	constexpr float character_full_hp = 100.0f;
	constexpr float character_full_mp = 1000.0f;
	constexpr float max_charge = 250.0f;
	constexpr float min_charge = 10.0f;  // character::fire uses 10 when nothing was charged

	constexpr float gravity = 9.80f * 1.5f;  // Math::G_ACC, px/s^2

	// Projectile hit cooldown against characters (ProjectileController::is_cooldown).
	constexpr int hit_cooldown_ticks = seconds_to_ticks(0.5f);
	// Multi-shot interval (ProjectileTimer).
	constexpr int multi_shot_interval_ticks = seconds_to_ticks(0.1f);
	// Guided missile lock-on range from its leading bottom corner.
	constexpr float guided_lock_range = 100.0f;
	// Projectiles below map height + this are out of bounds (BattleScene::movable).
	constexpr float out_of_bounds_margin = 500.0f;

	inline const wchar_t* short_name(const CharacterType type)
	{
		switch (type)
		{
		case CharacterType::Cannon: return L"cannon";
		case CharacterType::Missile: return L"missile";
		case CharacterType::Secwind: return L"secwind";
		}

		return L"";
	}

	inline ProjectileSpec projectile_spec(const CharacterType type, const ShotKind kind)
	{
		if (kind == ShotKind::Nutshell)
		{
			// NutShellProjectile (Teleport item): the shooter's main speed, no
			// damage, radius 1, one hit.
			ProjectileSpec nutshell = projectile_spec(type, ShotKind::Main);
			nutshell = {nutshell.speed, 0.0f, 1.0f, 0.0f, 1, 1, false};
			return nutshell;
		}

		const auto* p = Table::find_projectile(short_name(type), kind == ShotKind::Main ? L"main" : L"sub");

		if (p == nullptr)
		{
			return {};
		}

		// Homing is behaviour of GuidedMissileProjectile, the missile's sub shot.
		const bool guided = type == CharacterType::Missile && kind == ShotKind::Sub;

		return {{p->speed_x, p->speed_y}, p->damage, p->radius, p->penetration, p->hit_count, p->fire_count, guided};
	}

	inline float character_armor(const CharacterType type)
	{
		const auto* c = Table::find_character(short_name(type));
		return c ? c->armor : 1.0f;
	}

	// Same formula as Server/ClientSide.hpp calculate_damage.
	// hit_point is the projectile centre, near_point the victim's nearest
	// boundary point; previous_hits is how often the victim was already hit
	// this turn.
	inline float calculate_damage(
		const ProjectileSpec& spec,
		const CharacterType victim,
		const int previous_hits,
		const bool double_damage,
		const Vec2& hit_point,
		const Vec2& near_point)
	{
		const float close_hit_epsilon = projectile_hitbox.magnitude() / 2.0f;
		const float distance = (hit_point - near_point).magnitude();

		const float normalized_rate = std::min(1.0f, std::fabs(spec.penetration / character_armor(victim)));
		const float pen_damage = spec.damage * normalized_rate;

		const float close_hit_compensation = std::fabs(distance - close_hit_epsilon);
		const float dist_ratio = std::log(close_hit_compensation) / std::log(spec.radius);
		const float far_dist_ratio = std::min(1.0f, std::max(dist_ratio, 0.0f));

		const float dist_pen_damage = pen_damage * (1.0f - far_dist_ratio);
		float total = dist_pen_damage + dist_pen_damage * (static_cast<float>(previous_hits) * 0.25f);

		if (double_damage)
		{
			total += total;
		}

		return total;
	}
}

#endif // SIM_DATA_HPP
