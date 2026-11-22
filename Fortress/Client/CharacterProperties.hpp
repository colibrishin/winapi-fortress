#pragma once
#include <string>
#include "../Common/vector2.hpp"
#include "../Common/PropertyTable.hpp"

// Client-facing getters over Common/PropertyTable.hpp, which is shared with
// the server simulation. Unknown names return zero values, as before.
namespace Fortress::Object::Property
{
	inline Math::Vector2 projectile_speed_getter(const std::wstring& short_name, const std::wstring& type)
	{
		const auto* p = Table::find_projectile(short_name, type);
		return p ? Math::Vector2{p->speed_x, p->speed_y} : Math::Vector2{};
	}

	inline float projectile_radius_getter(const std::wstring& short_name, const std::wstring& type)
	{
		const auto* p = Table::find_projectile(short_name, type);
		return p ? p->radius : 0.0f;
	}

	inline float projectile_damage_getter(const std::wstring& short_name, const std::wstring& type)
	{
		const auto* p = Table::find_projectile(short_name, type);
		return p ? p->damage : 0.0f;
	}

	inline float projectile_pen_rate_getter(const std::wstring& short_name, const std::wstring& type)
	{
		const auto* p = Table::find_projectile(short_name, type);
		return p ? p->penetration : 0.0f;
	}

	inline int projectile_hit_count_getter(const std::wstring& short_name, const std::wstring& type)
	{
		const auto* p = Table::find_projectile(short_name, type);
		return p ? p->hit_count : 1;
	}

	inline int projectile_fire_count_getter(const std::wstring& short_name, const std::wstring& type)
	{
		const auto* p = Table::find_projectile(short_name, type);
		return p ? p->fire_count : 1;
	}

	inline float character_armor_getter(const std::wstring& short_name)
	{
		const auto* c = Table::find_character(short_name);
		return c ? c->armor : 0.0f;
	}

	inline SizeVector character_hitbox_getter()
	{
		return {Table::character_hitbox[0], Table::character_hitbox[1]};
	}

	inline SizeVector projectile_hitbox_getter()
	{
		return {Table::projectile_hitbox[0], Table::projectile_hitbox[1]};
	}
}
