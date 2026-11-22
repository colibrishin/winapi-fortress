#pragma once
#ifndef ROUND_HPP
#define ROUND_HPP
#include <map>
#include <memory>
#include <vector>

#include "BattleScene.h"
#include "deltatime.hpp"
#include "message.hpp"

namespace Fortress
{
	namespace ObjectBase
	{
		class character;
		class projectile;
	}

	constexpr float max_time = 60.0f;

	// Client side of a match. The server decides turns, wind, hp and the
	// winner (TurnChange) and simulates every shot; Round shows the turn
	// and replays the shot it receives (ShotPath + ShotEvents).
	class Round : public std::enable_shared_from_this<Round>
	{
	public:
		Round();
		void initialize(const std::vector<CharacterPointer>& players);
		void update();
		float get_current_time() const;
		int get_wind_acceleration() const;
		const std::weak_ptr<ObjectBase::character>& get_current_player() const;
		const eRoundState& get_current_status() const;

	private:
		struct ReplayProjectile
		{
			std::vector<Network::ShotPathMsg::Sample> samples;
			std::weak_ptr<ObjectBase::projectile> object;
			bool spawned = false;
			bool ended = false;
		};

		void receive_turn_changes();
		void apply_turn_change(const Network::TurnChangeMsg& msg);
		void receive_shot();
		void start_replay();
		void update_replay();
		void apply_event(const Network::ShotEventsMsg::Event& event);
		void finish_replay();
		void update_camera();
		void follow_current_player() const;

		std::shared_ptr<ObjectBase::character> find_player(Network::PlayerID pid) const;

		float m_curr_timeout = 0.0f;
		float m_time_limit = max_time;
		int m_wind_affect = 0;
		uint32_t m_turn_no = 0;
		eRoundState m_state;

		std::vector<std::weak_ptr<ObjectBase::character>> m_all_players;
		std::weak_ptr<ObjectBase::character> m_current_player;
		std::weak_ptr<ObjectBase::character> m_winner;

		// Chunks of the current turn's shot.
		uint32_t m_shot_turn = 0;
		Network::PlayerID m_shooter = -1;
		int m_path_chunks = -1;
		int m_event_chunks = -1;
		int m_end_tick = 0;
		std::map<uint16_t, Network::ShotPathMsg> m_path;
		std::map<uint16_t, Network::ShotEventsMsg> m_events;

		// Replay state.
		bool m_replaying = false;
		bool m_replayed = false;
		float m_replay_time = 0.0f;
		std::map<int, ReplayProjectile> m_replay_projectiles;
		std::vector<Network::ShotEventsMsg::Event> m_replay_events;
		size_t m_next_event = 0;
	};
}
#endif // ROUND_HPP
