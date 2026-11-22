#pragma once
#ifndef CHARACTER_HPP
#define CHARACTER_HPP

#include "CharacterController.hpp"
#include "rigidBody.hpp"
#include "common.h"
#include "sceneManager.hpp"
#include "Texture.hpp"
#include "common.h"
#include "ProjectileTimer.hpp"
#include "PropertyTable.hpp"

// forward declaration for avoiding circular reference
namespace Fortress
{
	namespace Object
	{
		enum class GroundState;
		class Ground;
	}
}

namespace Fortress::ObjectBase
{
	class projectile;

	constexpr float character_full_hp = Object::Property::Table::character_full_hp;
	constexpr float character_full_mp = Object::Property::Table::character_full_mp;
	constexpr float character_max_charge = Object::Property::Table::character_max_charge;

	class character : public Abstract::rigidBody, public Controller::CharacterController
	{
	public:
		character() = delete;
		character& operator=(const character& other) = default;
		character& operator=(character&& other) = default;
		character(const character& other) = default;
		character(character&& other) = default;
		~character() override;

		void initialize() override;
		void update() override;
		void render() override;
		void prerender() override;

		virtual void hit(const std::weak_ptr<ObjectBase::projectile>& prj, const GlobalPosition& hit_point);

		const std::wstring& get_short_name() const;

		void on_collision(
			const CollisionCode& collision,
			const GlobalPosition& collision_point,
			const RigidBodyPointer& other) override;

		void on_nocollison() override;

		float get_armor() const;
		Network::PlayerID get_player_id() const;

		// Creates a projectile of the given kind that only follows a replayed
		// server path (see projectile::start_replay).
		ProjectilePointer spawn_replay_projectile(eProjectileType kind, unsigned int id, const GlobalPosition& position);
		void teleport_to(const GlobalPosition& position);

		// Applies the server's CharacterState. Other players follow it; the
		// local player keeps its own prediction unless it is off by more
		// than local_correction_distance.
		void apply_server_state(const GlobalPosition& position, float facing, float pitch, float mp, bool moving);
		// Prediction quality of the local player (for logs and tests).
		int get_server_corrections() const { return m_server_corrections; }
		float get_max_server_deviation() const { return m_max_server_deviation; }
		virtual Network::eCharacterType get_type() const = 0;
	private:
		Network::PlayerID m_player_id;
		bool m_bGrounded;

		const std::wstring m_short_name;
		float m_armor;
		int m_server_corrections = 0;
		float m_max_server_deviation = 0.0f;

		void move() override;

		void render_hp_bar(const GlobalPosition& position);
		bool check_angle(const GlobalPosition& position, const GroundPointer& ground_ptr) const;


		void ground_walk(const CollisionCode& collision, const GroundPointer& ptr_ground);
		void ground_gravity(const GroundPointer& ptr_ground);
		void ground_pitching(const GroundPointer& ptr_ground);

		GlobalPosition get_forward_ground(const GroundPointer& ground_ptr, const bool& reverse) const;
		GlobalPosition search_ground(const GroundPointer& ground, const GlobalPosition& start_position,
		                             const UnitVector& offset, bool reverse) const;

		UnitVector get_next_velocity(const LocalPosition& local_position_bottom,
		                             const GroundPointer& ground_ptr) const;

		ProjectilePointer initialize_projectile(const unsigned id, const UnitVector& angle, const float charged);

		std::weak_ptr<ProjectileTimer> m_multi_projectile_timer;

	protected:
		void move_left() override;
		void move_right() override;
		void stop() override;
		void fire() override;

		character(
			const Network::PlayerID& player_id,
			const std::wstring& name,
			const std::wstring& short_name,
			const UnitVector& offset,
			const GlobalPosition& position,
			const UnitVector& velocity,
			const float mass,
			const SpeedVector& speed,
			const AccelVector& acceleration,
			const int hp,
			const int mp,
			const float armor);
	};
}

#endif
