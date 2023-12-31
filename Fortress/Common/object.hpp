#ifndef OBJECT_HPP
#define OBJECT_HPP
#pragma once

#include <memory>

#include "vector2.hpp"
#include "entity.hpp"

namespace Fortress::Abstract
{
	class object : public entity
	{
	public:
		Math::Vector2 m_hitbox;
		Math::Vector2 m_position;

		object() = delete;
		object& operator=(const object& other) = default;
		object& operator=(object&& other) = default;
		object(const object& other) = default;
		object(object&& other) = default;
		~object() override = default;

		__forceinline virtual void render();
		__forceinline virtual void prerender();
		virtual void update();
		__forceinline virtual void move_left();
		__forceinline virtual void move_right();
		__forceinline virtual void move_down();
		__forceinline virtual void move_up();
		__forceinline float get_x() const;
		__forceinline float get_y() const;
		__forceinline Math::Vector2 get_position() const;
		__forceinline virtual Math::Vector2 operator+(const Math::Vector2& vector) const;
		__forceinline virtual object& operator+=(const Math::Vector2& vector);

		bool is_active() const;
		virtual void set_disabled();
		virtual void set_enabled();

		__forceinline Math::Vector2 get_top() const;
		__forceinline Math::Vector2 get_bottom() const;
		__forceinline Math::Vector2 get_left() const;
		__forceinline Math::Vector2 get_right() const;
		__forceinline Math::Vector2 get_top_left() const;
		__forceinline Math::Vector2 get_top_right() const;
		__forceinline Math::Vector2 get_bottom_left() const;
		__forceinline Math::Vector2 get_bottom_right() const;
		__forceinline Math::Vector2 get_center() const;
		std::vector<Math::Vector2> get_all_hit_points() const;
		__forceinline Math::Vector2 get_dir_point(const eDirVector& e_vector) const;
		__forceinline Math::Vector2 to_top_left_local_position(const Math::Vector2& other) const;
		__forceinline Math::Vector2 to_top_right_local_position(const Math::Vector2& other) const;
		__forceinline Math::Vector2 to_nearest_local_position(const Math::Vector2& other) const;
		__forceinline Math::Vector2 get_nearest_point(const Math::Vector2& other) const;

		__forceinline float get_mass() const;
	protected:
		object(
			const std::wstring& name, 
			const Math::Vector2& position, 
			const Math::Vector2& hitbox, 
			const float mass);

		__forceinline virtual void initialize();

	private:
		float m_mass;
		bool m_bActive;
		};
}

namespace Fortress::Abstract
{
	void object::initialize()
	{
	}

	void object::render()
	{
	}

	inline void object::prerender()
	{
	}

	inline void object::update()
	{
	}

	void object::move_left()
	{
		m_position = m_position.left();
	}

	void object::move_right()
	{
		m_position = m_position.right();
	}

	void object::move_down()
	{
		m_position = m_position.top();
	}

	void object::move_up()
	{
		m_position = m_position.bottom();
	}

	float object::get_x() const
	{
		return m_position.get_x();
	}

	float object::get_y() const
	{
		return m_position.get_y();
	}

	Math::Vector2 object::get_position() const
	{
		return m_position;
	}

	Math::Vector2 object::operator+(const Math::Vector2& vector) const
	{
		return {m_position + vector};
	}

	object& object::operator+=(const Math::Vector2& vector)
	{
		m_position += vector;
		return *this;
	}

	inline object::object(
		const std::wstring& name, 
		const Math::Vector2& position, 
		const Math::Vector2& hitbox,
		const float mass): entity(name), m_hitbox(hitbox), m_position(position), m_mass(mass), m_bActive(false)
	{
	}

	inline bool object::is_active() const
	{
		return m_bActive;
	}

	inline void object::set_disabled()
	{
		m_bActive = false;
	}

	inline void object::set_enabled()
	{
		m_bActive = true;
	}

