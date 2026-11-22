// Headless tests for gameplay rules that the server will own.
// No framework: each CHECK prints on failure, and the exit code is the
// number of failed checks (0 = all passed).

#include <cmath>
#include <iostream>
#include <string>

#include "../Server/ClientSide.hpp"

#pragma comment (lib, "Common.lib")

namespace
{
	int g_failed = 0;
	int g_checked = 0;

	void check_near(const float actual, const float expected, const float tolerance, const std::string& what)
	{
		++g_checked;

		if (std::fabs(actual - expected) > tolerance)
		{
			++g_failed;
			std::cout << "FAIL " << what << ": expected " << expected << ", got " << actual << std::endl;
		}
	}

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
}

int main()
{
	test_damage_formula();

	std::cout << (g_checked - g_failed) << "/" << g_checked << " checks passed" << std::endl;
	return g_failed;
}
