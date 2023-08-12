#ifndef TIMER_HPP
#define TIMER_HPP
#include <map>
#include <functional>
#include "framework.h"
#include "entity.hpp"
#include "winapihandles.hpp"

namespace Fortress
{
	namespace ObjectBase
	{
		class TimerManager;
	}

	/**
	 * \brief A timer that triggered and operates temporarily in specific duration in WinAPI thread.
	 */
	class Timer : public Abstract::entity
	{
	public:
		Timer& operator=(const Timer& other) = default;
		Timer& operator=(Timer&& other) = default;
		Timer(const Timer& other) = default;
		Timer(Timer&& other) = default;
		virtual ~Timer() override;

		void initialize();
		void toggle();
		void set_duration(const UINT&);
		bool is_started() const;
		virtual void on_timer();
		virtual void reset();
		virtual void stop();

	private:
		friend ObjectBase::TimerManager;
		UINT_PTR m_timer_id;
		UINT m_duration;
		bool m_bStarted;

	protected:
		Timer(const std::wstring& name, const UINT duration, const WPARAM timer_id) :
			entity(name), m_timer_id(timer_id), m_duration(duration), m_bStarted(false)
		{
			initialize();
		}
	};

	inline Timer::~Timer()
	{
		entity::~entity();
	}

	inline void Timer::initialize()
	{
	}

	inline void Timer::toggle()
	{
		if(!m_bStarted)
		{
			SetTimer(WinAPIHandles::get_hwnd(), m_timer_id, m_duration, nullptr);
			m_bStarted = true;
		}
	}

	inline void Timer::set_duration(const UINT& duration)
	{
		m_duration = duration;
	}

	inline bool Timer::is_started() const
	{
		return m_bStarted;
	}

	inline void Timer::on_timer()
	{
		m_bStarted = false;
	}

	inline void Timer::reset()
	{
		KillTimer(WinAPIHandles::get_hwnd(), m_timer_id);
		m_bStarted = false;
	}

	inline void Timer::stop()
	{
		KillTimer(WinAPIHandles::get_hwnd(), m_timer_id);
		m_bStarted = false;
	}
}
#endif // TIMER_HPP
