#pragma once
#ifndef SIM_GROUND_HPP
#define SIM_GROUND_HPP

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "SimMath.hpp"

namespace Fortress::Sim
{
	// Object::GroundState. Outline is never produced; kept so the values
	// line up with the client enum.
	enum class GroundState : uint8_t
	{
		NotDestroyed = 0,
		Outline,
		Destroyed,
		OutOfBound,
	};

	// Destructible terrain as one byte per pixel (1 = destroyed). Replaces the
	// per-pixel std::map of Object::Ground; the GDI mask stays client-side.
	class SimGround
	{
	public:
		SimGround(const Vec2& center, const int width, const int height) :
			m_center(center),
			m_width(width),
			m_height(height),
			m_destroyed(static_cast<size_t>(width) * static_cast<size_t>(height), 0)
		{
		}

		// Same shape as Object::Stairway: the top quarter of one side is cut
		// away in a 45-degree ramp. ramp_right = Math::right.
		static SimGround stairway(const Vec2& center, const int width, const int height, const bool ramp_right)
		{
			SimGround ground(center, width, height);
			int run = width / 4;

			for (int y = 0; y < height && run > 0; ++y, --run)
			{
				for (int i = 0; i < run; ++i)
				{
					ground.set_destroyed(ramp_right ? i : width - 1 - i, y);
				}
			}

			return ground;
		}

		// Uncompressed BMP mask (16/24/32 bpp, BI_RGB or BI_BITFIELDS) where
		// pure black pixels are holes, as CloudGround reads cloud-mask.bmp.
		// Returns false if the file is missing or unsupported.
		static bool from_mask_bmp(const std::string& path, const Vec2& center, SimGround& out)
		{
			std::ifstream stream(path, std::ios::binary);
			const std::vector<uint8_t> file(
				(std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

			const auto u16 = [&](const size_t at) { return static_cast<uint32_t>(file[at] | file[at + 1] << 8); };
			const auto u32 = [&](const size_t at) { return u16(at) | u16(at + 2) << 16; };

			if (file.size() < 54 || file[0] != 'B' || file[1] != 'M')
			{
				return false;
			}

			const uint32_t pixel_offset = u32(10);
			const int width = static_cast<int>(u32(18));
			const int raw_height = static_cast<int>(u32(22));
			const uint32_t bpp = u16(28);
			const uint32_t compression = u32(30);
			const int height = raw_height < 0 ? -raw_height : raw_height;

			if (width <= 0 || height <= 0 || (bpp != 16 && bpp != 24 && bpp != 32) ||
				(compression != 0 && compression != 3))
			{
				return false;
			}

			const size_t stride = (static_cast<size_t>(width) * bpp / 8 + 3) & ~static_cast<size_t>(3);

			if (pixel_offset + stride * height > file.size())
			{
				return false;
			}

			out = SimGround(center, width, height);

			for (int y = 0; y < height; ++y)
			{
				const int row = raw_height > 0 ? height - 1 - y : y;  // positive height = bottom-up
				const size_t base = pixel_offset + stride * row;

				for (int x = 0; x < width; ++x)
				{
					const size_t at = base + static_cast<size_t>(x) * (bpp / 8);
					const uint32_t value = bpp == 16 ? u16(at) : bpp == 24 ? (u16(at) | file[at + 2] << 16) : u32(at) & 0xffffff;

					if (value == 0)
					{
						out.set_destroyed(x, y);
					}
				}
			}

			return true;
		}

		int width() const { return m_width; }
		int height() const { return m_height; }
		const Vec2& center() const { return m_center; }
		Box box() const { return {m_center, {static_cast<float>(m_width), static_cast<float>(m_height)}}; }
		Vec2 top_left() const { return box().top_left(); }

		bool in_bounds(const int x, const int y) const
		{
			return x >= 0 && x < m_width && y >= 0 && y < m_height;
		}

		bool is_destroyed(const int x, const int y) const
		{
			return m_destroyed[index(x, y)] != 0;
		}

		// True if the global position is an intact terrain pixel.
		bool is_solid_global(const Vec2& global) const
		{
			const Vec2 local = global - top_left();
			const int x = static_cast<int>(std::floor(local.x));
			const int y = static_cast<int>(std::floor(local.y));
			return in_bounds(x, y) && !is_destroyed(x, y);
		}

		void set_destroyed(const int x, const int y)
		{
			if (in_bounds(x, y))
			{
				m_destroyed[index(x, y)] = 1;
			}
		}

		// Destroys every pixel whose centre lies inside the circle
		// (dx^2 + dy^2 <= r^2). Replaces the sin-based column fill.
		void destroy_circle(const Vec2& global_center, const float radius)
		{
			const Vec2 c = global_center - top_left();
			const int x0 = static_cast<int>(std::floor(c.x - radius));
			const int x1 = static_cast<int>(std::ceil(c.x + radius));
			const int y0 = static_cast<int>(std::floor(c.y - radius));
			const int y1 = static_cast<int>(std::ceil(c.y + radius));
			const float r2 = radius * radius;

			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					const float dx = static_cast<float>(x) + 0.5f - c.x;
					const float dy = static_cast<float>(y) + 0.5f - c.y;

					if (dx * dx + dy * dy <= r2)
					{
						set_destroyed(x, y);
					}
				}
			}
		}

