#pragma once
#ifndef ROOM_GAME_HPP
#define ROOM_GAME_HPP

// Server-side state of one running match: turns, wind, hp and the
// authoritative shot simulation (Common/Sim). Network-free: received
// messages go in through on_*(), messages to send come out of
// take_outbox(), so Server.cpp only routes packets.

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cmath>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "../Common/message.hpp"
#include "../Common/Sim/SimShot.hpp"
#include "../Common/Sim/SimMove.hpp"

namespace Fortress::Network::Server
{
	static_assert(static_cast<int>(eShotEvent::Explosion) == static_cast<int>(Sim::ShotEventType::Explosion));
	static_assert(static_cast<int>(eShotEvent::Damage) == static_cast<int>(Sim::ShotEventType::Damage));
	static_assert(static_cast<int>(eShotEvent::Death) == static_cast<int>(Sim::ShotEventType::Death));
	static_assert(static_cast<int>(eShotEvent::Teleport) == static_cast<int>(Sim::ShotEventType::Teleport));
	static_assert(static_cast<int>(eShotEvent::End) == static_cast<int>(Sim::ShotEventType::End));

	class RoomGame
	{
	public:
		using Clock = std::chrono::steady_clock;
		using Payload = std::variant<ShotPathMsg, ShotEventsMsg, TurnChangeMsg, CharacterStateMsg>;

		struct Outgoing
		{
			PlayerID target;
			Payload message;
		};

		static constexpr float turn_seconds = 60.0f;
		// Pause after a shot's replay before the next turn (NextPlayerTimer).
		static constexpr float next_turn_delay_seconds = 2.0f;
		static constexpr auto resend_interval = std::chrono::milliseconds(250);
		static constexpr int max_resends = 40;
		static constexpr auto state_interval = std::chrono::milliseconds(50);
		// Longest catch-up after a stall, in movement ticks.
		static constexpr int max_ticks_per_update = 30;

		RoomGame() : m_random(static_cast<unsigned>(Clock::now().time_since_epoch().count()))
		{
		}

		// Builds the world for the GameInit the room was started with.
		bool start(const RoomID room, const GameInitMsg& init, const std::string& cloud_mask, const Clock::time_point now)
		{
			*this = RoomGame();
			m_room = room;

			const bool built = init.map_type == eMapType::SkyValleyMap
				? Sim::build_map(m_world, Map::Layout::sky_valley, cloud_mask)
				: Sim::build_map(m_world, Map::Layout::desert);

			if (!built)
			{
				return false;
			}

			for (int i = 0; i < init.player_count && i < 15; ++i)
			{
				Sim::SimCharacter ch;
				ch.id = init.players[i];
				ch.type = to_sim(init.character_type[i]);
				m_world.characters.push_back(ch);
				m_items[ch.id] = {init.equied_item[i][0], init.equied_item[i][1], init.equied_item[i][2], init.equied_item[i][3]};
			}

			std::sort(m_world.characters.begin(), m_world.characters.end(),
				[](const auto& l, const auto& r) { return l.id < r.id; });

			// Same spawn points as BattleScene::spawnpoints.
			Sim::spawn_characters(m_world);
			m_last_step = now;

			m_active = !m_world.characters.empty();
			if (m_active)
			{
				begin_turn(m_world.characters.front().id, now);
			}

			return m_active;
		}

		bool active() const { return m_active; }
		PlayerID current() const { return m_current; }
		uint32_t turn_no() const { return m_turn_no; }
		const Sim::SimWorld& world() const { return m_world; }

		// Held movement key of the current player (Sim::MoveInput values).
		void on_move_input(const PlayerID pid, const MoveInputMsg& msg)
		{
			if (!m_active || pid != m_current || m_phase != Phase::Aiming || msg.turn_no != m_turn_no ||
				msg.input > static_cast<uint8_t>(Sim::MoveInput::Right))
			{
				return;
			}

			if (m_input_seen && msg.seq <= m_input_seq)
			{
				return;  // older than the one applied
			}

			m_input_seen = true;
			m_input_seq = msg.seq;
			m_input = static_cast<Sim::MoveInput>(msg.input);
		}

