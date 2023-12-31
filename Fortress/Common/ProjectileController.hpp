#ifndef PROJECTILECONTROLLER_HPP
#define PROJECTILECONTROLLER_HPP
#include "stateController.hpp"

namespace Fortress
{
	namespace Object
	{
		class Ground;
	}
}

namespace Fortress
{
	namespace ObjectBase
	{
		class character;
	}
}

namespace Fortress::Controller
{
	class ProjectileController : public stateController<eProjectileState>
	{
	public:
		~ProjectileController() override = default;
		ProjectileController(
			const unsigned int id,
			const ObjectBase::character* const shooter,
			const std::wstring& short_name, 
			const Abstract::rigidBody* const rb,
			int max_hit_count, 
			int max_fire_count);

		void initialize() override;
		void update() override;
		void prerender() override;

		bool is_cooldown() const;
		bool is_exploded() const;

		unsigned int get_id() const;
		int get_hit_count() const;
		int get_fire_count() const;
		int get_max_hit_count() const;

		const ObjectBase::character* get_origin() const;
		virtual eProjectileType get_type() const = 0;
		const Network::ProjectileHitMsg& get_hit_msg() const;

	private:
		void set_current_sprite(const eProjectileState&) override;
		DirVector get_moving_direction() const;

		void fire_state();
		void flying_state();
		void character_hit_state();
		void ground_hit_state();
		void destroyed_state();

		void reset_cooldown();
		void increase_hit_count();

		unsigned int m_id_;
		const Abstract::rigidBody* const m_rb;

		const int m_max_hit_count;
		int m_curr_hit_count;
		float m_hit_cooldown;

		int m_fire_count;

		bool m_bExploded;

		float m_pitch;

		Math::Vector2 m_previous_position;

	protected:
		friend class Object::Ground;
		const ObjectBase::character* const m_shooter;
		Network::ProjectileHitMsg m_hit_msg_;

		virtual void fire() = 0;
		virtual void flying() = 0;
		virtual void hit() = 0;
		virtual void destroyed() = 0;

		virtual void play_fire_sound() = 0;
		virtual void play_hit_sound() = 0;

		float get_pitch() const;
		virtual bool notify_character_hit();
		virtual void notify_ground_hit();
	};
}
#endif // PROJECTILECONTROLLER_HPP
