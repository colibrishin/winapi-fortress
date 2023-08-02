#ifndef ITEM_HPP
#define ITEM_HPP
#include "character.hpp"
#include "entity.hpp"

namespace Fortress::Object
{
	class item : public Abstract::entity
	{
	public:
		item(const std::wstring& name, const bool instant)
			: entity(name), m_instant(instant), m_is_used(false), m_is_ended(false) {}

		~item() override = default;

		virtual void initialize() = 0;
		virtual void update(const std::weak_ptr<ObjectBase::character> owner);
		virtual bool is_effect_ended();
		bool is_instant() const;
		virtual void reset();
		void set_ended();

	private:
		bool m_instant;
		bool m_is_used;
		bool m_is_ended;
	};

	// base function will flip the used condition.
	inline void item::update(const std::weak_ptr<ObjectBase::character> owner)
	{
		m_is_used = true;
	}

	inline bool item::is_effect_ended()
	{
		return m_is_ended;
	}

	inline bool item::is_instant() const
	{
		return m_instant;
	}

	inline void item::reset()
	{
		m_is_used = false;
		m_is_ended = false;
	}

	inline void item::set_ended()
	{
		m_is_ended = true;
	}
}
#endif // ITEM_HPP