#ifndef MAP_HPP
#define MAP_HPP
#include "../Common/BattleScene.h"
#include "CannonCharacter.hpp"
#include "DoubleDamageItem.hpp"
#include "DoubleShotItem.hpp"
#include "MissileCharacter.hpp"
#include "RepairItem.hpp"
#include "../Common/objectManager.hpp"
#include "SecwindCharacter.hpp"
#include "TeleportItem.hpp"

namespace Fortress::Map
{
	class Map : public Scene::BattleScene
	{
	public:
		Map(const std::wstring& name, const Network::GameInitMsg& game_init) : BattleScene(name, game_init)
		{
		}
		void initialize() override;
		void pre_initialize() override;
		void set_bgm() override;
		void set_background_img() override;
		void set_grounds() override;

	private:
		void set_characters() override;
		void set_client_character() override;
		void character_position_update();
		void update() override;
	};

	inline void Map::initialize()
	{
		BattleScene::initialize();
	}

	inline void Map::pre_initialize()
	{
	}

	inline void Map::set_bgm()
	{
	}

	inline void Map::set_background_img()
	{
	}

	inline void Map::set_characters()
	{
		for(int i = 0; i < 15; ++i)
		{
			if(m_game_init.players[i] == 0)
			{
				continue;
			}

			auto& ch = m_characters[m_game_init.players[i]];

			switch(m_game_init.character_type[i])
			{
			case Network::eCharacterType::CannonCharacter: 
				ch = 
				ObjectBase::ObjectManager::create_object<Object::CannonCharacter>(
				m_game_init.players[i], L"Cannon" + std::to_wstring(m_game_init.players[i]), Math::Vector2{-1000, 0}, Math::left);
				break;
			case Network::eCharacterType::MissileCharacter: 
				ch = 
				ObjectBase::ObjectManager::create_object<Object::MissileCharacter>(
				m_game_init.players[i], L"Missile" + std::to_wstring(m_game_init.players[i]), Math::Vector2{-1000, 0}, Math::left);
				break;
			case Network::eCharacterType::SecwindCharacter: 
				ch = 
				ObjectBase::ObjectManager::create_object<Object::SecwindCharacter>(
				m_game_init.players[i], L"Secwind" + std::to_wstring(m_game_init.players[i]), Math::Vector2{-1000, 0}, Math::left);
				break;
			case Network::eCharacterType::None: break;
			default: ;
			}

			const auto& items = m_game_init.equied_item[i];

			for(unsigned int j = 0; j < sizeof(items) / sizeof (Network::eItemType); ++j)
			{
				switch(items[j])
				{
				// key binding is starts from 1.
				case Network::eItemType::DoubleShot:
					ch.lock()->add_item(j + 1, std::make_shared<Item::DoubleShotItem>());
					break;
				case Network::eItemType::Teleport:
					ch.lock()->add_item(j + 1, std::make_shared<Item::TeleportItem>());
					break;
				case Network::eItemType::Repair:
					ch.lock()->add_item(j + 1, std::make_shared<Item::RepairItem>());
					break;
				case Network::eItemType::DoubleDamage:
					ch.lock()->add_item(j + 1, std::make_shared<Item::DoubleDamageItem>());
					break;
				case Network::eItemType::None:
				default: break;
				}
			}
		}
	}

	inline void Map::set_client_character()
	{
		m_self = m_characters[EngineHandle::get_messenger()->get_player_id()];
	}

	inline void Map::update()
	{
		BattleScene::update();
	}

	inline void Map::set_grounds()
	{
	}
}
#endif // DESERTMAP_HPP
