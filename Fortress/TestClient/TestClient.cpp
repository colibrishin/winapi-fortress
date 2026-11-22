#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "../Common/Socket.hpp"
#include "../Common/message.hpp"

#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "Common.lib")

namespace
{
	using namespace Fortress::Network;
	using Fortress::Network::Server::Socket;

	constexpr u_short server_port = 51211;
	constexpr int reply_timeout_ms = 2000;

	// One scripted player: its own UDP socket (so its own source port),
	// talking to the server the same way Client.exe does.
	class ScriptClient
	{
	public:
		explicit ScriptClient(const u_short preferred_port) : m_socket(preferred_port, false)
		{
			std::thread(&Socket::receiving_message, &m_socket).detach();

			m_server.sin_family = AF_INET;
			m_server.sin_port = htons(server_port);
			inet_pton(AF_INET, "127.0.0.1", &m_server.sin_addr);
		}

		u_short port() const
		{
			return m_socket.get_local_port();
		}

		template <typename T>
		void send(const T& message)
		{
			const auto stamped = create_prewritten_network_message<T>(message);
			m_socket.send_message<T>(&stamped, m_server);
		}

		template <typename T>
		void send_header(const eMessageType type, const RoomID room_id, const PlayerID player_id)
		{
			T message{};
			message.type = type;
			message.room_id = room_id;
			message.player_id = player_id;
			send(message);
		}

		template <typename T>
		bool wait_for(const eMessageType type, T* out, const int timeout_ms = reply_timeout_ms)
		{
			// Checks at least once, so timeout 0 polls.
			for (int waited = 0; ; waited += 10)
			{
				if (m_socket.find_message<T>(type, out))
				{
					return true;
				}

				if (waited >= timeout_ms)
				{
					return false;
				}

				Sleep(10);
			}
		}

		bool request_state(const RoomID room_id, DebugStateRspMsg* out)
		{
			m_socket.flush_message(eMessageType::DebugStateRsp);
			send_header<DebugStateReqMsg>(eMessageType::DebugStateReq, room_id, -1);
			return wait_for<DebugStateRspMsg>(eMessageType::DebugStateRsp, out);
		}

	private:
		Socket m_socket;
		sockaddr_in m_server{};
	};

	const DebugStateRspMsg::Slot* find_slot(const DebugStateRspMsg& state, const PlayerID pid)
	{
		for (int i = 0; i < state.slot_count; ++i)
		{
			if (state.slots[i].pid == pid)
			{
				return &state.slots[i];
			}
		}

		return nullptr;
	}

	bool fail(const std::string& reason)
	{
		std::cout << "FAIL: " << reason << std::endl;
		return false;
	}