		// Test hook (--debug-state only): puts a character somewhere.
		void debug_place(const PlayerID pid, const float x, const float y)
		{
			if (Sim::SimCharacter* ch = m_world.find(pid); ch != nullptr && std::isfinite(x) && std::isfinite(y))
			{
				ch->position = {x, y};
				ch->velocity = {};
				ch->gravity_speed = 0.0f;
			}
		}

		void on_shot_request(const PlayerID pid, const ShotRequestMsg& request, const Clock::time_point now)
		{
			if (!m_active || pid != m_current || m_phase != Phase::Aiming || request.turn_no != m_turn_no)
			{
				return;
			}

			// Fires from where the server has the character.
			// set_unmovable(): a walking shooter stops.
			m_input = Sim::MoveInput::None;
			// The client fires from Idle (not walking), facing its own way.
			if (Sim::SimCharacter* shooter = m_world.find(pid))
			{
				if (shooter->state == Sim::CharacterState::Move)
				{
					Sim::MoveDetail::stop(*shooter);
				}
				shooter->state = Sim::CharacterState::Idle;
				shooter->facing = request.facing < 0.0f ? -1.0f : 1.0f;
			}

			Sim::ShotInput input;
			input.shooter = pid;
			input.kind = request.prj_type == eProjectileType::Sub ? Sim::ShotKind::Sub : Sim::ShotKind::Main;
			input.charge = std::clamp(request.charged, 0.0f, Sim::max_charge);
			input.wind = m_wind;

			// Items are reusable on purpose (test cheat), so only check the
			// player has the item equipped.
			const auto& items = m_items[pid];
			const bool has_item = request.item != eItemType::None &&
				std::find(items.begin(), items.end(), request.item) != items.end();

			if (has_item)
			{
				switch (request.item)
				{
				case eItemType::DoubleShot: input.volleys = 2; break;
				case eItemType::DoubleDamage: input.double_damage = true; break;
				case eItemType::Teleport:
					input.kind = Sim::ShotKind::Nutshell;
					input.teleport = true;
					break;
				case eItemType::Repair:
				{
					// Instant: +50 hp, no shot, the turn ends.
					Sim::SimCharacter* ch = m_world.find(pid);
					ch->hp = std::min(Sim::character_full_hp, ch->hp + 50.0f);
					m_phase = Phase::Replaying;
					m_input = Sim::MoveInput::None;
					m_replay_end_at = now;
					m_next_turn_at = now + seconds(next_turn_delay_seconds);
					return;
				}
				default: break;
				}
			}

			m_last_shot = Sim::simulate_shot(m_world, input);
			std::cout << "Room " << m_room << " turn " << m_turn_no << ": player " << pid << " fired, "
				<< m_last_shot.path.size() << " path samples, " << m_last_shot.events.size() << " events, end tick "
				<< m_last_shot.end_tick << std::endl;
			m_shot_kind = static_cast<uint8_t>(
				input.teleport ? eProjectileType::Nutshell :
				input.kind == Sim::ShotKind::Sub ? eProjectileType::Sub : eProjectileType::Main);
			m_phase = Phase::Replaying;
			// Characters may fall once the clients' replay reaches the first
			// crater (clients ignore positions while they replay).
			int first_explosion = m_last_shot.end_tick;
			for (const auto& e : m_last_shot.events)
			{
				if (e.type == Sim::ShotEventType::Explosion)
				{
					first_explosion = std::min(first_explosion, e.tick);
				}
			}
			m_shot_turn = m_turn_no;
			m_replay_end_at = now + seconds(first_explosion * Sim::tick_seconds);
			m_next_turn_at = m_replay_end_at + seconds(next_turn_delay_seconds);

			build_shot_chunks(pid);
			for (const auto& ch : m_world.characters)
			{
				for (size_t i = 0; i < m_path_chunks.size(); ++i) m_pending_shot[ch.id].insert({0, static_cast<uint16_t>(i)});
				for (size_t i = 0; i < m_event_chunks.size(); ++i) m_pending_shot[ch.id].insert({1, static_cast<uint16_t>(i)});
			}
			m_shot_resends = 0;
			send_pending_shot();
			m_last_shot_send = now;
		}

