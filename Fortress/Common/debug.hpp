#pragma once
#ifndef DEBUG_HPP
#define DEBUG_HPP
#include "../Common/common.h"
#include "../Common/input.hpp"
#include "../Common/EngineHandle.h"

namespace Fortress
{
	class Debug final
	{
	public:
		static void initialize(HDC hdc)
		{
			m_hdc = hdc;
		}

		static void Log(const std::wstring& str)
		{
			push([str]()
			{
				TextOut(m_hdc, x, y, str.c_str(), str.length());
				y += y_movement;
				y %= EngineHandle::get_handle().lock()->get_actual_max_y();
			});
		}

		static void set_debug_flag();
		static bool get_debug_flag();
		static void push(std::function<void()> func);

		static void draw_line(const Math::Vector2 left, const Math::Vector2 right);
		static void draw_dot(const Math::Vector2 point);
		static void draw_circle(Math::Vector2 point, float radius);
		static void draw_rect(const Math::Vector2 point, const Math::Vector2 size, const COLORREF color);

		static void render()
		{
			if(Input::getKeyDown(eKeyCode::ScrollLock))
			{
				m_bDebug = !m_bDebug;
			}

			if(!m_bDebug)
			{
				return;
			}

			while (!m_render_queue.empty())
			{
				m_render_queue.front()();
				m_render_queue.pop();
			}

			y = y_initial;
		}

	private:
		inline static bool m_bDebug = true;
		static constexpr int y_movement = 15;
		static constexpr int y_initial = 30;
		inline static int x = 100;
		inline static int y = y_initial;
		inline static HDC m_hdc;
		inline static std::queue<std::function<void()>> m_render_queue;
	};

	inline void Debug::set_debug_flag()
	{
		m_bDebug = true;
	}

	inline bool Debug::get_debug_flag()
	{
		return m_bDebug;
	}

	inline void Debug::push(std::function<void()> func)
	{
		if(!m_bDebug)
		{
			return;
		}

		m_render_queue.push(std::move(func));
	}

	inline void Debug::draw_line(const Math::Vector2 left, const Math::Vector2 right)
	{
		push([left, right]()
		{
			MoveToEx(m_hdc, left.get_x(), left.get_y(), nullptr);
			LineTo(m_hdc, right.get_x(), right.get_y());
		});
	}

	inline void Debug::draw_dot(const Math::Vector2 point)
	{
		push([point]()
		{
			Ellipse(m_hdc, point.get_x(), point.get_y(), point.get_x() + 5, point.get_y() + 5);
		});
	}

	inline void Debug::draw_circle(const Math::Vector2 point, const float radius)
	{
		push([point, radius]()
		{
			Ellipse(m_hdc, point.get_x(), point.get_y(), point.get_x() + radius, point.get_y() + radius);
		});
	}

	inline void Debug::draw_rect(const Math::Vector2 point, const Math::Vector2 size, const COLORREF color)
	{
		push([point, size, color]()
		{
			const HBRUSH transparent = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
			const HBRUSH previous = static_cast<HBRUSH>(SelectObject(m_hdc, transparent));
			const HPEN outline = static_cast<HPEN>(CreatePen(PS_SOLID, 1, color));
			const HPEN previousPen = static_cast<HPEN>(SelectObject(m_hdc, outline));

			Rectangle(
				m_hdc, 
				point.get_x(),
				point.get_y(),
				point.get_x() + size.get_x(),
				point.get_y() + size.get_y());

			SelectObject(m_hdc, previous);
			SelectObject(m_hdc, previousPen);
			DeleteObject(transparent);
			DeleteObject(outline);
		});
	}
}

#endif
