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
		// Received chunks per turn. Chunks may arrive before their turn's
		// TurnChange is processed; they are kept until that turn is replayed.
		struct ShotBuffer
		{
			Network::PlayerID shooter = -1;
			int path_chunks = -1;
			int event_chunks = -1;
			int end_tick = 0;
			std::map<uint16_t, Network::ShotPathMsg> path;
			std::map<uint16_t, Network::ShotEventsMsg> events;

			bool complete() const;
		};

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
		void start_replay(ShotBuffer& shot);
		void update_replay();
		void apply_event(const Network::ShotEventsMsg::Event& event);
		void finish_replay();
		void update_camera();
		void receive_character_state();
		void log_prediction(uint32_t turn_no, uint32_t server_terrain_crc) const;
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

		std::map<uint32_t, ShotBuffer> m_shots;
		Network::PlayerID m_shooter = -1;
		int m_end_tick = 0;

		// Replay state.
		bool m_replaying = false;
		bool m_replayed = false;
		float m_replay_time = 0.0f;
		std::map<int, ReplayProjectile> m_replay_projectiles;
		std::vector<Network::ShotEventsMsg::Event> m_replay_events;
		size_t m_next_event = 0;

		// Server movement tick of the last applied CharacterState. The
		// server does not move anyone while a shot is replayed, so states
		// up to the tick seen when the replay started are stale afterwards.
		uint32_t m_state_tick = 0;
		uint32_t m_replay_state_tick = 0;
	};
}
#endif // ROUND_HPP
