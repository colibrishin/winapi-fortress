#include "pch.h"
#include "character.hpp"

#include "ground.hpp"
#include "projectile.hpp"
#include "ProjectileController.hpp"
#include "CharacterController.hpp"
#include "debug.hpp"

#undef max
#undef min

namespace Fortress::ObjectBase
{
	character::~character()
	{
		TimerManager::remove(m_multi_projectile_timer);
	}

	void character::initialize()
	{
		rigidBody::initialize();
		CharacterController::initialize();
		m_multi_projectile_timer = TimerManager::create<ProjectileTimer>(
			&character::initialize_projectile, this);
	}

	void character::update()
	{
		rigidBody::update();
		CharacterController::update();
	}

	void character::prerender()
	{
		rigidBody::prerender();
		CharacterController::prerender();
	}

	void character::hit(const std::weak_ptr<projectile>& prj, const GlobalPosition& hit_point)
	{
	}

	void character::fire()
	{
		m_multi_projectile_timer.lock()->reset();

		float charged = get_charged_power();

		if(charged <= 0.0f)
		{
			charged = Object::Property::Table::character_min_charge;
		}

		Math::Vector2 angle{};

		if(get_offset() == Math::left)
		{
			angle = {-cosf(get_movement_pitch_radian()), -sinf(get_movement_pitch_radian())};
		}
		else
		{
			angle = {cosf(get_movement_pitch_radian()), sinf(get_movement_pitch_radian())};
		}

		const std::weak_ptr<projectile> instantiated = initialize_projectile(0, angle, charged);

		const int remaining = instantiated.lock()->get_fire_count() - 1;

		if(remaining > 0)
		{
			m_multi_projectile_timer.lock()->set_count(remaining);
			m_multi_projectile_timer.lock()->set_angle(angle);
			m_multi_projectile_timer.lock()->set_charged(charged);
			m_multi_projectile_timer.lock()->toggle();
		}

		CharacterController::fire();
	}

	ProjectilePointer character::spawn_replay_projectile(
		const eProjectileType kind, const unsigned int id, const GlobalPosition& position)
	{
		std::weak_ptr<projectile> instantiated;

		switch (kind)
		{
		case eProjectileType::Sub:
			instantiated = get_sub_projectile(id);
			break;
		case eProjectileType::Nutshell:
			instantiated = get_nutshell_projectile();
			break;
		default:
			instantiated = get_main_projectile(id);
			break;
		}

		const auto prj = instantiated.lock();
		if (!prj)
		{
			return {};
		}

		prj->start_replay(position);

		if (const auto scene = Scene::SceneManager::get_active_scene().lock())
		{
			scene->add_game_object(Abstract::LayerType::Projectile, prj);
		}

		add_active_projectile(prj);
		return instantiated;
	}

	void character::teleport_to(const GlobalPosition& position)
	{
		m_position = position;
		m_velocity = {};
	}

	void character::apply_server_state(
		const GlobalPosition& position, const float facing, const float pitch, const float mp, const bool moving)
	{
		constexpr float local_correction_distance = 8.0f;

		const auto state = get_state();
		if (state == eCharacterState::Death || state == eCharacterState::Dead)
		{
			return;
		}

		if (is_localplayer())
		{
			// While walking, the local mp is the prediction; the server's lags.
			if (!is_movable_localplayer())
			{
				set_server_mp(mp);
			}

			const float deviation = (position - m_position).magnitude();
			m_max_server_deviation = std::max(m_max_server_deviation, deviation);

			if (deviation > local_correction_distance)
			{
				m_position = position;
				m_server_corrections++;
			}
			return;
		}

		set_server_mp(mp);
		m_position = position;
		reset_current_gravity_speed();
		set_offset(facing < 0.0f ? Math::left : Math::right);
		set_movement_pitch_radian(pitch);

		if (state == eCharacterState::Idle || state == eCharacterState::IdleLow ||
			state == eCharacterState::Move || state == eCharacterState::MoveLow)
		{
			// Only the animation; the server moves the character.
			m_velocity = {};
			set_state(moving ? eCharacterState::Move : eCharacterState::Idle);
		}
	}

	float character::get_armor() const
	{
		return m_armor;
	}

	Network::PlayerID character::get_player_id() const
	{
		return m_player_id;
	}

	void character::render_hp_bar(const Math::Vector2& position)
	{
		// white box
		Rectangle(
			EngineHandle::get_handle().lock()->get_buffer_dc(),
			position.get_x(),
			position.get_y() - 20,
			position.get_x() + 52,
			position.get_y() - 10);

		// inside hp bar
		const float hp_percentage = get_hp_percentage();
		HBRUSH brush;

		if (hp_percentage > 0.5f) 
		{
			brush = CreateSolidBrush(RGB(0, 255, 0));
		}
		else if (hp_percentage >= 0.3f)
		{
			brush = CreateSolidBrush(RGB(255, 255, 0));
		}
		else
		{
			brush = CreateSolidBrush(RGB(255, 0, 0));
		}

		const RECT rect = {
			static_cast<int>(position.get_x()),
			static_cast<int>(position.get_y() - 19),
			static_cast<int>(position.get_x() + (51 * get_hp_percentage())),
			static_cast<int>(position.get_y() - 12) };

		FillRect(EngineHandle::get_handle().lock()->get_buffer_dc(), &rect, brush);
		DeleteObject(brush);
	}

