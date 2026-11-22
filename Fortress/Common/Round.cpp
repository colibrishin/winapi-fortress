#include "pch.h"
#include "Round.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include "BattleScene.h"
#include "debug.hpp"
#include "ground.hpp"
#include "projectile.hpp"
#include "sceneManager.hpp"
#include "SummaryScene.hpp"

#undef min
#undef max

namespace Fortress
{
	namespace
	{
		constexpr float replay_ticks_per_second = 60.0f;
	}

	Round::Round(): m_state(eRoundState::Start)
	{
	}

	void Round::initialize(const std::vector<CharacterPointer>& players)
	{
		for(const auto& ptr : players)
		{
			if(const auto player = ptr.lock())
			{
				player->set_unmovable();
				m_all_players.push_back(ptr);
			}
		}

		// Nobody moves until the server announces the first turn.
		m_state = eRoundState::Waiting;
		m_curr_timeout = 0.0f;
	}

	void Round::update()
	{
		if (m_state == eRoundState::End)
		{
			if (const auto winner = m_winner.lock())
			{
				Debug::Log(winner->get_name() + L" won the match!");
			}
			else
			{
				Debug::Log(L"The match ended in a draw");
			}

			Scene::SceneManager::CreateScene<Scene::SummaryScene>(shared_from_this());
			Scene::SceneManager::SetActive(L"Summary Scene");
			Scene::SceneManager::remove_scene<Scene::BattleScene>();
			return;
		}

		receive_turn_changes();
		update_replay();
		receive_character_state();

		if (m_state == eRoundState::InProgress)
		{
			if (const auto current = m_current_player.lock())
			{
				if (current->get_state() == eCharacterState::Fire)
				{
					// Shot requested; wait for the server's result.
					m_state = eRoundState::Waiting;
				}
			}
		}

		if (m_state == eRoundState::Waiting && !m_replaying && !m_replayed)
		{
			EngineHandle::get_messenger()->resend_pending_shot_request();
		}

		if (m_state == eRoundState::InProgress)
		{
			m_curr_timeout = std::min(m_curr_timeout + DeltaTime::get_deltaTime(), m_time_limit);
		}

		update_camera();
	}

	std::shared_ptr<ObjectBase::character> Round::find_player(const Network::PlayerID pid) const
	{
		for (const auto& ptr : m_all_players)
		{
			if (const auto ch = ptr.lock(); ch && ch->get_player_id() == pid)
			{
				return ch;
			}
		}

		return {};
	}

	void Round::receive_turn_changes()
	{
		// One TurnChange at a time with the shot chunks in between, so a shot
		// queued between two turn changes is still replayed.
		receive_shot();

		Network::TurnChangeMsg msg{};
		while (EngineHandle::get_messenger()->pop_turn_change(&msg))
		{
			if (msg.turn_no > m_turn_no)
			{
				apply_turn_change(msg);
			}

			receive_shot();
		}
	}

	void Round::apply_turn_change(const Network::TurnChangeMsg& msg)
	{
		// A turn change ends the previous shot: replay what is left of it
		// at once (including a complete shot not started yet).
		if (!m_replaying && !m_replayed)
		{
			if (const auto found = m_shots.find(m_turn_no); found != m_shots.end() && found->second.complete())
			{
				start_replay(found->second);
			}
		}

		if (m_replaying)
		{
			m_replay_time = static_cast<float>(m_end_tick + 1) / replay_ticks_per_second;
			update_replay();
		}

		if (const auto previous = m_current_player.lock())
		{
			previous->set_unmovable();
			previous->finish_replay();
		}

		m_shots.erase(m_shots.begin(), m_shots.upper_bound(msg.turn_no - 1));
		log_prediction(msg.turn_no);
		m_turn_no = msg.turn_no;
		m_wind_affect = msg.wind;
		m_time_limit = msg.time_limit > 0.0f ? msg.time_limit : max_time;
		m_curr_timeout = 0.0f;
		m_replayed = false;

		for (int i = 0; i < msg.count && i < 15; ++i)
		{
			if (const auto ch = find_player(msg.players[i]))
			{
				ch->set_server_hp(msg.alive[i] ? msg.hp[i] : 0.0f);
			}
		}

		if (msg.current == -1)
		{
			m_winner = msg.winner >= 0 ? find_player(msg.winner) : nullptr;
			if (const auto winner = m_winner.lock())
			{
				winner->set_unmovable();
			}
			m_current_player = {};
			m_state = eRoundState::End;
			return;
		}

		m_current_player = find_player(msg.current);
		if (const auto current = m_current_player.lock())
		{
			current->set_movable();
		}
		follow_current_player();

		m_state = eRoundState::InProgress;
	}

	bool Round::ShotBuffer::complete() const
	{
		return path_chunks >= 0 && static_cast<int>(path.size()) == path_chunks &&
			static_cast<int>(events.size()) == event_chunks;
	}