	inline Math::Vector2 object::get_top() const
	{
		return m_position - Math::Vector2{0, m_hitbox.get_y() / 2};
	}

	inline Math::Vector2 object::get_bottom() const
	{
		return m_position + Math::Vector2{0, m_hitbox.get_y() / 2};
	}

	inline Math::Vector2 object::get_left() const
	{
		return m_position - Math::Vector2{m_hitbox.get_x() / 2, 0};
	}

	inline Math::Vector2 object::get_right() const
	{
		return m_position + Math::Vector2{m_hitbox.get_x() / 2, 0};
	}

	inline Math::Vector2 object::get_top_left() const
	{
		return m_position - m_hitbox / 2;
	}

	inline Math::Vector2 object::get_top_right() const
	{
		return m_position + Math::Vector2{m_hitbox.get_x() / 2, -m_hitbox.get_y() / 2};
	}

	inline Math::Vector2 object::get_bottom_left() const
	{
		return m_position + Math::Vector2{-m_hitbox.get_x() / 2, m_hitbox.get_y() / 2};
	}

	inline Math::Vector2 object::get_bottom_right() const
	{
		return m_position + Math::Vector2{m_hitbox.get_x() / 2, m_hitbox.get_y() / 2};
	}

	inline Math::Vector2 object::get_center() const
	{
		return m_position;
	}

	inline std::vector<Math::Vector2> object::get_all_hit_points() const
	{
		return {
			get_center(), get_top(), get_bottom(), get_left(), get_right(), get_top_left(), get_top_right(),
			get_bottom_left(), get_bottom_left(), get_bottom_right()
		};
	}

	inline Math::Vector2 object::get_dir_point(const eDirVector & e_vector) const
	{
		switch (e_vector) {
		case eDirVector::Identical: return get_center();
		case eDirVector::Top: return get_top();
		case eDirVector::Bottom: return get_bottom();
		case eDirVector::Left: return get_left();
		case eDirVector::Right: return get_right();
		case eDirVector::TopLeft: return get_top_left();
		case eDirVector::TopRight: return get_top_right();
		case eDirVector::BottomLeft: return get_bottom_left();
		case eDirVector::BottomRight: return get_bottom_right();
		case eDirVector::Unknown: 
		default: return {0.0f, 0.0f};  // NOLINT(clang-diagnostic-covered-switch-default)
		}
	}

	inline Math::Vector2 object::to_top_left_local_position(const Math::Vector2& other) const
	{
		return other - get_top_left();
	}

	inline Math::Vector2 object::to_top_right_local_position(const Math::Vector2& other) const
	{
		return get_top_right() - other;
	}

	inline Math::Vector2 object::to_nearest_local_position(const Math::Vector2& other) const
	{
		return other - get_nearest_point(other);
	}

	inline Math::Vector2 object::get_nearest_point(const Math::Vector2& other) const
	{
		std::vector<std::pair<float, Math::Vector2>> distances = 
		{
			std::make_pair((other - get_top()).magnitude(), get_top()),
			std::make_pair((other - get_bottom()).magnitude(), get_bottom()),
			std::make_pair((other - get_left()).magnitude(), get_left()),
			std::make_pair((other - get_right()).magnitude(), get_right()),
			std::make_pair((other - get_top_left()).magnitude(), get_top_left()),
			std::make_pair((other - get_top_right()).magnitude(), get_top_right()),
			std::make_pair((other - get_bottom_left()).magnitude(), get_bottom_left()),
			std::make_pair((other - get_bottom_right()).magnitude(), get_bottom_right()),
		};

		std::sort(distances.begin(), distances.end(), [](
			const std::pair<float, Math::Vector2>& left,
			const std::pair<float, Math::Vector2>& right)
			{
				return left.first < right.first;
			});

		return (*distances.begin()).second;
	}

	inline float object::get_mass() const
	{
		return m_mass;
	}
}
#endif // OBJECT_HPP
