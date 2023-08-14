#pragma once
#ifndef GROUND_H
#define GROUND_H

#include <mutex>
#include <vector>
#include "rigidBody.hpp"
#include "projectile.hpp"
#include "scene.hpp"
#include "sceneManager.hpp"

namespace Fortress
{
	class Radar;
}

namespace Fortress::Object
{
	enum class GroundState
	{
		NotDestroyed = static_cast<uint8_t>(0),
		Outline,
		Destroyed,
		OutOfBound,
	};

	using GroundMapKey = std::pair<int, int>;

	class Ground : public Abstract::rigidBody
	{
	public:
		Ground(
			const std::wstring& name, 
			const Math::Vector2& position, 
			const Math::Vector2& size,
			const ImagePointer& tile_image = {}) :
			rigidBody(
				name, 
				position, 
				size, 
				{},
				10000.0f,
				{}, 
				{}, 
				false),
			m_ground_hdc(nullptr),
			m_ground_bitmap(nullptr),
			m_tile_image(tile_image)
		{
			Ground::initialize();
		}
		Ground& operator=(const Ground& other) = default;
		Ground& operator=(Ground&& other) = default;

		void on_collision(const CollisionCode& collision, const Math::Vector2& hit_vector, const std::weak_ptr<rigidBody>& other) override;

		~Ground() override
		{
			if(m_ground_hdc)
			{
				DeleteObject(m_ground_bitmap);
				ReleaseDC(WinAPIHandles::get_hwnd(), m_ground_hdc);
				DeleteDC(m_ground_hdc);
			}
			rigidBody::~rigidBody();
		}

		void initialize() override;
		void render() override;
		void prerender() override;

		void set_hitbox(const Math::Vector2& hitbox) override;

		GroundState safe_is_destroyed(const Math::Vector2& local_position);
		void safe_set_destroyed_global(const Math::Vector2& hit_position, const float radius);
		bool safe_is_object_stuck_global(const Math::Vector2& position);
		bool safe_is_object_stuck_local(const Math::Vector2& position);
		Math::Vector2 safe_nearest_surface(const Math::Vector2& global_position);
		// this starts from given position y
		Math::Vector2 safe_orthogonal_surface_global(
			const Math::Vector2& global_position,
			const int depth = -1,
			const int start_y = -1);

		Math::Vector2 safe_parallel_surface_global(
			const Math::Vector2& global_position, const Math::Vector2& offset);
	protected:
		HDC get_ground_hdc() const;

		HDC get_ground_mask_hdc() const;

		std::unique_ptr<Bitmap> get_mask_bitmap_copy() const;

		void force_update_mask();

		void unsafe_set_destroyed(const int x, const int y);
		void unsafe_set_destroyed_visual(int x, int y);
		void safe_set_circle_destroyed(const Math::Vector2& center_position, const int radius);
		Math::Vector2 safe_orthogonal_surface_local(
			const Math::Vector2& local_position,
			const int depth);
		void unsafe_set_line_destroyed(const Math::Vector2& line, const int n);
		void unsafe_set_line_destroyed_reverse(const Math::Vector2& line, int n);
		bool safe_is_projectile_hit(const Math::Vector2& hit_position, const std::weak_ptr<ObjectBase::projectile>& projectile_ptr);

		COLORREF get_pixel_threadsafe(const int x, const int y);

		void reset_hdc();

		friend Radar;
		std::map<GroundMapKey, GroundState> m_destroyed_table;
		HDC m_ground_hdc;
		HDC m_mask_hdc;
		HDC m_buffer_hdc;

		HBITMAP m_ground_bitmap;
		HBITMAP m_mask_bitmap;
		HBITMAP m_buffer_bitmap;

	private:
		void set_tile(const std::weak_ptr<ImageWrapper>& tile_image) const;

		ImagePointer m_tile_image;
		std::mutex map_write_lock;
		std::mutex map_read_lock;
		std::mutex hdc_lock;
	};

	inline void Ground::on_collision(
		const CollisionCode& collision, 
		const Math::Vector2& hit_vector, 
		const std::weak_ptr<Abstract::rigidBody>& other)
	{
		if (auto const projectile = other.lock()->downcast_from_this<ObjectBase::projectile>())
		{
			const eHitVector e_vec = Math::translate_hit_vector(hit_vector);
			const auto hit_point = projectile->get_hit_point(e_vec);

			if (projectile->get_max_hit_count() > projectile->get_hit_count() &&
				safe_is_projectile_hit(hit_point, projectile))
			{
				projectile->stateController::downcast_from_this<Controller::ProjectileController>()->notify_ground_hit();
				safe_set_destroyed_global(hit_point, projectile->get_radius());
			}
		}
	}