	void Round::receive_shot()
	{
		Network::ShotPathMsg path{};
		while (EngineHandle::get_messenger()->pop_shot_path(&path))
		{
			if (path.turn_no < m_turn_no || path.chunk >= path.path_chunks)
			{
				continue;
			}

			auto& shot = m_shots[path.turn_no];
			shot.shooter = path.shooter;
			shot.path_chunks = path.path_chunks;
			shot.event_chunks = path.event_chunks;
			shot.end_tick = path.end_tick;
			shot.path[path.chunk] = path;
		}

		Network::ShotEventsMsg events{};
		while (EngineHandle::get_messenger()->pop_shot_events(&events))
		{
			if (events.turn_no < m_turn_no || events.chunk >= events.event_chunks)
			{
				continue;
			}

			auto& shot = m_shots[events.turn_no];
			shot.shooter = events.shooter;
			shot.path_chunks = events.path_chunks;
			shot.event_chunks = events.event_chunks;
			shot.end_tick = events.end_tick;
			shot.events[events.chunk] = events;
		}

		if (m_replaying || m_replayed)
		{
			return;
		}

		if (const auto found = m_shots.find(m_turn_no); found != m_shots.end() && found->second.complete())
		{
			start_replay(found->second);
		}
	}

	void Round::start_replay(ShotBuffer& shot)
	{
		m_replay_projectiles.clear();
		m_replay_events.clear();
		m_next_event = 0;
		m_replay_time = 0.0f;
		m_shooter = shot.shooter;
		m_end_tick = shot.end_tick;

		for (const auto& [_, chunk] : shot.path)
		{
			const int count = std::min<int>(chunk.count, static_cast<int>(std::size(chunk.samples)));
			for (int i = 0; i < count; ++i)
			{
				m_replay_projectiles[chunk.samples[i].projectile].samples.push_back(chunk.samples[i]);
			}
		}

		for (const auto& [_, chunk] : shot.events)
		{
			const int count = std::min<int>(chunk.count, static_cast<int>(std::size(chunk.events)));
			m_replay_events.insert(m_replay_events.end(), chunk.events, chunk.events + count);
		}

		std::stable_sort(m_replay_events.begin(), m_replay_events.end(),
			[](const auto& l, const auto& r) { return l.tick < r.tick; });

		shot.path.clear();
		shot.events.clear();
		shot.path_chunks = -1;
		m_replaying = true;
		m_replay_state_tick = m_state_tick;
		m_state = eRoundState::Waiting;

		if (const auto current = m_current_player.lock())
		{
			current->set_unmovable();
		}
	}

	void Round::update_replay()
	{
		if (!m_replaying)
		{
			return;
		}

		m_replay_time += DeltaTime::get_deltaTime();
		const float tick = m_replay_time * replay_ticks_per_second;
		const auto shooter = find_player(m_shooter);

		for (auto& [index, prj] : m_replay_projectiles)
		{
			if (prj.ended || prj.samples.empty())
			{
				continue;
			}

			if (!prj.spawned && tick >= prj.samples.front().tick && shooter)
			{
				const auto kind = static_cast<eProjectileType>(prj.samples.front().projectile_kind);
				const auto& first = prj.samples.front();
				prj.object = shooter->spawn_replay_projectile(kind, static_cast<unsigned>(index), {first.x, first.y});
				prj.spawned = true;
			}

			const auto object = prj.object.lock();
			if (!object)
			{
				continue;
			}

			// Position between the two samples around the current tick.
			const auto next = std::find_if(prj.samples.begin(), prj.samples.end(),
				[tick](const auto& s) { return s.tick > tick; });

			if (next == prj.samples.end())
			{
				const auto& last = prj.samples.back();
				object->replay_move({last.x, last.y});
			}
			else if (next == prj.samples.begin())
			{
				object->replay_move({next->x, next->y});
			}
			else
			{
				const auto& before = *(next - 1);
				const float span = static_cast<float>(next->tick - before.tick);
				const float t = span > 0.0f ? (tick - before.tick) / span : 1.0f;
				object->replay_move({before.x + (next->x - before.x) * t, before.y + (next->y - before.y) * t});
			}
		}

		while (m_next_event < m_replay_events.size() && m_replay_events[m_next_event].tick <= tick)
		{
			apply_event(m_replay_events[m_next_event]);
			++m_next_event;
		}

		if (tick > static_cast<float>(m_end_tick) && m_next_event >= m_replay_events.size())
		{
			finish_replay();
		}
	}

