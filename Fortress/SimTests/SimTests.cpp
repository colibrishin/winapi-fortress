// Headless tests for gameplay rules that the server will own.
// No framework: each CHECK prints on failure, and the exit code is the
// number of failed checks (0 = all passed).

#include <cmath>
#include <iostream>
#include <string>

#include "../Server/ClientSide.hpp"
#include "Check.hpp"
#include "../Common/Sim/SimData.hpp"
#include "../Common/math.h"

#include <cwchar>
#include <utility>

#pragma comment (lib, "Common.lib")

namespace
{
	using SimTests::check_near;

	using Fortress::eProjectileType;
	using Fortress::Math::Vector2;
	using Fortress::Network::eCharacterType;
	using Fortress::Network::Server::calculate_damage;

	// Projectile hitbox is 30x30, so the "close hit" epsilon is half its
	// diagonal: sqrt(30^2 + 30^2) / 2.
	const float close_hit_epsilon = std::sqrt(30.0f * 30.0f * 2.0f) / 2.0f;
	const Vector2 projectile_hitbox = {30.0f, 30.0f};
	const Vector2 near_point = {100.0f, 100.0f};

	// Hit point whose distance from near_point leaves `compensation` after
	// the close-hit epsilon is subtracted.
	Vector2 hit_at(const float compensation)
	{
		return {near_point.get_x() + close_hit_epsilon + compensation, near_point.get_y()};
	}

	// Expected values below are worked out by hand from the formula in
	// Server/ClientSide.hpp and the tables in Client/CharacterProperties.hpp,
	// not captured from a run, so a formula or table change shows up here.
	void test_damage_formula()
	{
		// cannon main (dmg 10, pen 0.9) vs missile (armor 1.0): pen rate 0.9.
		// Compensation ~0 -> log ratio negative -> clamped to 0 -> full 9.
		check_near(
			calculate_damage(eCharacterType::CannonCharacter, eCharacterType::MissileCharacter,
				eProjectileType::Main, 0, false, projectile_hitbox, hit_at(0.0f), near_point),
			9.0f, 1e-3f, "direct hit uses pen/armor rate");

		// missile sub (dmg 10, pen 0.7) vs cannon (armor 0.5): rate 1.4 -> capped at 1.
		// Compensation 1 -> log 0 -> no falloff -> 10.
		check_near(
			calculate_damage(eCharacterType::MissileCharacter, eCharacterType::CannonCharacter,
				eProjectileType::Sub, 0, false, projectile_hitbox, hit_at(1.0f), near_point),
			10.0f, 1e-3f, "pen/armor rate is capped at 1");

		// cannon sub (dmg 25, radius 15) vs cannon: compensation = radius -> ratio 1 -> 0.
		check_near(
			calculate_damage(eCharacterType::CannonCharacter, eCharacterType::CannonCharacter,
				eProjectileType::Sub, 0, false, projectile_hitbox, hit_at(15.0f), near_point),
			0.0f, 1e-3f, "hit at blast radius does no damage");

		// secwind main (dmg 30, pen 0.7, radius 30) vs secwind (armor 0.7): rate 1.
		// Compensation sqrt(30) -> ratio 0.5 -> 15.
		const Vector2 half_falloff = hit_at(std::sqrt(30.0f));
		check_near(
			calculate_damage(eCharacterType::SecwindCharacter, eCharacterType::SecwindCharacter,
				eProjectileType::Main, 0, false, projectile_hitbox, half_falloff, near_point),
			15.0f, 1e-3f, "log falloff at sqrt(radius) halves damage");

		// Same hit with 2 previous hits: +25% each -> 15 * 1.5 = 22.5.
		check_near(
			calculate_damage(eCharacterType::SecwindCharacter, eCharacterType::SecwindCharacter,
				eProjectileType::Main, 2, false, projectile_hitbox, half_falloff, near_point),
			22.5f, 1e-3f, "consecutive hit bonus is 25% per previous hit");

		// And with double damage on top: 22.5 * 2 = 45.
		check_near(
			calculate_damage(eCharacterType::SecwindCharacter, eCharacterType::SecwindCharacter,
				eProjectileType::Main, 2, true, projectile_hitbox, half_falloff, near_point),
			45.0f, 1e-3f, "double damage doubles the total");

		// Nutshell (teleport item) has no damage entry.
		check_near(
			calculate_damage(eCharacterType::CannonCharacter, eCharacterType::CannonCharacter,
				eProjectileType::Nutshell, 0, false, projectile_hitbox, hit_at(1.0f), near_point),
			0.0f, 1e-3f, "nutshell does no damage");
	}

	// The numbers the client hard-coded before they moved to
	// Common/PropertyTable.hpp (speed, radius, damage, penetration, and the
	// hit/fire counts from the *Projectile.hpp constructors).
	struct LegacyProjectile
	{
		const wchar_t* name;
		const wchar_t* type;
		float speed_x, speed_y, radius, damage, pen;
		int hit_count, fire_count;
	};

