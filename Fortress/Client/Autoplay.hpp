#pragma once
#ifndef AUTOPLAY_HPP
#define AUTOPLAY_HPP

// Scripted input for testing the real client without touching the desktop.
//
// Set FORTRESS_AUTOPLAY to a ';'-separated script and the client ignores the
// keyboard and plays the script instead (keys are injected into Input, not
// into Windows). Steps:
//   wait <seconds>         do nothing
//   hold <key> <seconds>   keep a key pressed
//   tap <key>              press a key for 0.15 s
//   quit                   close the client
// Keys: A-Z, 0-9, SPACE, ENTER, UP, DOWN, LEFT, RIGHT, TAB, SHIFT.
// Example: "wait 1; tap 1; wait 1; tap ENTER; wait 2; tap SPACE"
// FORTRESS_AUTOPLAY_LOG, if set, is a file that gets one line per step.

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../Common/deltatime.hpp"
#include "../Common/input.hpp"

namespace Fortress
{
	class Autoplay final
	{
	public:
		static void initialize()
		{
			const std::string script = environment("FORTRESS_AUTOPLAY");
			if (script.empty())
			{
				return;
			}

			if (const std::string log = environment("FORTRESS_AUTOPLAY_LOG"); !log.empty())
			{
				m_log.open(log, std::ios::app);
			}

			std::stringstream stream(script);
			std::string item;
			while (std::getline(stream, item, ';'))
			{
				Step step;
				if (parse(item, step))
				{
					m_steps.push_back(step);
				}
				else
				{
					write_log("ignored step: " + item);
				}
			}

			Input::enable_virtual_input();
			m_enabled = true;
			write_log("autoplay started with " + std::to_string(m_steps.size()) + " steps");
		}

		// Call once per frame before Input::update().
		static void update()
		{
			if (!m_enabled || m_current >= m_steps.size())
			{
				return;
			}

			Step& step = m_steps[m_current];

			if (!m_started)
			{
				m_started = true;
				m_elapsed = 0.0f;
				write_log("step " + std::to_string(m_current) + ": " + step.text);

				if (step.kind == Step::Kind::Quit)
				{
					PostQuitMessage(0);
					m_current = m_steps.size();
					return;
				}

				if (step.kind == Step::Kind::Hold)
				{
					Input::set_virtual_key(step.key, true);
				}
			}

			m_elapsed += DeltaTime::get_deltaTime();

			if (m_elapsed >= step.seconds)
			{
				if (step.kind == Step::Kind::Hold)
				{
					Input::set_virtual_key(step.key, false);
				}

				m_current++;
				m_started = false;

				if (m_current == m_steps.size())
				{
					write_log("autoplay finished");
				}
			}
		}

	private:
		struct Step
		{
			enum class Kind { Wait, Hold, Quit } kind = Kind::Wait;
			eKeyCode key = eKeyCode::SPACE;
			float seconds = 0.0f;
			std::string text;
		};

		static std::string environment(const char* name)
		{
			char* value = nullptr;
			size_t size = 0;
			std::string result;

			if (_dupenv_s(&value, &size, name) == 0 && value != nullptr)
			{
				result = value;
			}

			std::free(value);
			return result;
		}

		static bool parse(const std::string& item, Step& step)
		{
			std::stringstream in(item);
			std::string verb;
			in >> verb;
			step.text = item;

			if (verb == "wait")
			{
				step.kind = Step::Kind::Wait;
				return static_cast<bool>(in >> step.seconds);
			}
			if (verb == "hold" || verb == "tap")
			{
				std::string name;
				if (!(in >> name) || !key_from_name(name, step.key))
				{
					return false;
				}
				step.kind = Step::Kind::Hold;
				step.seconds = 0.15f;
				return verb == "tap" || static_cast<bool>(in >> step.seconds);
			}
			if (verb == "quit")
			{
				step.kind = Step::Kind::Quit;
				return true;
			}

			return false;
		}

		static bool key_from_name(const std::string& name, eKeyCode& out)
		{
			static const std::map<std::string, eKeyCode> named = {
				{"SPACE", eKeyCode::SPACE}, {"ENTER", eKeyCode::ENTER}, {"UP", eKeyCode::UP},
				{"DOWN", eKeyCode::DOWN}, {"LEFT", eKeyCode::LEFT}, {"RIGHT", eKeyCode::RIGHT},
				{"TAB", eKeyCode::TAB}, {"SHIFT", eKeyCode::SHIFT},
			};

			if (const auto it = named.find(name); it != named.end())
			{
				out = it->second;
				return true;
			}

			if (name.size() != 1)
			{
				return false;
			}

			// KEY_TABLE order: Q W E ... letters, then the named keys, then 0-9.
			for (size_t i = 0; i < std::size(KEY_TABLE); ++i)
			{
				if (KEY_TABLE[i] == static_cast<uint8_t>(name[0]))
				{
					out = static_cast<eKeyCode>(i);
					return true;
				}
			}

			return false;
		}

		static void write_log(const std::string& line)
		{
			if (m_log.is_open())
			{
				m_log << line << std::endl;
			}
		}

		inline static bool m_enabled = false;
		inline static std::vector<Step> m_steps;
		inline static size_t m_current = 0;
		inline static bool m_started = false;
		inline static float m_elapsed = 0.0f;
		inline static std::ofstream m_log;
	};
}

#endif // AUTOPLAY_HPP