	inline void Ground::initialize()
	{
		// @todo: initialization path gets too complicated.
		if(m_hitbox != Math::zero)
		{
			reset_hdc();

			if(const auto tile_image = m_tile_image.lock())
			{
				set_tile(tile_image);
			}
		}

		rigidBody::initialize();
	}

	inline void Ground::render()
	{
		rigidBody::render();

		if (is_active())
		{
			if(const auto camera_ptr = Scene::SceneManager::get_active_scene().lock()->get_camera().lock())
			{
				const auto pos = camera_ptr->get_relative_position(
				std::dynamic_pointer_cast<object>(shared_from_this()));

				prerender();

				// Move ground buffer to render buffer.
				GdiTransparentBlt(
					WinAPIHandles::get_buffer_dc(),
					pos.get_x(),
					pos.get_y(),
					m_hitbox.get_x(),
					m_hitbox.get_y(),
					m_buffer_hdc,
					0,
					0,
					m_hitbox.get_x(),
					m_hitbox.get_y(),
					RGB(0,0,0));

				Debug::draw_dot(pos);
			}
		}
	}

	inline void Ground::prerender()
	{
		rigidBody::prerender();

		// Copy ground sprite to buffer.
		BitBlt(
			m_buffer_hdc,
			0,
			0,
			m_hitbox.get_x(),
			m_hitbox.get_y(),
			m_ground_hdc,
			0,
			0,
			SRCCOPY);

		// AND operation with mask.
		BitBlt(
			m_buffer_hdc,
			0,
			0,
			m_hitbox.get_x(),
			m_hitbox.get_y(),
			m_mask_hdc,
			0,
			0,
			SRCAND);

		if(Debug::get_debug_flag())
		{
			StretchBlt(
				WinAPIHandles::get_buffer_dc(),
				100,
				10,
				300,
				100,
				m_mask_hdc,
				0,
				0,
				m_hitbox.get_x(),
				m_hitbox.get_y(),
				SRCCOPY);

			StretchBlt(
				WinAPIHandles::get_buffer_dc(),
				100,
				120,
				300,
				100,
				m_ground_hdc,
				0,
				0,
				m_hitbox.get_x(),
				m_hitbox.get_y(),
				SRCCOPY);
		}
	}

	inline void Ground::set_hitbox(const Math::Vector2& hitbox)
	{
		m_destroyed_table.clear();
		for(int i = 0; i < static_cast<int>(m_hitbox.get_y()); ++i)
		{
			for(int j = 0; j < static_cast<int>(m_hitbox.get_x()); ++j)
			{
				std::lock_guard _(map_write_lock);
				m_destroyed_table[{i, j}] = GroundState::NotDestroyed;
			}
		}

		rigidBody::set_hitbox(hitbox);
	}

	inline void Ground::set_tile(const std::weak_ptr<ImageWrapper>& tile_image) const
	{
		if(const auto tile = tile_image.lock())
		{
			tile->tile_copy_to(m_hitbox, m_ground_hdc);
		}
	}

	inline GroundState Ground::safe_is_destroyed(const Math::Vector2& local_position)
	{
		if(static_cast<int>(local_position.get_x()) >= 0 && 
			static_cast<int>(local_position.get_x()) < m_hitbox.get_x() && 
			static_cast<int>(local_position.get_y()) >= 0 && 
			static_cast<int>(local_position.get_y()) < m_hitbox.get_y())
		{
			int i = static_cast<int>(local_position.get_y());
			int j = static_cast<int>(local_position.get_x());
			std::lock_guard _(map_read_lock);
			return m_destroyed_table.at({i ,j});
		}

		return GroundState::OutOfBound;
	}

	inline void Ground::unsafe_set_destroyed(const int x, const int y)
	{
		std::lock_guard _(map_write_lock);
		m_destroyed_table[{y, x}] = GroundState::Destroyed;
	}

	inline void Ground::unsafe_set_destroyed_visual(const int x, const int y)
	{
		{
			std::lock_guard _hdc(hdc_lock);
			SetPixel(m_mask_hdc, x, y, RGB(0,0,0));
		}
	}