	void Round::apply_event(const Network::ShotEventsMsg::Event& event)
	{
		using ShotEventType = Network::eShotEvent;
		const auto type = static_cast<ShotEventType>(event.type);
		const auto scene = Scene::SceneManager::get_active_scene().lock();

		const auto replay_object = [this, &event]() -> std::shared_ptr<ObjectBase::projectile>
		{
			const auto found = m_replay_projectiles.find(event.projectile);
			return found == m_replay_projectiles.end() ? nullptr : found->second.object.lock();
		};

		switch (type)
		{
		case ShotEventType::Explosion:
			if (scene)
			{
				for (const auto& ptr : scene->get_objects<Object::Ground>())
				{
					if (const auto ground = ptr.lock())
					{
						ground->safe_set_destroyed_global({event.x, event.y}, event.value);
					}
				}
			}
			if (const auto object = replay_object())
			{
				object->replay_hit();
			}
			break;
		case ShotEventType::Damage:
		case ShotEventType::Death:
			if (const auto ch = find_player(event.player))
			{
				ch->set_server_hp(type == ShotEventType::Death ? 0.0f : event.hp);
			}
			break;
		case ShotEventType::Teleport:
			if (const auto ch = find_player(event.player))
			{
				ch->teleport_to({event.x, event.y});
			}
			break;
		case ShotEventType::End:
			if (const auto found = m_replay_projectiles.find(event.projectile); found != m_replay_projectiles.end())
			{
				if (const auto object = found->second.object.lock())
				{
					object->replay_move({event.x, event.y});
					object->replay_end();
				}
				found->second.ended = true;
			}
			break;
		default:
			break;
		}
	}

	void Round::finish_replay()
	{
		for (auto& [_, prj] : m_replay_projectiles)
		{
			if (const auto object = prj.object.lock(); object && !prj.ended)
			{
				object->replay_end();
			}
		}

		m_replay_projectiles.clear();
		m_replay_events.clear();
		m_replaying = false;
		m_replayed = true;

		if (const auto shooter = find_player(m_shooter))
		{
			shooter->finish_replay();
		}

		follow_current_player();
	}

	// With FORTRESS_AUTOPLAY_LOG set (test runs), records how far the local
	// player's prediction was from the server's position.
	void Round::log_prediction(const uint32_t turn_no) const
	{
		char* path = nullptr;
		size_t length = 0;
		if (_dupenv_s(&path, &length, "FORTRESS_AUTOPLAY_LOG") != 0 || path == nullptr)
		{
			return;
		}

		std::ofstream log(path, std::ios::app);
		free(path);

		for (const auto& ptr : m_all_players)
		{
			if (const auto ch = ptr.lock(); ch && ch->is_localplayer())
			{
				log << "turn " << turn_no << ": local player " << ch->get_player_id() << " max deviation "
					<< ch->get_max_server_deviation() << " px, corrections " << ch->get_server_corrections()
					<< ", position (" << ch->get_position().get_x() << ", " << ch->get_position().get_y() << ")" << std::endl;
			}
		}
	}

	void Round::receive_character_state()
	{
		Network::CharacterStateMsg msg{};
		Network::CharacterStateMsg latest{};
		bool any = false;

		while (EngineHandle::get_messenger()->pop_character_state(&msg))
		{
			if (!any || msg.tick >= latest.tick)
			{
				latest = msg;
				any = true;
			}
		}

		// While replaying, the clients' own physics drops characters into
		// the new craters; the server catches up after the replay.
		if (!any || m_replaying || latest.tick <= m_replay_state_tick || latest.tick < m_state_tick)
		{
			return;
		}

		m_state_tick = latest.tick;

		const int count = std::min<int>(latest.count, static_cast<int>(std::size(latest.entries)));
		for (int i = 0; i < count; ++i)
		{
			const auto& e = latest.entries[i];
			if (const auto ch = find_player(e.pid))
			{
				if (!e.alive)
				{
					ch->set_server_hp(0.0f);
					continue;
				}

				ch->apply_server_state({e.x, e.y}, e.facing, e.pitch, e.mp, e.moving != 0);
			}
		}
	}

	void Round::update_camera()
	{
		const auto scene = Scene::SceneManager::get_active_scene().lock();
		if (!scene)
		{
			return;
		}

		const auto camera = scene->get_camera().lock();

		for (const auto& [_, prj] : m_replay_projectiles)
		{
			if (const auto object = prj.object.lock(); object && object->is_active())
			{
				camera->set_object(prj.object);
				return;
			}
		}

	}

	void Round::follow_current_player() const
	{
		const auto scene = Scene::SceneManager::get_active_scene().lock();
		if (scene && m_current_player.lock())
		{
			scene->get_camera().lock()->set_object(m_current_player);
		}
	}

	float Round::get_current_time() const
	{
		return m_curr_timeout;
	}

	int Round::get_wind_acceleration() const
	{
		return m_wind_affect;
	}

	const std::weak_ptr<ObjectBase::character>& Round::get_current_player() const
	{
		return m_current_player;
	}

	const eRoundState& Round::get_current_status() const
	{
		return m_state;
	}
}