		// ---- Ports of the Object::Ground queries used by character movement
		// (Common/ground.hpp). Same names and semantics, including the
		// static_cast<int> truncation toward zero: a local coordinate in
		// (-1, 0) maps to pixel 0 and counts as inside the ground.

		// object::to_top_left_local_position
		Vec2 to_top_left_local_position(const Vec2& global) const
		{
			return global - top_left();
		}

		// Ground::safe_is_destroyed
		GroundState safe_is_destroyed(const Vec2& local) const
		{
			const int x = static_cast<int>(local.x);
			const int y = static_cast<int>(local.y);

			if (in_bounds(x, y))
			{
				return is_destroyed(x, y) ? GroundState::Destroyed : GroundState::NotDestroyed;
			}

			return GroundState::OutOfBound;
		}

		// Ground::safe_is_object_stuck_global: all four 1-px neighbours intact.
		bool safe_is_object_stuck_global(const Vec2& global) const
		{
			const Vec2 local = to_top_left_local_position(global);
			const Vec2 offsets[4] = {{-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}};

			int count = 0;
			for (const auto& offset : offsets)
			{
				count += safe_is_destroyed(local + offset) == GroundState::NotDestroyed;
			}

			return count == 4;
		}

		// Ground::safe_nearest_surface: walks up 1 px at a time to the first
		// destroyed pixel and returns (start - found), i.e. how far to move
		// up. If none is found before local y <= 0 it returns {0, -y} of the
		// last probe, which is only the fractional part of the depth.
		Vec2 safe_nearest_surface(const Vec2& global) const
		{
			const Vec2 origin = to_top_left_local_position(global);
			Vec2 local = origin;

			if (local.y <= 0.0f)
			{
				return {0.0f, -local.y};
			}

			while (local.y > 0.0f)
			{
				if (safe_is_destroyed(local) == GroundState::Destroyed)
				{
					return origin - local;
				}

				local = {local.x, local.y - 1.0f};
			}

			return {0.0f, -local.y};
		}

		// Ground::safe_orthogonal_surface_global (+ _local): first intact
		// pixel at or below the position in its column, searching `depth`
		// rows (-1 = to the bottom of the ground). start_y != -1 replaces
		// the local y. Returns the offset to it, or vector_inf.
		Vec2 safe_orthogonal_surface_global(const Vec2& global, const int depth = -1, const int start_y = -1) const
		{
			Vec2 local = to_top_left_local_position(global);

			if (start_y != -1)
			{
				local = {local.x, static_cast<float>(start_y)};
			}

			const int first_y = static_cast<int>(local.y);
			const int end_y = depth == -1 ? m_height : first_y + depth;

			for (int y = first_y; y < end_y; ++y)
			{
				const Vec2 probe = {local.x, static_cast<float>(y)};

				if (safe_is_destroyed(probe) == GroundState::NotDestroyed)
				{
					return probe - local;
				}
			}

			return vector_inf;
		}

		// Ground::safe_parallel_surface_global: first intact pixel on the
		// same row, stepping toward `left` (Math::left) or right, at most the
		// ground width. Returns the offset to it, or vector_inf.
		Vec2 safe_parallel_surface_global(const Vec2& global, const bool left) const
		{
			const Vec2 local = to_top_left_local_position(global);
			const int step = left ? -1 : 1;

			for (int i = 0; i > -m_width && i < m_width; i += step)
			{
				const Vec2 probe = {local.x + static_cast<float>(i), local.y};

				if (safe_is_destroyed(probe) == GroundState::NotDestroyed)
				{
					return probe - local;
				}
			}

			return vector_inf;
		}

		size_t destroyed_count() const
		{
			size_t n = 0;
			for (const uint8_t v : m_destroyed)
			{
				n += v;
			}
			return n;
		}

		// FNV-1a over size and pixels; used to compare terrain between the
		// server and clients.
		uint32_t checksum() const
		{
			uint32_t h = 2166136261u;
			const auto mix = [&h](const uint32_t v) { h = (h ^ v) * 16777619u; };
			mix(static_cast<uint32_t>(m_width));
			mix(static_cast<uint32_t>(m_height));
			for (const uint8_t v : m_destroyed)
			{
				mix(v);
			}
			return h;
		}

	private:
		size_t index(const int x, const int y) const
		{
			return static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);
		}

		Vec2 m_center;
		int m_width;
		int m_height;
		std::vector<uint8_t> m_destroyed;
	};
}

#endif // SIM_GROUND_HPP