	void test_property_table()
	{
		using namespace Fortress::Object::Property;

		const LegacyProjectile legacy[] = {
			{L"cannon", L"main", 1.0f, 2.0f, 50.0f, 10.0f, 0.9f, 1, 1},
			{L"cannon", L"sub", 2.0f, 4.0f, 15.0f, 25.0f, 1.0f, 1, 1},
			{L"missile", L"main", 1.5f, 3.0f, 30.0f, 10.0f, 0.9f, 2, 1},
			{L"missile", L"sub", 1.5f, 3.0f, 10.0f, 10.0f, 0.7f, 1, 1},
			{L"secwind", L"main", 1.5f, 3.0f, 30.0f, 30.0f, 0.7f, 1, 1},
			{L"secwind", L"sub", 2.0f, 4.0f, 10.0f, 10.0f, 0.5f, 1, 3},
		};

		for (const auto& l : legacy)
		{
			const std::string what = std::string(l.name, l.name + std::wcslen(l.name)) + " " +
				std::string(l.type, l.type + std::wcslen(l.type));
			const auto speed = projectile_speed_getter(l.name, l.type);
			SimTests::check(speed.get_x() == l.speed_x && speed.get_y() == l.speed_y, what + " speed");
			SimTests::check(projectile_radius_getter(l.name, l.type) == l.radius, what + " radius");
			SimTests::check(projectile_damage_getter(l.name, l.type) == l.damage, what + " damage");
			SimTests::check(projectile_pen_rate_getter(l.name, l.type) == l.pen, what + " penetration");
			SimTests::check(projectile_hit_count_getter(l.name, l.type) == l.hit_count, what + " hit count");
			SimTests::check(projectile_fire_count_getter(l.name, l.type) == l.fire_count, what + " fire count");
		}

		SimTests::check(character_armor_getter(L"cannon") == 0.5f, "cannon armor");
		SimTests::check(character_armor_getter(L"missile") == 1.0f, "missile armor");
		SimTests::check(character_armor_getter(L"secwind") == 0.7f, "secwind armor");
		SimTests::check(projectile_damage_getter(L"cannon", L"") == 0.0f, "unknown projectile has no damage");

		// Constants that were literals in character.hpp, character.cpp, math.h,
		// NutshellProjectile.hpp, ProjectileController.cpp, ProjectileTimer.hpp,
		// GuidedMissileProjectile.hpp and BattleScene.cpp.
		SimTests::check(Table::character_full_hp == 100.0f && Table::character_full_mp == 1000.0f, "full hp/mp");
		SimTests::check(Table::character_max_charge == 250.0f && Table::character_min_charge == 10.0f, "charge range");
		SimTests::check(Table::gravity_acceleration == 9.80f * 1.5f && Fortress::Math::G_ACC == 9.80f * 1.5f, "gravity");
		SimTests::check(Table::nutshell_damage == 0.0f && Table::nutshell_radius == 1.0f &&
			Table::nutshell_penetration == 0.0f && Table::nutshell_hit_count == 1 && Table::nutshell_fire_count == 1,
			"nutshell numbers");
		SimTests::check(Table::projectile_hit_cooldown_seconds == 0.5f && Table::multi_shot_interval_seconds == 0.1f,
			"hit cooldown and multi-shot interval");
		SimTests::check(Table::guided_lock_range == 100.0f && Table::out_of_bounds_margin == 500.0f,
			"guided range and out of bounds margin");
	}

	// Fortress::Sim::calculate_damage must match the server's formula for
	// every shooter/victim/projectile combination over a range of distances.
	void test_sim_damage_matches_server()
	{
		namespace Sim = Fortress::Sim;

		const std::pair<eCharacterType, Sim::CharacterType> types[] = {
			{eCharacterType::CannonCharacter, Sim::CharacterType::Cannon},
			{eCharacterType::MissileCharacter, Sim::CharacterType::Missile},
			{eCharacterType::SecwindCharacter, Sim::CharacterType::Secwind},
		};
		const std::pair<eProjectileType, Sim::ShotKind> kinds[] = {
			{eProjectileType::Main, Sim::ShotKind::Main},
			{eProjectileType::Sub, Sim::ShotKind::Sub},
		};

		int mismatches = 0;
		int compared = 0;

		for (const auto& [shooter, sim_shooter] : types)
		for (const auto& [victim, sim_victim] : types)
		for (const auto& [kind, sim_kind] : kinds)
		for (int hits = 0; hits < 3; ++hits)
		for (int dd = 0; dd < 2; ++dd)
		for (float d = 0.0f; d <= 60.0f; d += 0.75f)
		{
			const Vector2 hit = hit_at(d);
			const float expected = calculate_damage(
				shooter, victim, kind, hits, dd != 0, projectile_hitbox, hit, near_point);
			const float actual = Sim::calculate_damage(
				Sim::projectile_spec(sim_shooter, sim_kind), sim_victim, hits, dd != 0,
				{hit.get_x(), hit.get_y()}, {near_point.get_x(), near_point.get_y()});

			++compared;
			if (std::fabs(expected - actual) > 1e-4f)
			{
				++mismatches;
			}
		}

		SimTests::check(compared > 1000 && mismatches == 0,
			"Sim::calculate_damage matches server (" + std::to_string(mismatches) + " of " +
			std::to_string(compared) + " differ)");
	}
}

void run_sim_core_tests();  // SimCoreTests.cpp
void run_sim_move_tests();  // SimMoveTests.cpp

int main()
{
	test_damage_formula();
	test_property_table();
	test_sim_damage_matches_server();
	run_sim_core_tests();
	run_sim_move_tests();

	std::cout << (SimTests::g_checked - SimTests::g_failed) << "/" << SimTests::g_checked
		<< " checks passed" << std::endl;
	return SimTests::g_failed;
}