		void on_shot_ack(const PlayerID pid, const ShotAckMsg& ack)
		{
			if (ack.turn_no == m_turn_no)
			{
				m_pending_shot[pid].erase({ack.is_events ? 1 : 0, ack.chunk});
			}
		}

		void on_turn_ack(const PlayerID pid, const TurnAckMsg& ack)
		{
			if (ack.turn_no == m_turn_no)
			{
				m_pending_turn.erase(pid);
			}
		}

		void tick(const Clock::time_point now)
		{
			if (!m_active)
			{
				return;
			}

			if (!m_pending_turn.empty() && now - m_last_turn_send >= resend_interval && m_turn_resends < max_resends)
			{
				for (const PlayerID pid : m_pending_turn) m_outbox.push_back({pid, m_turn_message});
				m_last_turn_send = now;
				m_turn_resends++;
			}

			if (now - m_last_shot_send >= resend_interval && m_shot_resends < max_resends)
			{
				if (send_pending_shot())
				{
					m_shot_resends++;
				}
				m_last_shot_send = now;
			}

			step_movement(now);

			if (m_phase != Phase::Over && now - m_last_state_send >= state_interval)
			{
				send_state();
				m_last_state_send = now;
			}

			const bool timed_out = m_phase == Phase::Aiming && now >= m_turn_deadline;
			const bool shot_done = m_phase == Phase::Replaying && now >= m_next_turn_at;

			if (timed_out || shot_done)
			{
				next_turn(now);
			}
		}

		std::vector<Outgoing> take_outbox()
		{
			std::vector<Outgoing> out;
			out.swap(m_outbox);
			return out;
		}

	private:
		enum class Phase { Aiming, Replaying, Over };

		static Sim::CharacterType to_sim(const eCharacterType type)
		{
			switch (type)
			{
			case eCharacterType::MissileCharacter: return Sim::CharacterType::Missile;
			case eCharacterType::SecwindCharacter: return Sim::CharacterType::Secwind;
			default: return Sim::CharacterType::Cannon;
			}
		}

		static Clock::duration seconds(const float s)
		{
			return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(s));
		}

		// Runs the movement simulation in fixed ticks up to now. While a shot
		// is being replayed on the clients nobody moves (the craters appear
		// on their screens only when the replay reaches them).
		void step_movement(const Clock::time_point now)
		{
			const auto tick = seconds(Sim::tick_seconds);
			int steps = 0;

			while (now - m_last_step >= tick)
			{
				m_last_step += tick;
				if (++steps > max_ticks_per_update)
				{
					m_last_step = now;
					break;
				}

				const bool frozen = m_phase == Phase::Replaying && m_last_step < m_replay_end_at;
				if (frozen || m_phase == Phase::Over)
				{
					continue;
				}

				const bool movable = m_phase == Phase::Aiming;
				Sim::step_world(m_world, m_current, movable ? m_input : Sim::MoveInput::None, movable);
				m_tick++;
				check_falls(now);
			}
		}

		void check_falls(const Clock::time_point now)
		{
			int alive = 0;
			bool fell = false;
			bool current_died = false;

			for (auto& ch : m_world.characters)
			{
				if (!ch.alive && ch.hp > 0.0f)
				{
					// Fell out of the map (step_character cleared alive).
					ch.hp = 0.0f;
					fell = true;
					current_died = current_died || ch.id == m_current;
					std::cout << "Room " << m_room << ": player " << ch.id << " fell" << std::endl;
				}
				alive += ch.alive ? 1 : 0;
			}

			if (m_phase == Phase::Aiming && fell && (current_died || alive <= 1))
			{
				m_phase = Phase::Replaying;
				m_input = Sim::MoveInput::None;
				m_replay_end_at = now;
				m_next_turn_at = now;
			}
		}

		void send_state()
		{
			CharacterStateMsg msg{};
			msg.type = eMessageType::CharacterState;
			msg.room_id = m_room;
			msg.player_id = -1;
			msg.turn_no = m_turn_no;
			msg.tick = m_tick;
			msg.shot_turn = m_shot_turn;

			for (const auto& ch : m_world.characters)
			{
				if (msg.count >= std::size(msg.entries)) break;
				auto& e = msg.entries[msg.count++];
				e.pid = ch.id;
				e.x = ch.position.x;
				e.y = ch.position.y;
				e.facing = ch.facing;
				e.pitch = std::isfinite(ch.pitch) ? ch.pitch : 0.0f;
				e.velocity_x = ch.velocity.x;
				e.mp = ch.mp;
				e.moving = ch.state == Sim::CharacterState::Move;
				e.alive = ch.alive;
			}

			const auto stamped = create_prewritten_network_message<CharacterStateMsg>(msg);
			for (const auto& ch : m_world.characters)
			{
				m_outbox.push_back({ch.id, stamped});
			}
		}