	// The server must answer PING and the debug state request, and the
	// state must show the sender bound to its own source port.
	bool scenario_smoke()
	{
		ScriptClient player(60901);
		LobbyInfoMsg lobby{};
		player.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 1);
		if (!player.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby))
		{
			return fail("no LobbyInfo from server (is it running?)");
		}

		player.send_header<PingMsg>(eMessageType::PING, -1, 1);
		PongMsg pong{};
		if (!player.wait_for<PongMsg>(eMessageType::PONG, &pong))
		{
			return fail("no PONG from server (is it running?)");
		}

		DebugStateRspMsg state{};
		if (!player.request_state(-1, &state))
		{
			return fail("no debug state reply (was the server started with --debug-state?)");
		}

		const auto* slot = find_slot(state, 1);
		if (slot == nullptr)
		{
			return fail("player 1 is not listed in the lobby");
		}

		std::cout << "player 1 bound to port " << slot->port << ", client port " << player.port() << std::endl;
		return slot->port == player.port() || fail("player 1 is bound to another port");
	}

	// A second socket claims player 1's room slot by sending RoomJoin with
	// player_id = 1. The slot must stay bound to the original sender.
	bool scenario_spoof_room_slot()
	{
		constexpr RoomID room = 0;
		constexpr PlayerID victim = 1;

		ScriptClient owner(60911);
		ScriptClient attacker(60921);

		owner.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, victim);
		owner.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, victim);

		RoomInfoMsg room_info{};
		if (!owner.wait_for<RoomInfoMsg>(eMessageType::RoomInfo, &room_info))
		{
			return fail("owner did not get RoomInfo after joining");
		}

		DebugStateRspMsg before{};
		if (!owner.request_state(room, &before))
		{
			return fail("no debug state reply (was the server started with --debug-state?)");
		}

		const auto* owner_slot = find_slot(before, victim);
		if (owner_slot == nullptr || owner_slot->port != owner.port())
		{
			return fail("owner is not bound to its slot after joining");
		}

		attacker.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, victim);
		Sleep(300);

		DebugStateRspMsg after{};
		if (!owner.request_state(room, &after))
		{
			return fail("no debug state reply after the spoofed join");
		}

		const auto* slot = find_slot(after, victim);
		if (slot == nullptr)
		{
			return fail("player slot disappeared");
		}

		std::cout << "slot port " << slot->port << ", owner " << owner.port()
			<< ", attacker " << attacker.port() << std::endl;
		return slot->port == owner.port() || fail("spoofed RoomJoin took over the slot");
	}

	// Lobby -> room -> start -> loading with two players, the same message
	// sequence as Client.exe. p1 picks no character (gets the cannon).
	bool start_match(ScriptClient& p1, ScriptClient& p2, const RoomID room, const eCharacterType p2_type = eCharacterType::MissileCharacter)
	{
		LobbyInfoMsg lobby{};
		p1.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 1);
		if (!p1.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby)) return fail("p1 got no LobbyInfo");
		p2.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 2);
		if (!p2.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby)) return fail("p2 got no LobbyInfo");

		RoomInfoMsg info{};
		p1.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, 1);
		if (!p1.wait_for<RoomInfoMsg>(eMessageType::RoomInfo, &info)) return fail("p1 got no RoomInfo");
		p2.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, 2);
		if (!p2.wait_for<RoomInfoMsg>(eMessageType::RoomInfo, &info)) return fail("p2 got no RoomInfo");
		if (info.player_count != 2) return fail("room should list 2 players, got " + std::to_string(info.player_count));

		RoomSelectChMsg ch{};
		ch.type = eMessageType::RoomSelectCh;
		ch.room_id = room;
		ch.player_id = 2;
		ch.ch_type = p2_type;
		p2.send(ch);

		RoomStartMsg start{};
		start.type = eMessageType::RoomStart;
		start.room_id = room;
		start.player_id = 1;
		start.map_type = eMapType::DesertMap;
		p1.send(start);

		GameInitMsg init{};
		if (!p1.wait_for<GameInitMsg>(eMessageType::GameInit, &init)) return fail("p1 got no GameInit");
		if (!p2.wait_for<GameInitMsg>(eMessageType::GameInit, &init)) return fail("p2 got no GameInit");
		if (init.player_count != 2 || init.character_type[1] != p2_type)
			return fail("GameInit does not carry both players and p2's character");
		if (init.character_type[0] != eCharacterType::CannonCharacter)
			return fail("p1 picked no character and should get the cannon, got " +
				std::to_string(static_cast<int>(init.character_type[0])));

		p1.send_header<LoadDoneMsg>(eMessageType::LoadDone, room, 1);
		p2.send_header<LoadDoneMsg>(eMessageType::LoadDone, room, 2);
		GameStartMsg go_start{};
		if (!p1.wait_for<GameStartMsg>(eMessageType::GameStart, &go_start)) return fail("p1 got no GameStart");
		if (!p2.wait_for<GameStartMsg>(eMessageType::GameStart, &go_start)) return fail("p2 got no GameStart");
		return true;
	}

	// Waits for TurnChange number turn_no (acknowledging every TurnChange seen).
	bool wait_turn(ScriptClient& player, const PlayerID pid, const RoomID room, const uint32_t turn_no,
	               TurnChangeMsg* out, const int timeout_ms = reply_timeout_ms)
	{
		for (int waited = 0; waited < timeout_ms; waited += 50)
		{
			TurnChangeMsg msg{};
			while (player.wait_for<TurnChangeMsg>(eMessageType::TurnChange, &msg, 0))
			{
				TurnAckMsg ack{};
				ack.type = eMessageType::TurnAck;
				ack.room_id = room;
				ack.player_id = pid;
				ack.turn_no = msg.turn_no;
				player.send(ack);

				if (msg.turn_no == turn_no)
				{
					*out = msg;
					return true;
				}
			}
			Sleep(50);
		}
		return false;
	}

	struct ReceivedShot
	{
		std::map<int, ShotPathMsg> path;
		std::map<int, ShotEventsMsg> events;
		int path_chunks = -1;
		int event_chunks = -1;
		int duplicates = 0;

		bool complete() const
		{
			return path_chunks >= 0 && static_cast<int>(path.size()) == path_chunks &&
				static_cast<int>(events.size()) == event_chunks;
		}

		std::vector<ShotEventsMsg::Event> all_events() const
		{
			std::vector<ShotEventsMsg::Event> out;
			for (const auto& [_, chunk] : events)
			{
				out.insert(out.end(), chunk.events, chunk.events + chunk.count);
			}
			return out;
		}
	};

	// Collects the chunks of the shot of turn_no. With ack = false nothing is
	// acknowledged (to make the server resend).
	bool collect_shot(ScriptClient& player, const PlayerID pid, const RoomID room, const uint32_t turn_no,
	                  ReceivedShot& shot, const int timeout_ms = reply_timeout_ms, const bool ack = true)
	{
		const auto send_ack = [&](const bool is_events, const uint16_t chunk)
		{
			ShotAckMsg msg{};
			msg.type = eMessageType::ShotAck;
			msg.room_id = room;
			msg.player_id = pid;
			msg.turn_no = turn_no;
			msg.is_events = is_events;
			msg.chunk = chunk;
			player.send(msg);
		};

		for (int waited = 0; waited < timeout_ms; waited += 20)
		{
			ShotPathMsg path{};
			while (player.wait_for<ShotPathMsg>(eMessageType::ShotPath, &path, 0))
			{
				if (path.turn_no != turn_no) continue;
				shot.duplicates += shot.path.count(path.chunk);
				shot.path[path.chunk] = path;
				shot.path_chunks = path.path_chunks;
				shot.event_chunks = path.event_chunks;
				if (ack) send_ack(false, path.chunk);
			}

			ShotEventsMsg events{};
			while (player.wait_for<ShotEventsMsg>(eMessageType::ShotEvents, &events, 0))
			{
				if (events.turn_no != turn_no) continue;
				shot.duplicates += shot.events.count(events.chunk);
				shot.events[events.chunk] = events;
				shot.path_chunks = events.path_chunks;
				shot.event_chunks = events.event_chunks;
				if (ack) send_ack(true, events.chunk);
			}

			if (ack && shot.complete()) return true;
			Sleep(20);
		}

		return shot.complete();
	}

	// Test hook: the server (with --debug-state) puts a character somewhere.
	void place(ScriptClient& player, const RoomID room, const PlayerID target, const float x, const float y)
	{
		DebugPlaceMsg msg{};
		msg.type = eMessageType::DebugPlace;
		msg.room_id = room;
		msg.player_id = -1;
		msg.target = target;
		msg.x = x;
		msg.y = y;
		player.send(msg);
	}

	void move_input(ScriptClient& player, const PlayerID pid, const RoomID room, const uint32_t turn_no,
	                const uint32_t seq, const uint8_t input)
	{
		MoveInputMsg msg{};
		msg.type = eMessageType::MoveInput;
		msg.room_id = room;
		msg.player_id = pid;
		msg.turn_no = turn_no;
		msg.seq = seq;
		msg.input = input;
		player.send(msg);
	}

	const DebugStateRspMsg::Slot* slot_of(const DebugStateRspMsg& state, const PlayerID pid)
	{
		return find_slot(state, pid);
	}

	ShotRequestMsg shot_request(const RoomID room, const PlayerID pid, const uint32_t turn_no, const float charge,
	                            const Fortress::eProjectileType kind = Fortress::eProjectileType::Main,
	                            const eItemType item = eItemType::None)
	{
		ShotRequestMsg msg{};
		msg.type = eMessageType::ShotRequest;
		msg.room_id = room;
		msg.player_id = pid;
		msg.turn_no = turn_no;
		msg.prj_type = kind;
		msg.item = item;
		msg.charged = charge;
		return msg;
	}

	// Two players start a match; the server announces turn 1 for player 1
	// with full hp, and the movement relay still works.
	bool scenario_client_flow()
	{
		constexpr RoomID room = 3;
		ScriptClient p1(60931);
		ScriptClient p2(60941);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn)) return fail("p1 got no TurnChange 1");
		if (!wait_turn(p2, 2, room, 1, &turn)) return fail("p2 got no TurnChange 1");
		if (turn.current != 1 || turn.count != 2 || turn.hp[0] != 100.0f || turn.hp[1] != 100.0f)
			return fail("turn 1 should belong to player 1 with both at 100 hp");

		PositionMsg position{};
		position.type = eMessageType::Position;
		position.room_id = room;
		position.player_id = 1;
		position.object_type = eObjectType::Character;
		position.position = {123.0f, 45.0f};
		p1.send(position);
		PositionMsg relayed{};
		if (!p2.wait_for<PositionMsg>(eMessageType::Position, &relayed) || relayed.player_id != 1 ||
			relayed.position.get_x() != 123.0f)
			return fail("p2 did not receive p1's position");

		return true;
	}

	// Player 1 fires: both players receive the whole shot (path + events),
	// then turn 2 starts for player 2.
	bool scenario_shot_flow()
	{
		constexpr RoomID room = 4;
		ScriptClient p1(61001);
		ScriptClient p2(61011);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn) || !wait_turn(p2, 2, room, 1, &turn)) return fail("no turn 1");

		// Standing on the desert stairways (top surface y = -50).
		Sleep(200);

		p1.send(shot_request(room, 1, 1, 120.0f));

		ReceivedShot s1, s2;
		if (!collect_shot(p1, 1, room, 1, s1)) return fail("p1 did not get the whole shot");
		if (!collect_shot(p2, 2, room, 1, s2)) return fail("p2 did not get the whole shot");

		bool exploded = false;
		for (const auto& e : s2.all_events()) exploded = exploded || e.type == 1;  // Explosion
		if (!exploded) return fail("shot has no explosion event");
		if (s1.path.at(0).samples[0].tick != 0) return fail("path does not start at tick 0");

		const int end_tick = s2.path.at(0).end_tick;
		const int wait_ms = end_tick * 1000 / 60 + 4000;
		if (!wait_turn(p1, 1, room, 2, &turn, wait_ms) || !wait_turn(p2, 2, room, 2, &turn, 1000))
			return fail("no turn 2 after the shot");
		if (turn.current != 2) return fail("turn 2 should belong to player 2");
		return true;
	}

	// A direct hit: the damage in the shot events is applied by the server
	// and announced in the next TurnChange.
	bool scenario_direct_hit()
	{
		constexpr RoomID room = 5;
		ScriptClient p1(61021);
		ScriptClient p2(61031);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn) || !wait_turn(p2, 2, room, 1, &turn)) return fail("no turn 1");

		// Middle ground top is y = 50; p2 stands right in front of p1.
		place(p1, room, 1, 0.0f, 25.0f);
		place(p1, room, 2, 100.0f, 25.0f);
		Sleep(200);
		p1.send(shot_request(room, 1, 1, 20.0f, Fortress::eProjectileType::Sub));

		ReceivedShot shot;
		if (!collect_shot(p2, 2, room, 1, shot)) return fail("p2 did not get the shot");
		collect_shot(p1, 1, room, 1, shot, 500);

		float damage = 0.0f;
		for (const auto& e : shot.all_events())
		{
			if (e.type == 2 && e.player == 2) damage += e.value;  // Damage
		}
		if (damage <= 0.0f) return fail("no damage to player 2 in the shot events");

		if (!wait_turn(p2, 2, room, 2, &turn, 5000)) return fail("no turn 2");
		const float hp = turn.players[0] == 2 ? turn.hp[0] : turn.hp[1];
		std::cout << "damage " << damage << ", p2 hp " << hp << std::endl;
		return std::fabs(hp - (100.0f - damage)) < 1e-3f || fail("TurnChange hp does not match the damage");
	}

	// Nobody but the current player can fire, and nobody can fire for
	// someone else; the old client-reported Damage message is ignored.
	bool scenario_out_of_turn()
	{
		constexpr RoomID room = 6;
		ScriptClient p1(61041);
		ScriptClient p2(61051);
		ScriptClient p3(61061);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn) || !wait_turn(p2, 2, room, 1, &turn)) return fail("no turn 1");

		p2.send(shot_request(room, 2, 1, 100.0f));  // not p2's turn
		p2.send(shot_request(room, 1, 1, 100.0f)); // pretends to be p1

		LobbyInfoMsg lobby{};
		p3.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 3);
		p3.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby);
		p3.send(shot_request(room, 3, 1, 100.0f));      // not in the room

		DamageMsg damage{};
		damage.type = eMessageType::Damage;
		damage.room_id = room;
		damage.player_id = 2;
		damage.prj_owner_id = 1;
		damage.damage = 99.0f;
		p2.send(damage);

		ReceivedShot shot;
		if (collect_shot(p1, 1, room, 1, shot, 1000)) return fail("a shot was simulated for a rejected request");

		DebugStateRspMsg state{};
		if (!p1.request_state(room, &state)) return fail("no debug state reply");
		if (state.turn_no != 1 || state.current_player != 1) return fail("turn changed after rejected requests");
		for (int i = 0; i < state.slot_count; ++i)
		{
			if (state.slots[i].hp != 100.0f) return fail("hp changed after rejected requests");
		}
		return true;
	}

	// If a client does not acknowledge, the server resends the chunks.
	bool scenario_shot_resend()
	{
		constexpr RoomID room = 7;
		ScriptClient p1(61071);
		ScriptClient p2(61081);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn) || !wait_turn(p2, 2, room, 1, &turn)) return fail("no turn 1");
		Sleep(200);
		p1.send(shot_request(room, 1, 1, 120.0f));

		ReceivedShot silent;
		collect_shot(p2, 2, room, 1, silent, 1200, false);  // receive without acking
		if (silent.duplicates == 0) return fail("server did not resend unacknowledged chunks");

		ReceivedShot shot;
		if (!collect_shot(p2, 2, room, 1, shot, 2000)) return fail("chunks stopped arriving");
		std::cout << "duplicates while silent: " << silent.duplicates << std::endl;
		return true;
	}

	// Clients cannot report deaths any more; the server sees a character
	// fall out of the map and, with one player left, ends the match.
	bool scenario_fall_death()
	{
		constexpr RoomID room = 8;
		ScriptClient p1(61091);
		ScriptClient p2(61101);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn) || !wait_turn(p2, 2, room, 1, &turn)) return fail("no turn 1");

		DestroyedMsg fake{};
		fake.type = eMessageType::Destroyed;
		fake.room_id = room;
		fake.player_id = 1;
		fake.object_type = eObjectType::Character;
		fake.object_player_id = 1;
		p1.send(fake);  // ignored: the server decides falls
		Sleep(500);

		DebugStateRspMsg state{};
		if (!p1.request_state(room, &state)) return fail("no debug state reply");
		if (!slot_of(state, 1) || !slot_of(state, 1)->alive) return fail("a client death report was accepted");

		place(p1, room, 2, 2000.0f, -75.0f);  // nothing below
		if (!wait_turn(p1, 1, room, 2, &turn, 15000)) return fail("no TurnChange after the fall");
		if (turn.current != -1 || turn.winner != 1) return fail("player 1 should have won (current " +
			std::to_string(turn.current) + ", winner " + std::to_string(turn.winner) + ")");
		return true;
	}

	// Characters start where BattleScene::spawnpoints puts them.
	bool scenario_spawn()
	{
		constexpr RoomID room = 10;
		ScriptClient p1(61131);
		ScriptClient p2(61141);
		if (!start_match(p1, p2, room)) return false;
		Sleep(500);

		DebugStateRspMsg state{};
		if (!p1.request_state(room, &state)) return fail("no debug state reply");
		const auto* s1 = slot_of(state, 1);
		const auto* s2 = slot_of(state, 2);
		if (!s1 || !s2 || !s1->in_game || !s2->in_game) return fail("players missing from the match");
		std::cout << "p1 (" << s1->x << ", " << s1->y << "), p2 (" << s2->x << ", " << s2->y << ")" << std::endl;
		const bool p1_ok = std::fabs(s1->x - -400.0f) < 0.01f && std::fabs(s1->y - -75.0f) < 1.0f;
		const bool p2_ok = std::fabs(s2->x - 275.0f) < 0.01f && std::fabs(s2->y - -75.0f) < 1.0f;
		return (p1_ok && p2_ok) || fail("spawn positions differ from the client's");
	}

	// The current player walks with MoveInput; the other player's input and
	// stale inputs are ignored. CharacterState reports the positions.
	bool scenario_walk()
	{
		constexpr RoomID room = 11;
		ScriptClient p1(61151);
		ScriptClient p2(61161);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn) || !wait_turn(p2, 2, room, 1, &turn)) return fail("no turn 1");
		Sleep(300);

		DebugStateRspMsg before{};
		if (!p1.request_state(room, &before)) return fail("no debug state reply");

		for (uint32_t seq = 1; seq <= 20; ++seq)
		{
			move_input(p1, 1, room, 1, seq, 2);  // right, repeated like the client
			if (seq == 5)
			{
				move_input(p2, 2, room, 1, 100, 1);  // left from p2: not p2's turn
			}
			Sleep(100);
		}
		move_input(p1, 1, room, 1, 5, 0);   // stale (seq 5 < 20): ignored
		Sleep(300);

		DebugStateRspMsg walking{};
		if (!p1.request_state(room, &walking)) return fail("no debug state reply");
		move_input(p1, 1, room, 1, 21, 0);  // release
		Sleep(500);
		DebugStateRspMsg after{};
		if (!p1.request_state(room, &after)) return fail("no debug state reply");
		Sleep(300);
		DebugStateRspMsg later{};
		if (!p1.request_state(room, &later)) return fail("no debug state reply");

		const float walked = slot_of(after, 1)->x - slot_of(before, 1)->x;
		std::cout << "p1 walked " << walked << " px in ~2.3 s" << std::endl;
		if (walked < 30.0f || walked > 60.0f) return fail("p1 should walk about 20 px/s");
		if (slot_of(walking, 1)->x <= slot_of(before, 1)->x + 30.0f) return fail("stale input stopped the walk");
		if (std::fabs(slot_of(later, 1)->x - slot_of(after, 1)->x) > 0.01f) return fail("p1 kept walking after release");
		if (std::fabs(slot_of(after, 2)->x - slot_of(before, 2)->x) > 0.01f) return fail("p2 moved on p1's turn");

		CharacterStateMsg cs{};
		if (!p2.wait_for<CharacterStateMsg>(eMessageType::CharacterState, &cs)) return fail("no CharacterState");
		return true;
	}

	// The server ends a turn after 60 s without a shot.
	bool scenario_turn_timeout()
	{
		constexpr RoomID room = 9;
		ScriptClient p1(61111);
		ScriptClient p2(61121);
		if (!start_match(p1, p2, room)) return false;

		TurnChangeMsg turn{};
		if (!wait_turn(p1, 1, room, 1, &turn) || !wait_turn(p2, 2, room, 1, &turn)) return fail("no turn 1");

		std::cout << "waiting for the 60 s turn limit..." << std::endl;
		for (int i = 0; i < 58; ++i)
		{
			p1.send_header<PingMsg>(eMessageType::PING, -1, 1);
			p2.send_header<PingMsg>(eMessageType::PING, -1, 2);
			Sleep(1000);
		}
		if (!wait_turn(p2, 2, room, 2, &turn, 5000)) return fail("turn did not time out");
		return turn.current == 2 || fail("turn 2 should belong to player 2");
	}

	// Ids out of range, unknown player ids and out-of-range indices inside
	// messages must not crash the server or touch memory outside its tables.
	bool scenario_bad_ids()
	{
		ScriptClient player(60951);

		player.send_header<PingMsg>(eMessageType::PING, 99, 99);
		player.send_header<TurnEndMsg>(eMessageType::TurnEnd, -1, 7);
		player.send_header<LoadDoneMsg>(eMessageType::LoadDone, 14, 14);

		// Player 7 has no fixed name (the server used to throw on that).
		LobbyInfoMsg lobby{};
		player.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 7);
		if (!player.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby)) return fail("player 7 got no LobbyInfo");

		RoomInfoMsg info{};
		player.send_header<RoomJoinMsg>(eMessageType::RoomJoin, 1, 7);
		if (!player.wait_for<RoomInfoMsg>(eMessageType::RoomInfo, &info)) return fail("player 7 got no RoomInfo");

		RoomSelectItMsg item{};
		item.type = eMessageType::RoomSelectIt;
		item.room_id = 1;
		item.player_id = 7;
		item.index = 1000;
		item.item_type = eItemType::Repair;
		player.send(item);

		DamageMsg damage{};
		damage.type = eMessageType::Damage;
		damage.room_id = 1;
		damage.player_id = 7;
		damage.prj_owner_id = 100000;
		player.send(damage);

		// Bound player, ids out of range: must be rejected by the range check.
		player.send_header<TurnEndMsg>(eMessageType::TurnEnd, 15, 7);
		player.send_header<TurnEndMsg>(eMessageType::TurnEnd, -2, 7);
		player.send_header<LoadDoneMsg>(eMessageType::LoadDone, 1000000, 7);

		// PING carries a room id but must not put the sender into that room.
		player.send_header<PingMsg>(eMessageType::PING, 5, 7);

		Sleep(300);
		DebugStateRspMsg state{};
		if (!player.request_state(5, &state)) return fail("no debug state reply");
		if (state.slot_count != 0) return fail("PING with a room id joined that room");

		player.send_header<PingMsg>(eMessageType::PING, -1, 7);
		PongMsg pong{};
		return player.wait_for<PongMsg>(eMessageType::PONG, &pong) || fail("server stopped answering");
	}

	// A player id stays with the address that claimed it while that address
	// keeps talking (Client.exe pings every second). Another address can
	// neither claim it nor take its room slot. After 30 s of silence the
	// binding is released together with the player's room entry.
	bool scenario_session_takeover()
	{
		constexpr RoomID room = 0;
		ScriptClient owner(60961);
		ScriptClient other(60971);
		LobbyInfoMsg lobby{};
		RoomInfoMsg info{};
		DebugStateRspMsg state{};

		owner.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 1);
		if (!owner.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby)) return fail("owner got no LobbyInfo");
		owner.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, 1);
		if (!owner.wait_for<RoomInfoMsg>(eMessageType::RoomInfo, &info)) return fail("owner got no RoomInfo");

		// While the owner pings, the other address keeps trying to claim the
		// id and its room slot.
		for (int second = 0; second < 8; ++second)
		{
			owner.send_header<PingMsg>(eMessageType::PING, -1, 1);
			other.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 1);
			other.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, 1);
			Sleep(1000);
		}

		if (!owner.request_state(room, &state)) return fail("no debug state reply");
		const auto* slot = find_slot(state, 1);
		if (slot == nullptr || slot->port != owner.port()) return fail("an active player's room slot was taken over");

		std::cout << "owner goes silent for the 30 s session timeout..." << std::endl;
		Sleep(31000);

		other.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 1);
		if (!other.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby)) return fail("silent binding was not released");

		if (!other.request_state(room, &state)) return fail("no debug state reply");
		if (find_slot(state, 1) != nullptr) return fail("released player is still in the room");

		if (!other.request_state(-1, &state)) return fail("no debug state reply");
		slot = find_slot(state, 1);
		return (slot != nullptr && slot->port == other.port()) || fail("player 1 not rebound to the new address");
	}

	const std::map<std::string, std::function<bool()>> scenarios = {
		{"bad-ids", scenario_bad_ids},
		{"session-takeover", scenario_session_takeover},
		{"shot-flow", scenario_shot_flow},
		{"direct-hit", scenario_direct_hit},
		{"out-of-turn", scenario_out_of_turn},
		{"shot-resend", scenario_shot_resend},
		{"fall-death", scenario_fall_death},
		{"spawn", scenario_spawn},
		{"walk", scenario_walk},
		{"turn-timeout", scenario_turn_timeout},
		{"smoke", scenario_smoke},
		{"spoof-room-slot", scenario_spoof_room_slot},
		{"client-flow", scenario_client_flow},
	};

	[[noreturn]] void ping_forever()
	{
		Socket soc{60901, false};

		sockaddr_in server{};
		server.sin_family = AF_INET;
		server.sin_port = htons(server_port);
		inet_pton(PF_INET, "127.0.0.1", &server.sin_addr);

		while (true)
		{
			auto message = create_network_message<PingMsg>(eMessageType::PONG, 0, 0);
			soc.send_message<PingMsg>(&message, server);
			std::cout << "Sending Packets..." << std::endl;
			Sleep(0x000000ff);
		}
	}
}

// Usage:
//   TestClient                    send PONG packets forever (original behaviour)
//   TestClient scenario <name>    run one scripted scenario; exit code 0 = PASS, 1 = FAIL, 2 = usage
int main(const int argc, char** argv)
{
	if (argc < 2)
	{
		ping_forever();
	}

	if (argc != 3 || std::string(argv[1]) != "scenario" || scenarios.find(argv[2]) == scenarios.end())
	{
		std::cout << "usage: TestClient [scenario <name>]\nscenarios:";
		for (const auto& [name, _] : scenarios)
		{
			std::cout << ' ' << name;
		}
		std::cout << std::endl;
		return 2;
	}

	const bool passed = scenarios.at(argv[2])();
	std::cout << (passed ? "PASS " : "FAIL ") << argv[2] << std::endl;

	// Skip static/socket teardown: the receiving threads are still blocked
	// in recvfrom and the scenario result is already decided.
	std::cout.flush();
	ExitProcess(passed ? 0 : 1);
}
