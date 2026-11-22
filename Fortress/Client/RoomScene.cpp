#include "RoomScene.h"
#include "../Common/KeyHelp.hpp"

#include "application.h"
#include "BulletinBoardScene.h"
#include "DesertMap.hpp"
#include "../Common/input.hpp"
#include "LoadingScene.hpp"

#include "../Common/resourceManager.hpp"
#include "SkyValleyMap.hpp"

void Fortress::Scene::RoomScene::initialize()
{
	scene::initialize();
	Resource::ResourceManager::load<ImageWrapper>(
		L"Room", "./resources/misc/room/room_blueprint.jpg");
	m_imBackground = Resource::ResourceManager::find<ImageWrapper>(L"Room");
	m_bgm = Resource::ResourceManager::load<Resource::Sound>(
		L"Room BGM", "./resources/sounds/room.wav");
}

template <typename T>
void Fortress::Scene::RoomScene::load_and_sync_map(const Network::GameInitMsg& game_info) const
{
	SceneManager::CreateScene<LoadingScene<T>>(game_info);
	SceneManager::SetActive<LoadingScene<T>>();
}

void Fortress::Scene::RoomScene::update()
{
	scene::update();
	KeyHelp::update();
	Network::GameInitMsg gis{};

	if (Input::getKey(eKeyCode::S))
	{
		EngineHandle::get_messenger()->start_game(Network::eMapType::SkyValleyMap, &gis);
		load_and_sync_map<Map::SkyValleyMap>(gis);
		return;
	}
	if (Input::getKey(eKeyCode::D))
	{
		EngineHandle::get_messenger()->start_game(Network::eMapType::DesertMap, &gis);
		load_and_sync_map<Map::DesertMap>(gis);
		return;
	}
	if (Input::getKeyDown(eKeyCode::Q))
	{
		EngineHandle::get_messenger()->send_item(Network::eItemType::DoubleShot, 0);
	}
	if (Input::getKeyDown(eKeyCode::W))
	{
		EngineHandle::get_messenger()->send_item(Network::eItemType::Teleport, 1);
	}
	if (Input::getKeyDown(eKeyCode::E))
	{
		EngineHandle::get_messenger()->send_item(Network::eItemType::Repair, 2);
	}
	if (Input::getKeyDown(eKeyCode::R))
	{
		EngineHandle::get_messenger()->send_item(Network::eItemType::DoubleDamage, 3);
	}
	if (Input::getKeyDown(eKeyCode::One))
	{
		EngineHandle::get_messenger()->send_character(
			Network::eCharacterType::CannonCharacter);
	}
	if (Input::getKeyDown(eKeyCode::Two))
	{
		EngineHandle::get_messenger()->send_character(
			Network::eCharacterType::MissileCharacter);
	}
	if (Input::getKeyDown(eKeyCode::Three))
	{
		EngineHandle::get_messenger()->send_character(
			Network::eCharacterType::SecwindCharacter);
	}

	static float room_update = 0.0f;

	if(room_update >= 1.0f)
	{
		EngineHandle::get_messenger()->check_room_update(&m_room_info);
		room_update = 0.0f;
	}

	room_update += DeltaTime::get_deltaTime();

	static float game_update = 0.0f;

	if(game_update >= 0.2f)
	{
		if(EngineHandle::get_messenger()->check_room_start(&gis))
		{
			switch(gis.map_type)
			{
			case Network::eMapType::DesertMap:
				load_and_sync_map<Map::DesertMap>(gis);
				break;
			case Network::eMapType::SkyValleyMap: 
				load_and_sync_map<Map::SkyValleyMap>(gis);
				break;
			default: break;
			}
		}
		
		game_update = 0.0f;
	}

	game_update += DeltaTime::get_deltaTime();

	for(const auto& player_name : m_room_info.player_names)
	{
		if(wcslen(player_name) != 0)
		{
			Debug::Log(player_name);
		}
	}
}

void Fortress::Scene::RoomScene::render()
{
	scene::render();

	m_imBackground.lock()->render({0, -20.f}, m_imBackground.lock()->get_hitbox());

	KeyHelp::render({
		L"\uce90\ub9ad\ud130: 1 \uce90\ub17c, 2 \ubbf8\uc0ac\uc77c, 3 \uc138\ud06c\uc708\ub4dc",
		L"\uc544\uc774\ud15c \uce78 1-4: Q \ub354\ube14\uc0f7, W \ud154\ub808\ud3ec\ud2b8, E \uc218\ub9ac, R \ub354\ube14\ub370\ubbf8\uc9c0",
		L"\uc2dc\uc791: D \uc0ac\ub9c9 \ub9f5, S \ud558\ub298 \uacc4\uace1 \ub9f5 (\ub450 \uba85\uc774 \ub2e4 \ub4e4\uc5b4\uc628 \ub4a4 \ud55c \uba85\uc774)",
	});
}

void Fortress::Scene::RoomScene::deactivate()
{
	scene::deactivate();
	m_bgm.lock()->stop(true);
}

void Fortress::Scene::RoomScene::activate()
{
	EngineHandle::get_messenger()->join_room(m_room_id, &m_room_info);
	scene::activate();
	m_bgm.lock()->play(true);
}