	inline void Ground::safe_set_circle_destroyed(const Math::Vector2& center_position, const int radius)
	{
		const Math::Vector2 start_pos = {center_position.get_x() - radius, center_position.get_y()};
		Math::Vector2 curr_pos = start_pos;
		const Math::Vector2 end_pos = {center_position.get_x() + radius, center_position.get_y()};

		float radian = 0.0f;
		const float sin_max = Math::to_radian(180.0f);
		const float sin_iter = sin_max / (radius * 2.0f);

		while(curr_pos != end_pos)
		{
			const int column_n = static_cast<int>(std::sin(radian) * static_cast<float>(radius * 2));

			const int upper_size = column_n / 2 - 1;
			const int lower_size = column_n / 2;

			for(int i = 0; i < upper_size; ++i)
			{
				if(static_cast<int>(curr_pos.get_x()) >= 0 && 
					static_cast<int>(curr_pos.get_x()) < m_hitbox.get_x() && 
					static_cast<int>(curr_pos.get_y() + i) >= 0 && 
					static_cast<int>(curr_pos.get_y() + i) < m_hitbox.get_y())
				{
					unsafe_set_destroyed(curr_pos.get_x(), curr_pos.get_y() + i);
					unsafe_set_destroyed_visual(curr_pos.get_x(), curr_pos.get_y() + i);
				}
			}

			for(int i = 0; i < lower_size; ++i)
			{
				if(static_cast<int>(curr_pos.get_x()) >= 0 && 
					static_cast<int>(curr_pos.get_x()) < m_hitbox.get_x() && 
					static_cast<int>(curr_pos.get_y() - i) >= 0 && 
					static_cast<int>(curr_pos.get_y() - i) < m_hitbox.get_y())
				{
					unsafe_set_destroyed(curr_pos.get_x(), curr_pos.get_y() - i);
					unsafe_set_destroyed_visual(curr_pos.get_x(), curr_pos.get_y() - i);
				}
			}

			radian += sin_iter;
			curr_pos = {curr_pos.get_x() + 1, curr_pos.get_y()};
		}
	}

	inline void Ground::unsafe_set_line_destroyed(const Math::Vector2& line, const int n)
	{
		for(int i = 0; i < n; ++i)
		{
			unsafe_set_destroyed(line.get_x() + i, line.get_y());
			unsafe_set_destroyed_visual(line.get_x() + i, line.get_y());
		}
	}

	inline void Ground::unsafe_set_line_destroyed_reverse(const Math::Vector2& line, const int n)
	{
		for(int i = n - 1; i >= 0 ; --i)
		{
			unsafe_set_destroyed(line.get_x() - i, line.get_y());
			unsafe_set_destroyed_visual(line.get_x() - i, line.get_y());
		}
	}

	inline bool Ground::safe_is_projectile_hit(
		const Math::Vector2& hit_position,
		const std::weak_ptr<ObjectBase::projectile>& projectile_ptr)
	{
		if(const auto projectile = projectile_ptr.lock())
		{
			const auto local_position = to_top_left_local_position(hit_position);
			const GroundState ground_status = safe_is_destroyed(local_position);

			if(ground_status == GroundState::NotDestroyed)
			{
				Debug::Log(L"Projectile hits the Ground");
				return true;
			}
			if(ground_status == GroundState::Destroyed)
			{
				Debug::Log(L"Projectile hits the destroyed ground");
				return false;
			}
		}

		return false;
	}

	inline bool Ground::safe_is_object_stuck_global(const Math::Vector2& global_position)
	{
		const auto local_position = to_top_left_local_position(global_position);
		return safe_is_object_stuck_local(local_position);
	}

	inline bool Ground::safe_is_object_stuck_local(const Math::Vector2& local_position)
	{
		// @todo: proper oob definition
		Math::Vector2 offsets[4] =
		{
			{-1.0f, 0.0f},
			{1.0f, 0.0f},
			{0.0f, -1.0f},
			{0.0f, 1.0f},
		};

		int count = 0;

		for (const auto& offset : offsets) 
		{
			const auto results = safe_is_destroyed(local_position + offset);
			count += results == GroundState::NotDestroyed;
		}

		return count == std::size(offsets);
	}

	inline Math::Vector2 Ground::safe_nearest_surface(const Math::Vector2& global_position)
	{
		const auto o_local_position = to_top_left_local_position(global_position);
		auto local_position = to_top_left_local_position(global_position);

		if(local_position.get_y() <= 0)
		{
			return {0, -local_position.get_y()};
		}

		while(local_position.get_y() > 0)
		{
			if(safe_is_destroyed(local_position) == GroundState::Destroyed)
			{
				auto delta = o_local_position - local_position;
				return delta;
			}

			local_position = {local_position.get_x(), local_position.get_y() - 1};
		}

		return {0, -local_position.get_y()};
	}

	inline Math::Vector2 Ground::safe_orthogonal_surface_global(const Math::Vector2& global_position, const int depth, const int start_y)
	{
		auto local_position = to_top_left_local_position(global_position);

		if(start_y == -1)
		{
			local_position = {local_position.get_x(), local_position.get_y()};
		}
		else
		{
			local_position = {local_position.get_x(), start_y};
		}

		return safe_orthogonal_surface_local(local_position, depth);
	}

