#pragma once
#include "NutshellProjectile.hpp"
#include "../Common/character.hpp"
#include "../Common/item.hpp"

namespace Fortress::Network::Client::Object
{
	class ClientCharacter : public ObjectBase::character
	{
	public:
		ClientCharacter() = delete;

		void update() override;

	private:
		void send_move() const;
		void send_stop() const;
		void send_firing() const;
		void request_shot() override;
		void send_position_report();

		void change_projectile() override;
		void equip_nutshell() override;
		void unequip_nutshell() override;

		void update_local_player();
		void update_non_local_player();

		bool has_state_changed() const;


	protected:
		ClientCharacter(const Network::PlayerID& player_id, const std::wstring& name, const std::wstring& short_name,
			const UnitVector& offset, const GlobalPosition& position, const UnitVector& velocity, float mass,
			const SpeedVector& speed, const AccelVector& acceleration, int hp, int mp, float armor)
			: character(
				  player_id, name, short_name, offset, position, velocity, mass, speed, acceleration, hp, mp, armor),
			  m_previous_state_(eCharacterState::Idle),
			  m_current_state_(eCharacterState::Idle)
		{
		}

		eCharacterState m_previous_state_;
		eCharacterState m_current_state_;
		float m_position_report_timer = 0.0f;
		bool m_death_reported = false;

		std::weak_ptr<ObjectBase::projectile> get_nutshell_projectile() override;
	};

	inline void ClientCharacter::update()
	{
		m_previous_state_ = get_state();
		character::update();
		m_current_state_ = get_state();

		update_local_player();
		update_non_local_player();
	}

	inline void ClientCharacter::send_move() const
	{
		EngineHandle::get_messenger()->send_move_signal(get_position(), get_offset());
	}

	inline void ClientCharacter::send_stop() const
	{
		EngineHandle::get_messenger()->send_stop_signal(get_position(), get_offset());
	}

	inline void ClientCharacter::send_firing() const
	{
		EngineHandle::get_messenger()->send_firing_signal(get_position(), get_offset());
	}

	inline void ClientCharacter::request_shot()
	{
		if (!is_movable_localplayer())
		{
			return;
		}

		const auto item = m_active_item.lock();
		EngineHandle::get_messenger()->send_shot_request(
			get_projectile_type(),
			item ? item->get_item_type() : eItemType::None,
			get_charged_power(),
			get_position(),
			get_offset().get_x(),
			get_movement_pitch_radian());

		// Items are reusable (test cheat): only disarm it for this shot.
		if (item)
		{
			item->reset();
		}
	}

	// Until the server simulates movement it takes the position from the
	// owning client (bounded on the server side).
	inline void ClientCharacter::send_position_report()
	{
		constexpr float report_interval = 0.25f;

		m_position_report_timer += DeltaTime::get_deltaTime();
		if (m_position_report_timer < report_interval)
		{
			return;
		}

		m_position_report_timer = 0.0f;
		EngineHandle::get_messenger()->send_position_report(
			get_position(), get_offset().get_x(), get_movement_pitch_radian());
	}

	inline void ClientCharacter::change_projectile()
	{
		character::change_projectile();

		if (is_movable_localplayer())
		{
			EngineHandle::get_messenger()->send_projectile_select_signal(get_projectile_type());
		}
	}

	inline void ClientCharacter::equip_nutshell()
	{
		character::equip_nutshell();

		if (is_movable_localplayer())
		{
			EngineHandle::get_messenger()->send_projectile_select_signal(get_projectile_type());
		}
	}

	inline void ClientCharacter::unequip_nutshell()
	{
		character::unequip_nutshell();

		if (is_movable_localplayer())
		{
			EngineHandle::get_messenger()->send_projectile_select_signal(get_projectile_type());
		}
	}

	inline void ClientCharacter::update_local_player()
	{
		if (!is_localplayer())
		{
			return;
		}

		const bool dead = m_current_state_ == eCharacterState::Death || m_current_state_ == eCharacterState::Dead;
		if (dead)
		{
			// The server only learns a fall from the owner (hp deaths it knows).
			if (!m_death_reported)
			{
				EngineHandle::get_messenger()->send_own_death();
				m_death_reported = true;
			}
			return;
		}

		send_position_report();

		if (is_movable_localplayer() && has_state_changed())
		{
			switch (m_current_state_)
			{
			case eCharacterState::Idle:
			case eCharacterState::IdleLow:
				if(m_previous_state_ != eCharacterState::TurnEnd)
				{
					send_stop();	
				}
				break;
			case eCharacterState::Move:
			case eCharacterState::MoveLow:
				send_move();
				break;
			case eCharacterState::Firing:
				send_firing();
				break;
			default: ;
			}
		}
	}

	inline void ClientCharacter::update_non_local_player()
	{
		if(!is_localplayer())
		{
			PositionMsg position_msg{};
			FiringMsg firing_msg{};
			StopMsg stop_msg{};
			ProjectileSelectMsg projectile_msg{};

			if (EngineHandle::get_messenger()->get_move_signal(get_player_id(), &position_msg))
			{
				set_offset(position_msg.offset);
				set_state(eCharacterState::Move);

				if (get_offset() == Math::left)
				{
					move_left();
				}
				else if (get_offset() == Math::right)
				{
					move_right();
				}
			}
			if (EngineHandle::get_messenger()->get_stop_signal(get_player_id(), &stop_msg))
			{
				m_position = stop_msg.position;
				set_offset(stop_msg.offset);
				set_state(eCharacterState::Idle);
				stop();
			}
			if (EngineHandle::get_messenger()->get_projectile_select_signal(get_player_id(), &projectile_msg))
			{
				m_projectile_type = projectile_msg.prj_type;
			}
			if (EngineHandle::get_messenger()->get_firing_signal(get_player_id(), &firing_msg))
			{
				set_offset(firing_msg.offset);
				set_state(eCharacterState::Firing);
			}
		}
	}

	inline bool ClientCharacter::has_state_changed() const
	{
		return m_previous_state_ != m_current_state_;
	}

	inline std::weak_ptr<ObjectBase::projectile> ClientCharacter::get_nutshell_projectile()
	{
		return ObjectBase::ObjectManager::create_object<Fortress::Object::NutShellProjectile>(0, this).lock();
	}
}
