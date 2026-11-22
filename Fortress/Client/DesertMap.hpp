#ifndef DESERTMAP_HPP
#define DESERTMAP_HPP
#include "../Common/BattleScene.h"
#include "CannonCharacter.hpp"
#include "EgyptGround.hpp"
#include "EgyptStairway.hpp"
#include "Map.hpp"
#include "../Common/objectManager.hpp"
#include "../Common/MapLayout.hpp"

namespace Fortress::Map
{
	class DesertMap final : public Map
	{
	public:
		DesertMap(const Network::GameInitMsg& game_init) : Map(L"Desert", game_init)
		{
			Map::initialize();
		}

		void pre_initialize() override;
		void set_bgm() override;
		void set_background_img() override;
		void set_grounds() override;
	};

	inline void DesertMap::pre_initialize()
	{
	}

	inline void DesertMap::set_bgm()
	{
		m_bgm = Resource::ResourceManager::load<Resource::Sound>(
			L"Egyptian Rule", "./resources/sounds/stages/Stage00015.wav");
	}

	inline void DesertMap::set_background_img()
	{
		m_background = Resource::ResourceManager::load<ImageWrapper>(
			L"Desert_shutterstock", "./resources/images/desert-landscape-shutterstock.jpg");
	}

	// Layout comes from Common/MapLayout.hpp, shared with the server.
	inline void DesertMap::set_grounds()
	{
		using Layout::GroundShape;

		for (const auto& entry : Layout::desert)
		{
			const Math::Vector2 position{entry.x, entry.y};
			const Math::Vector2 size{entry.width, entry.height};

			if (entry.shape == GroundShape::Flat)
			{
				m_grounds.push_back(
					ObjectBase::ObjectManager::create_object<Object::EgyptGround>(entry.name, position, size));
			}
			else
			{
				m_grounds.push_back(
					ObjectBase::ObjectManager::create_object<Object::EgyptStairway>(
						entry.name, position, size,
						entry.shape == GroundShape::StairwayRampLeft ? Math::left : Math::right));
			}
		}
	}
}
#endif // DESERTMAP_HPP
