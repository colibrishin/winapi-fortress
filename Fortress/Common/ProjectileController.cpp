#include "pch.h"
#include "ProjectileController.hpp"

#include "BattleScene.h"
#include "math.h"
#include "stateController.hpp"
#include "GifWrapper.h"

namespace Fortress::Controller
{
	ProjectileController::ProjectileController(
		const unsigned int id,
		const ObjectBase::character* shooter,
		const std::wstring& short_name, 
		const Abstract::rigidBody* const rb,
		const int max_hit_count,
		const int max_fire_count) :
		stateController(short_name, eProjectileState::Fire),
		m_id_(id),
		m_rb(rb),
		m_max_hit_count(max_hit_count),
		m_curr_hit_count(0),
		m_hit_cooldown(0.0f),
		m_fire_count(max_fire_count),
		m_bExploded(false),
		m_pitch(0.0f),
		m_previous_position(rb->get_position()),
		m_shooter(shooter)
	{
	}

	void ProjectileController::initialize()
	{
		stateController::initialize();
		set_current_sprite(eProjectileState::Fire);
		set_state(eProjectileState::Fire);
	}

	void ProjectileController::update()
	{
		m_hit_cooldown += DeltaTime::get_deltaTime();

		switch(get_state())
		{
		case eProjectileState::Fire: 
			fire_state();
			break;
		case eProjectileState::Flying: 
			flying_state();
			break;
		case eProjectileState::CharacterHit: 
			character_hit_state();
			break;
		case eProjectileState::GroundHit:
			ground_hit_state();
			break;
		case eProjectileState::Destroyed: 
			destroyed_state();
			break;
		default: ;
		}
	}

	void ProjectileController::prerender()
	{
		stateController::prerender();

		const auto diff_by_offset = get_moving_direction() == Math::left ?
			m_previous_position - m_rb->get_position() :
			m_rb->get_position() - m_previous_position;

		const auto unit = diff_by_offset.normalized();
		m_pitch = unit.unit_angle();

		switch(get_state())
		{
		case eProjectileState::Fire: 
		case eProjectileState::Flying: 
		case eProjectileState::CharacterHit:
		case eProjectileState::GroundHit:
		case eProjectileState::Destroyed:
			set_current_sprite(eProjectileState::Fire);
		default: ;
		}

		m_previous_position = m_rb->get_position();
	}

	bool ProjectileController::is_cooldown() const
	{
		if(m_curr_hit_count == 0)
		{
			return false;
		}

		return (m_hit_cooldown < 0.5f);
	}

	bool ProjectileController::is_exploded() const
	{
		return m_bExploded;
	}

	unsigned ProjectileController::get_id() const
	{
		return m_id_;
	}

	int ProjectileController::get_hit_count() const
	{
		return m_curr_hit_count;
	}

	int ProjectileController::get_fire_count() const
	{
		return m_fire_count;
	}

	int ProjectileController::get_max_hit_count() const
	{
		return m_max_hit_count;
	}

	const ObjectBase::character* ProjectileController::get_origin() const
	{
		return m_shooter;
	}

	void ProjectileController::fire_state()
	{
		fire();
		m_bExploded = false;
		m_current_sprite.lock()->play();
		play_fire_sound();

		set_state(eProjectileState::Flying);
	}

	void ProjectileController::flying_state()
	{
		flying();

		if (const auto map = Scene::SceneManager::get_active_map().lock())
		{
			if(map->predicate_OOB(m_rb->get_position()))
			{
				set_state(eProjectileState::Destroyed);
			}
		}
	}

	void ProjectileController::character_hit_state()
	{
		if(!is_cooldown())
		{
			increase_hit_count();
			reset_cooldown();
			play_hit_sound();
			hit();
		}

		if(m_max_hit_count == m_curr_hit_count)
		{
			set_state(eProjectileState::Destroyed);
			return;
		}

		set_state(eProjectileState::Flying);
	}

	void ProjectileController::ground_hit_state()
	{
		increase_hit_count();
		reset_cooldown();
		play_hit_sound();
		hit();

		if(m_max_hit_count == m_curr_hit_count)
		{
			set_state(eProjectileState::Destroyed);
			return;
		}

		set_state(eProjectileState::Flying);
	}

	void ProjectileController::destroyed_state()
	{
		destroyed();
		m_curr_hit_count = 0;
		m_pitch = 0;
		m_bExploded = true;
		reset_cooldown();
		m_current_sprite.lock()->reset_transform();
	}

	void ProjectileController::reset_cooldown()
	{
		m_hit_cooldown = 0.0f;
	}

	void ProjectileController::increase_hit_count()
	{
		m_curr_hit_count++;
	}

	float ProjectileController::get_pitch() const
	{
		return m_pitch;
	}

	bool ProjectileController::notify_character_hit()
	{
		if(!is_cooldown())
		{
			set_state(eProjectileState::CharacterHit);
			return true;
		}

		return false;
	}

	void ProjectileController::notify_ground_hit()
	{
		set_state(eProjectileState::GroundHit);
	}

	const Network::ProjectileHitMsg& ProjectileController::get_hit_msg() const
	{
		return m_hit_msg_;
	}

	void ProjectileController::set_current_sprite(const eProjectileState& state)
	{
		m_current_sprite = m_texture.get_image(
			L"projectile",
			get_moving_direction() == Math::left ? L"left" : L"right");
	}

	DirVector ProjectileController::get_moving_direction() const
	{
		const UnitVector moving_dir = (m_rb->get_position() - m_previous_position).normalized();
		return moving_dir.x_dir();
	}
}
