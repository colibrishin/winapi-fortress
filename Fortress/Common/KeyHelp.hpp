#pragma once
#ifndef KEY_HELP_HPP
#define KEY_HELP_HPP

// On-screen list of the keys of the current screen. H shows or hides it
// (shared by every screen, shown by default).

#include <string>
#include <vector>

#include "input.hpp"

namespace Fortress
{
	class KeyHelp final
	{
	public:
		// Call from the scene's update().
		static void update()
		{
			if (Input::getKeyDown(eKeyCode::H))
			{
				m_visible = !m_visible;
			}
		}

		// Call at the end of the scene's render(), so it draws on top.
		static void render(const std::vector<std::wstring>& lines, const int x = 10, const int y = 30)
		{
			const HDC dc = EngineHandle::get_handle().lock()->get_buffer_dc();
			const int line_height = 18;

			if (!m_visible)
			{
				draw_text(dc, x, y, L"H: \uc870\uc791\ud0a4 \ubcf4\uae30");
				return;
			}

			int width = 0;
			for (const auto& line : lines)
			{
				SIZE size{};
				GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &size);
				width = (std::max)(width, static_cast<int>(size.cx));
			}

			const RECT box{x - 6, y - 4, x + width + 6, y + line_height * static_cast<int>(lines.size() + 1) + 2};
			const HBRUSH brush = CreateSolidBrush(RGB(20, 20, 20));
			FillRect(dc, &box, brush);
			DeleteObject(brush);

			int line_y = y;
			for (const auto& line : lines)
			{
				draw_text(dc, x, line_y, line);
				line_y += line_height;
			}
			draw_text(dc, x, line_y, L"H: \uc228\uae30\uae30");
		}

	private:
		static void draw_text(const HDC dc, const int x, const int y, const std::wstring& text)
		{
			const int previous_mode = SetBkMode(dc, TRANSPARENT);
			const COLORREF previous_color = SetTextColor(dc, RGB(255, 255, 255));
			TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
			SetTextColor(dc, previous_color);
			SetBkMode(dc, previous_mode);
		}

		inline static bool m_visible = true;
	};
}

#endif // KEY_HELP_HPP
