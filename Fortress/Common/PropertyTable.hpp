#pragma once
#ifndef PROPERTY_TABLE_HPP
#define PROPERTY_TABLE_HPP

// Single source of the character and projectile numbers. The client reads
// them through Client/CharacterProperties.hpp (character and projectile
// class constructors), the server's simulation reads them directly.
// Keep this header free of Windows/GDI includes: Common/Sim uses it.

#include <string>

namespace Fortress::Object::Property::Table
{
	struct ProjectileProperty
	{
		float speed_x;
		float speed_y;
		float damage;
		float radius;
		float penetration;
		int hit_count;   // explosions before the projectile is removed
		int fire_count;  // projectiles per shot, 0.1 s apart
		// Launch speed relative to shot_max_speed (server trajectory).
		// speed_x/speed_y above are the client's old per-axis speeds.
		float launch_scale;
	};

	struct ProjectileEntry
	{
		const wchar_t* short_name;
		const wchar_t* type;
		ProjectileProperty property;
	};

	// Base speed {1, 2} scaled per projectile.
	inline constexpr ProjectileEntry projectiles[] = {
		{L"cannon", L"main", {1.0f, 2.0f, 10.0f, 50.0f, 0.9f, 1, 1, 1.0f}},
		{L"cannon", L"sub", {2.0f, 4.0f, 25.0f, 15.0f, 1.0f, 1, 1, 1.1f}},
		{L"missile", L"main", {1.5f, 3.0f, 10.0f, 30.0f, 0.9f, 2, 1, 1.0f}},
		{L"missile", L"sub", {1.5f, 3.0f, 10.0f, 10.0f, 0.7f, 1, 1, 1.05f}},
		{L"secwind", L"main", {1.5f, 3.0f, 30.0f, 30.0f, 0.7f, 1, 1, 0.95f}},
		{L"secwind", L"sub", {2.0f, 4.0f, 10.0f, 10.0f, 0.5f, 1, 3, 1.1f}},
	};

	struct CharacterEntry
	{
		const wchar_t* short_name;
		float armor;
		// Aim angle above the barrel's slope, degrees (tuning values, not
		// taken from the original game).
		float aim_min;
		float aim_max;
		float aim_start;
	};

	inline constexpr CharacterEntry characters[] = {
		{L"cannon", 0.5f, 10.0f, 70.0f, 45.0f},
		{L"missile", 1.0f, 20.0f, 60.0f, 40.0f},
		{L"secwind", 0.7f, 30.0f, 80.0f, 55.0f},
	};

	// Aim change per second while Up/Down is held.
	inline constexpr float aim_degrees_per_second = 30.0f;

	inline constexpr float character_hitbox[2] = {50.0f, 50.0f};
	inline constexpr float projectile_hitbox[2] = {30.0f, 30.0f};

	inline constexpr float character_full_hp = 100.0f;
	inline constexpr float character_full_mp = 1000.0f;
	inline constexpr float character_max_charge = 250.0f;
	// Charge used when the player fires without charging (character::fire).
	inline constexpr float character_min_charge = 10.0f;

	// Gravitational acceleration in px/s^2 (Math::G_ACC). Characters fall
	// with it (client movement, ported as-is to Sim/SimMove.hpp).
	inline constexpr float gravity_acceleration = 9.80f * 1.5f;

	// Projectile trajectory (server, Sim/SimShot.hpp): a parabola
	// p(t) = p0 + v0*t + (wind, g)*t^2/2. Chosen so that a full-charge 45
	// degree shot without wind flies about 1200 px (the desert map is
	// 1100 px wide) in about 2.5 s, and the strongest wind (50) moves the
	// landing point by about 300 px.
	inline constexpr float shot_max_speed = 680.0f;      // px/s at max charge, launch_scale 1
	inline constexpr float shot_gravity = 385.0f;        // px/s^2
	inline constexpr float shot_wind_acceleration = 2.0f; // px/s^2 per wind unit

	// Nutshell (Teleport item): flies at the shooter's main projectile speed,
	// does no damage and only marks where the shooter lands.
	inline constexpr float nutshell_damage = 0.0f;
	inline constexpr float nutshell_radius = 1.0f;
	inline constexpr float nutshell_penetration = 0.0f;
	inline constexpr int nutshell_hit_count = 1;
	inline constexpr int nutshell_fire_count = 1;

	// A projectile that already hit cannot hit a character again for this long.
	inline constexpr float projectile_hit_cooldown_seconds = 0.5f;
	// Interval between the projectiles of a multi-shot (ProjectileTimer).
	inline constexpr float multi_shot_interval_seconds = 0.1f;
	// Guided missile lock-on range from its leading bottom corner.
	inline constexpr float guided_lock_range = 100.0f;
	// Objects below map height + this are out of bounds (BattleScene::movable).
	inline constexpr float out_of_bounds_margin = 500.0f;

	// nullptr for unknown names (e.g. the nutshell's empty type).
	inline const ProjectileProperty* find_projectile(const std::wstring& short_name, const std::wstring& type)
	{
		for (const auto& entry : projectiles)
		{
			if (short_name == entry.short_name && type == entry.type)
			{
				return &entry.property;
			}
		}

		return nullptr;
	}

	inline const CharacterEntry* find_character(const std::wstring& short_name)
	{
		for (const auto& entry : characters)
		{
			if (short_name == entry.short_name)
			{
				return &entry;
			}
		}

		return nullptr;
	}
}

#endif // PROPERTY_TABLE_HPP