	// Aim line from the top of the tank in the firing direction (the same
	// direction as Sim::Detail::muzzle) and the angle in degrees.
	void character::render_aim(const Math::Vector2& position)
	{
		constexpr float length = 60.0f;
		const float pitch = get_movement_pitch_radian();
		const float aim = Math::to_radian(get_aim_degree());
		const bool left = get_offset() == Math::left;
		const Math::Vector2 direction = left
			? Math::Vector2{-std::cos(pitch + aim), -std::sin(pitch + aim)}
			: Math::Vector2{std::cos(pitch - aim), std::sin(pitch - aim)};

		const Math::Vector2 start = position + Math::Vector2{m_hitbox.get_x() / 2, 0.0f};
		const Math::Vector2 end = start + direction * length;

		const HDC dc = EngineHandle::get_handle().lock()->get_buffer_dc();
		const HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));
		const HGDIOBJ previous = SelectObject(dc, pen);
		MoveToEx(dc, static_cast<int>(start.get_x()), static_cast<int>(start.get_y()), nullptr);
		LineTo(dc, static_cast<int>(end.get_x()), static_cast<int>(end.get_y()));
		SelectObject(dc, previous);
		DeleteObject(pen);

		const std::wstring text = std::to_wstring(static_cast<int>(std::lround(get_aim_degree()))) + L"\u00b0";
		const int previous_mode = SetBkMode(dc, TRANSPARENT);
		const COLORREF previous_color = SetTextColor(dc, RGB(255, 255, 0));
		TextOutW(dc, static_cast<int>(end.get_x()) + (left ? -30 : 4), static_cast<int>(end.get_y()) - 8,
			text.c_str(), static_cast<int>(text.size()));
		SetTextColor(dc, previous_color);
		SetBkMode(dc, previous_mode);
	}

	void character::render()
	{
		Math::Vector2 pos{};

		if (is_active())
		{
			const auto camera_ptr = Scene::SceneManager::get_active_scene().lock()->get_camera().lock();

			if(camera_ptr->get_locked_object().lock() == 
				std::dynamic_pointer_cast<object>(rigidBody::shared_from_this()))
			{
				pos = camera_ptr->get_offset(m_hitbox);
			}
			else
			{
				pos = camera_ptr->get_relative_position(
					std::dynamic_pointer_cast<object>(rigidBody::shared_from_this()));
			}

			prerender();
			render_hp_bar(pos);
			
			m_current_sprite.lock()->render(pos, m_hitbox, {1, 1}, Math::to_degree(get_movement_pitch_radian()));

			if (is_movable_localplayer())
			{
				render_aim(pos);
			}

			// c
			Debug::draw_line(pos, camera_ptr->get_offset());

			// t
			Debug::draw_line(
				camera_ptr->get_offset(), 
				{camera_ptr->get_offset().get_x(), pos.get_y()});

			// s
			Debug::draw_line(
				{camera_ptr->get_offset().get_x(), pos.get_y()}, pos);
		}

		rigidBody::render();
	}

	const std::wstring& character::get_short_name() const
	{
		return m_short_name;
	}

	ProjectilePointer character::initialize_projectile(const unsigned int id, const Math::Vector2& angle, const float charged)
	{
		std::weak_ptr<projectile> instantiated;

		if(get_projectile_type() == eProjectileType::Main)
		{
			instantiated = get_main_projectile(id);
		}
		else if(get_projectile_type() == eProjectileType::Sub)
		{
			instantiated = get_sub_projectile(id);
		}
		else
		{
			instantiated = get_nutshell_projectile();
		}

		const auto projectile = instantiated.lock();

		// thinking as virtual circle which has an radius of projectile size + half of character hitbox,
		// with an top left/right toward to.
		const auto forward = Math::Vector2{get_offset().get_x(), -1} * (projectile->m_hitbox);
		const auto forward_rotation = forward.rotate(get_movement_pitch_radian());

		projectile->fire(
			get_offset_top_forward_position() + forward_rotation, 
			angle, 
			charged);

		if(const auto scene = Scene::SceneManager::get_active_scene().lock())
		{
			scene->add_game_object(Abstract::LayerType::Projectile, projectile);
		}

		add_active_projectile(projectile);

		return instantiated;
	}

	character::character(
			const Network::PlayerID& player_id,
			const std::wstring& name,
			const std::wstring& short_name,
			const Math::Vector2& offset,
			const Math::Vector2& position,
			const Math::Vector2& velocity,
			const float mass,
			const Math::Vector2& speed,
			const Math::Vector2& acceleration,
			const int hp,
			const int mp,
			const float armor):
			rigidBody(
				name, 
				position, 
				{50.0f, 50.0f}, 
				velocity,
				mass, 
				speed, 
				acceleration, 
				true),
			CharacterController(
				m_player_id,
				short_name, 
				hp, 
				mp,
				this),
			m_player_id(player_id),
			m_bGrounded(false),
			m_short_name(short_name),
			m_armor(armor)
		{
		}


	void character::move()
	{
		rigidBody::move();
		CharacterController::move();
	}

	void character::move_left()
	{
		rigidBody::move_left();
		CharacterController::move_left();
	}

	void character::move_right()
	{
		rigidBody::move_right();
		CharacterController::move_right();
	}

	void character::stop()
	{
		rigidBody::stop();
		CharacterController::stop();
	}
}
