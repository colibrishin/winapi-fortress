#pragma once
#ifndef BATTLESCENE_H
#define BATTLESCENE_H

#include "common.h"
#include "character.hpp"
#include "ground.hpp"
#include "message.hpp"
#include "Round.h"
#include "sound.hpp"
#include "scene.hpp"
#include "Radar.h"

namespace Fortress::Scene
{
	using PlayerCharacterMap = std::map<Network::PlayerID, std::weak_ptr<ObjectBase::character>>;

	class BattleScene : public Abstract::scene
	{
	public:
		BattleScene(const std::wstring& name, const Network::GameInitMsg& game_init);
		~BattleScene() override = default;

		virtual void initialize() override;
		virtual void pre_initialize() = 0;
		void update() override;
		void render() override;
		void deactivate() override;
		void activate() override;

		std::weak_ptr<Round> get_round_status();
		const Math::Vector2& get_map_size() const;
		std::vector<CharacterPointer> get_characters() const;
		std::vector<GroundPointer> get_grounds() const;

		virtual void set_bgm() = 0;
		virtual void set_background_img() = 0;
		virtual void set_grounds() = 0;
		virtual void set_characters() = 0;
		virtual void set_client_character() = 0;
		virtual bool predicate_OOB(const Math::Vector2& position);
		virtual bool movable(const Math::Vector2& position);

		void spawnpoints() const;
		Math::Vector2 evaluate_map_size() const;

	protected:
		Math::Vector2 m_map_size;
		std::weak_ptr<ObjectBase::character> m_self;
		PlayerCharacterMap m_characters;
		std::vector<GroundPointer> m_grounds;
		std::weak_ptr<Resource::Sound> m_bgm;
		std::weak_ptr<ImageWrapper> m_hud;
		std::weak_ptr<ImageWrapper> m_background;
		std::shared_ptr<Round> m_round;
		std::unique_ptr<Radar> m_radar;
		Network::GameInitMsg m_game_init;
	};
}

#endif
