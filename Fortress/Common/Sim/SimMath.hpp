#pragma once
#ifndef SIM_MATH_HPP
#define SIM_MATH_HPP

// Headless simulation core shared by the server (authoritative) and tests.
// Must not include <windows.h>, pch.h or any GDI/scene header: SimTests
// compiles a translation unit that fails if Windows headers leak in.

#include <cmath>
#include <limits>

namespace Fortress::Sim
{
	// Fixed simulation step. Everything that used frame DeltaTime in the
	// client (integration, cooldowns, multi-shot interval) counts ticks.
	constexpr int ticks_per_second = 60;
	constexpr float tick_seconds = 1.0f / static_cast<float>(ticks_per_second);

	constexpr int seconds_to_ticks(const float seconds)
	{
		return static_cast<int>(seconds * ticks_per_second + 0.5f);
	}

	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;

		constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
		constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
		constexpr Vec2 operator-() const { return {-x, -y}; }
		constexpr Vec2 operator*(const float s) const { return {x * s, y * s}; }
		// Component-wise, like Math::Vector2::operator*(Vector2).
		constexpr Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
		Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
		constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

		float magnitude() const { return std::sqrt(x * x + y * y); }

		Vec2 normalized() const
		{
			const float m = magnitude();
			return m == 0.0f ? Vec2{} : Vec2{x / m, y / m};
		}

		// Same formula as Math::Vector2::rotate.
		Vec2 rotate(const float radian) const
		{
			return {x * std::cos(radian) - y * std::sin(radian), x * std::sin(radian) + y * std::cos(radian)};
		}
	};

	// Math::vector_inf: "nothing found" result of the ground surface queries.
	constexpr Vec2 vector_inf = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};

	// Axis-aligned box described like Abstract::object: position is the
	// centre, size is the full hitbox.
	struct Box
	{
		Vec2 center;
		Vec2 size;

		Vec2 top_left() const { return center - size * 0.5f; }
		Vec2 bottom_right() const { return center + size * 0.5f; }

		bool overlaps(const Box& o) const
		{
			return std::fabs(center.x - o.center.x) * 2.0f < size.x + o.size.x &&
				std::fabs(center.y - o.center.y) * 2.0f < size.y + o.size.y;
		}

		// Centre, the four edge midpoints and the four corners.
		void hit_points(Vec2 (&out)[9]) const
		{
			const Vec2 h = size * 0.5f;
			out[0] = center;
			out[1] = center + Vec2{0.0f, -h.y};
			out[2] = center + Vec2{0.0f, h.y};
			out[3] = center + Vec2{-h.x, 0.0f};
			out[4] = center + Vec2{h.x, 0.0f};
			out[5] = center + Vec2{-h.x, -h.y};
			out[6] = center + Vec2{h.x, -h.y};
			out[7] = center + Vec2{-h.x, h.y};
			out[8] = center + Vec2{h.x, h.y};
		}

		// Like object::get_nearest_point: the closest of the eight boundary
		// points (edge midpoints and corners), not the true closest point.
		Vec2 nearest_boundary_point(const Vec2& p) const
		{
			Vec2 points[9];
			hit_points(points);

			Vec2 best = points[1];
			float best_distance = (p - best).magnitude();

			for (int i = 2; i < 9; ++i)
			{
				const float d = (p - points[i]).magnitude();
				if (d < best_distance)
				{
					best_distance = d;
					best = points[i];
				}
			}

			return best;
		}
	};
}

#endif // SIM_MATH_HPP