		int roll_wind()
		{
			std::uniform_real_distribution<float> distribution{-50.0f, 50.0f};
			// Same rounding as the server's old get_wind_acceleration.
			return static_cast<int>(distribution(m_random) / 10.0f) * 10;
		}

		void next_turn(const Clock::time_point now)
		{
			std::vector<PlayerID> alive;
			for (const auto& ch : m_world.characters)
			{
				if (ch.alive) alive.push_back(ch.id);
			}

			if (alive.size() <= 1)
			{
				m_phase = Phase::Over;
				m_winner = alive.empty() ? -2 : alive.front();
				announce_turn(-1, now);
				return;
			}

			// Next alive player after the current one, in player id order.
			const auto after = std::upper_bound(alive.begin(), alive.end(), m_current);
			begin_turn(after == alive.end() ? alive.front() : *after, now);
		}

		void begin_turn(const PlayerID pid, const Clock::time_point now)
		{
			for (auto& ch : m_world.characters)
			{
				ch.hits_this_turn = 0;
			}

			// Round::apply_turn_change on the clients: the previous player
			// stops and gets its mp back.
			if (Sim::SimCharacter* previous = m_world.find(m_current))
			{
				Sim::finish_turn(*previous);
			}

			m_input = Sim::MoveInput::None;
			m_input_seen = false;
			m_pending_shot.clear();
			m_wind = roll_wind();
			m_phase = Phase::Aiming;
			m_turn_deadline = now + seconds(turn_seconds);
			announce_turn(pid, now);
		}

		void announce_turn(const PlayerID current, const Clock::time_point now)
		{
			m_turn_no++;
			m_current = current;

			TurnChangeMsg msg{};
			msg.type = eMessageType::TurnChange;
			msg.room_id = m_room;
			msg.player_id = -1;
			msg.turn_no = m_turn_no;
			msg.current = current;
			msg.winner = m_winner;
			msg.wind = m_wind;
			msg.time_limit = turn_seconds;

			uint32_t crc = 0;
			for (const auto& g : m_world.grounds) crc = crc * 31u + g.checksum();
			msg.terrain_crc = crc;

			for (const auto& ch : m_world.characters)
			{
				if (msg.count >= 15) break;
				msg.players[msg.count] = ch.id;
				msg.hp[msg.count] = ch.hp;
				msg.alive[msg.count] = ch.alive;
				msg.count++;
			}

			std::cout << "Room " << m_room << " turn " << m_turn_no << ": current " << current << ", winner " << m_winner
				<< ", wind " << m_wind << std::endl;

			m_turn_message = create_prewritten_network_message<TurnChangeMsg>(msg);
			m_pending_turn.clear();
			for (const auto& ch : m_world.characters)
			{
				m_pending_turn.insert(ch.id);
				m_outbox.push_back({ch.id, m_turn_message});
			}
			m_turn_resends = 0;
			m_last_turn_send = now;
		}