	inline Math::Vector2 Ground::safe_parallel_surface_global(
		const Math::Vector2& global_position,
		const Math::Vector2& offset)
	{
		const auto local_position = to_top_left_local_position(global_position);
		const int i_offset = offset == Math::left ? -1 : 1;

		for(int i = 0; 
			i > -m_hitbox.get_x() && i < m_hitbox.get_x(); 
			i += i_offset)
		{
			const Math::Vector2 new_pos = {local_position.get_x() + i, local_position.get_y()};

			if(safe_is_destroyed(new_pos) == GroundState::NotDestroyed)
			{
				return new_pos - local_position;
			}
		}

		return Math::vector_inf;
	}

	inline Math::Vector2 Ground::safe_orthogonal_surface_local(const Math::Vector2& local_position, const int depth)
	{
		const int start_y = local_position.get_y();
		int end_y = start_y + depth;

		if(depth == -1)
		{
			end_y = m_hitbox.get_y();
		}

		for(int y = start_y; y < end_y; ++y)
		{
			const Math::Vector2 new_pos = {local_position.get_x(), y};

			if(safe_is_destroyed(new_pos) == GroundState::NotDestroyed)
			{
				return new_pos - local_position;
			}
		}

		return Math::vector_inf;
	}

	inline HDC Ground::get_ground_hdc() const
	{
		return m_ground_hdc;
	}

	inline HDC Ground::get_ground_mask_hdc() const
	{
		return m_mask_hdc;
	}

	inline std::unique_ptr<Bitmap> Ground::get_mask_bitmap_copy() const
	{
		return std::unique_ptr<Bitmap>(Bitmap::FromHBITMAP(m_mask_bitmap, nullptr));
	}

	inline void Ground::force_update_mask()
	{
		for(int i = 0; i < static_cast<int>(m_hitbox.get_y()); ++i)
		{
			for(int j = 0; j < static_cast<int>(m_hitbox.get_x()); ++j)
			{
				std::lock_guard _(map_read_lock);
				std::lock_guard _g(hdc_lock);

				if(m_destroyed_table.at({i, j}) == GroundState::NotDestroyed)
				{
					SetPixel(m_mask_hdc, j, i, RGB(255, 255, 255));
				}
				else
				{
					SetPixel(m_mask_hdc, j, i, RGB(0, 0, 0));
				}
			}
		}
	}

	inline void Ground::safe_set_destroyed_global(
		const Math::Vector2& hit_position,
		const float radius)
	{
		const auto local_position = to_top_left_local_position(hit_position);
		safe_set_circle_destroyed(local_position, radius);
	}

	inline COLORREF Ground::get_pixel_threadsafe(const int x, const int y)
	{
		std::lock_guard _(hdc_lock);
		return GetPixel(m_ground_hdc, x ,y);
	}

	inline void Ground::reset_hdc()
	{
		if(m_ground_bitmap)
		{
			DeleteObject(m_ground_bitmap);
		}

		if(m_ground_hdc)
		{
			ReleaseDC(nullptr, m_ground_hdc);
		}

		if(m_mask_bitmap)
		{
			DeleteObject(m_mask_hdc);
		}

		if(m_mask_hdc)
		{
			ReleaseDC(nullptr, m_mask_hdc);
		}

		m_ground_hdc = CreateCompatibleDC(WinAPIHandles::get_main_dc());
		m_ground_bitmap = CreateCompatibleBitmap(
			WinAPIHandles::get_main_dc(), m_hitbox.get_x(), m_hitbox.get_y());

		m_mask_hdc = CreateCompatibleDC(WinAPIHandles::get_main_dc());
		m_mask_bitmap = CreateCompatibleBitmap(
			WinAPIHandles::get_main_dc(), m_hitbox.get_x(), m_hitbox.get_y());

		m_buffer_hdc = CreateCompatibleDC(WinAPIHandles::get_main_dc());
		m_buffer_bitmap = CreateCompatibleBitmap(
			WinAPIHandles::get_main_dc(), m_hitbox.get_x(), m_hitbox.get_y());

		DeleteObject(SelectObject(m_ground_hdc, m_ground_bitmap));
		DeleteObject(SelectObject(m_mask_hdc, m_mask_bitmap));
		DeleteObject(SelectObject(m_buffer_hdc, m_buffer_bitmap));

		const auto mask_bkgd = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
		const auto rect = RECT{0, 0, static_cast<int>(m_hitbox.get_x()), static_cast<int>(m_hitbox.get_y())};
		FillRect(m_mask_hdc, &rect, mask_bkgd);

		for(int i = 0; i < static_cast<int>(m_hitbox.get_y()); ++i)
		{
			for(int j = 0; j < static_cast<int>(m_hitbox.get_x()); ++j)
			{
				std::lock_guard _(map_write_lock);
				m_destroyed_table[{i, j}] = GroundState::NotDestroyed;
			}
		}

		DeleteObject(mask_bkgd);
	}
}

#endif
