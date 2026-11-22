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