		void build_shot_chunks(const PlayerID shooter)
		{
			m_path_chunks.clear();
			m_event_chunks.clear();

			const auto& path = m_last_shot.path;
			const auto& events = m_last_shot.events;
			constexpr size_t path_per_chunk = std::size(ShotPathMsg{}.samples);
			constexpr size_t events_per_chunk = std::size(ShotEventsMsg{}.events);

			const size_t path_chunks = std::max<size_t>(1, (path.size() + path_per_chunk - 1) / path_per_chunk);
			const size_t event_chunks = std::max<size_t>(1, (events.size() + events_per_chunk - 1) / events_per_chunk);
			const auto end_tick = static_cast<uint16_t>(std::min(m_last_shot.end_tick, 65535));

			for (size_t c = 0; c < path_chunks; ++c)
			{
				ShotPathMsg msg{};
				msg.type = eMessageType::ShotPath;
				msg.room_id = m_room;
				msg.player_id = -1;
				msg.turn_no = m_turn_no;
				msg.shooter = shooter;
				msg.chunk = static_cast<uint16_t>(c);
				msg.path_chunks = static_cast<uint16_t>(path_chunks);
				msg.event_chunks = static_cast<uint16_t>(event_chunks);
				msg.end_tick = end_tick;

				for (size_t i = c * path_per_chunk; i < path.size() && msg.count < path_per_chunk; ++i)
				{
					auto& s = msg.samples[msg.count++];
					s.tick = static_cast<uint16_t>(path[i].tick);
					s.projectile = static_cast<uint8_t>(path[i].projectile);
					s.projectile_kind = m_shot_kind;
					s.x = path[i].position.x;
					s.y = path[i].position.y;
				}

				m_path_chunks.push_back(create_prewritten_network_message<ShotPathMsg>(msg));
			}

			for (size_t c = 0; c < event_chunks; ++c)
			{
				ShotEventsMsg msg{};
				msg.type = eMessageType::ShotEvents;
				msg.room_id = m_room;
				msg.player_id = -1;
				msg.turn_no = m_turn_no;
				msg.shooter = shooter;
				msg.chunk = static_cast<uint16_t>(c);
				msg.path_chunks = static_cast<uint16_t>(path_chunks);
				msg.event_chunks = static_cast<uint16_t>(event_chunks);
				msg.end_tick = end_tick;

				for (size_t i = c * events_per_chunk; i < events.size() && msg.count < events_per_chunk; ++i)
				{
					const auto& e = events[i];
					auto& out = msg.events[msg.count++];
					out.tick = static_cast<uint16_t>(e.tick);
					out.type = static_cast<uint8_t>(e.type);
					out.projectile = static_cast<uint8_t>(std::max(0, e.projectile));
					out.player = e.player;
					out.x = e.position.x;
					out.y = e.position.y;
					out.value = e.value;
					out.hp = e.hp;
				}

				m_event_chunks.push_back(create_prewritten_network_message<ShotEventsMsg>(msg));
			}
		}

		// Queues every unacknowledged chunk; returns false if nothing is pending.
		bool send_pending_shot()
		{
			bool any = false;
			for (const auto& [pid, pending] : m_pending_shot)
			{
				for (const auto& [is_events, chunk] : pending)
				{
					any = true;
					if (is_events) m_outbox.push_back({pid, m_event_chunks[chunk]});
					else m_outbox.push_back({pid, m_path_chunks[chunk]});
				}
			}
			return any;
		}

		RoomID m_room = -1;
		bool m_active = false;
		Sim::SimWorld m_world;
		std::map<PlayerID, std::vector<eItemType>> m_items;
		// Movement.
		Sim::MoveInput m_input = Sim::MoveInput::None;
		bool m_input_seen = false;
		uint32_t m_input_seq = 0;
		Clock::time_point m_last_step{};
		Clock::time_point m_replay_end_at{};
		Clock::time_point m_last_state_send{};
		uint32_t m_tick = 0;
		uint32_t m_shot_turn = 0;

		uint32_t m_turn_no = 0;
		PlayerID m_current = -1;
		PlayerID m_winner = -1;
		int m_wind = 0;
		Phase m_phase = Phase::Over;
		Clock::time_point m_turn_deadline{};
		Clock::time_point m_next_turn_at{};

		Sim::ShotResult m_last_shot;
		uint8_t m_shot_kind = 0;
		std::vector<ShotPathMsg> m_path_chunks;
		std::vector<ShotEventsMsg> m_event_chunks;
		std::map<PlayerID, std::set<std::pair<int, uint16_t>>> m_pending_shot;
		Clock::time_point m_last_shot_send{};
		int m_shot_resends = 0;

		TurnChangeMsg m_turn_message{};
		std::set<PlayerID> m_pending_turn;
		Clock::time_point m_last_turn_send{};
		int m_turn_resends = 0;

		std::vector<Outgoing> m_outbox;
		std::default_random_engine m_random;
	};
}

#endif // ROOM_GAME_HPP
