#pragma once
#include "NutshellProjectile.hpp"
#include "../Common/character.hpp"
#include "../Common/item.hpp"
#include "../Common/input.hpp"

namespace Fortress::Network::Client::Object
{
	class ClientCharacter : public ObjectBase::character
	{
	public:
		ClientCharacter() = delete;

		void update() override;

	private:
		void send_firing() const;
		void request_shot() override;
		void send_move_input();

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
		uint8_t m_sent_move_input = 0;
		float m_move_input_timer = 0.0f;

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
			get_offset().get_x(),
			get_aim_degree());

		// Items are reusable (test cheat): only disarm it for this shot.
		if (item)
		{
			item->reset();
		}
	}

	// The server walks the character from the held key (A before D, as in
	// CharacterController::idle_state); sent on change and repeated.
	inline void ClientCharacter::send_move_input()
	{
		constexpr float repeat_interval = 0.1f;

		// A/D only walk from Idle or Move (not while charging or firing).
		const auto state = get_state();
		const bool can_walk = state == eCharacterState::Idle || state == eCharacterState::IdleLow ||
			state == eCharacterState::Move || state == eCharacterState::MoveLow;

		uint8_t input = 0;
		if (is_movable_localplayer() && can_walk)
		{
			if (Input::getKey(eKeyCode::A))
			{
				input = 1;
			}
			else if (Input::getKey(eKeyCode::D))
			{
				input = 2;
			}
		}

		m_move_input_timer += DeltaTime::get_deltaTime();
		const bool changed = input != m_sent_move_input;
		const bool repeat = is_movable_localplayer() && m_move_input_timer >= repeat_interval;

		if (changed || repeat)
		{
			EngineHandle::get_messenger()->send_move_input(input);
			m_sent_move_input = input;
			m_move_input_timer = 0.0f;
		}
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

		send_move_input();

		if (is_movable_localplayer() && has_state_changed())
		{
			if (m_current_state_ == eCharacterState::Firing)
			{
				send_firing();
			}
			else if (m_current_state_ == eCharacterState::PreItem)
			{
				// Others show the item animation; the shot request carries the item.
				if (const auto item = m_active_item.lock())
				{
					EngineHandle::get_messenger()->send_item_signal(
						get_position(), get_offset(), m_active_item_index, item->get_item_type());
				}
			}
		}
	}

	inline void ClientCharacter::update_non_local_player()
	{
		if(!is_localplayer())
		{
			FiringMsg firing_msg{};
			ItemMsg item_msg{};
			ProjectileSelectMsg projectile_msg{};

			if (EngineHandle::get_messenger()->get_projectile_select_signal(get_player_id(), &projectile_msg))
			{
				m_projectile_type = projectile_msg.prj_type;
			}
			if (EngineHandle::get_messenger()->get_item_signal(get_player_id(), &item_msg))
			{
				set_item_active(item_msg.index);
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
