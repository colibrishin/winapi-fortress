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
			for (int waited = 0; waited < timeout_ms; waited += 10)
			{
				if (m_socket.find_message<T>(type, out))
				{
					return true;
				}

				Sleep(10);
			}

			return false;
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

	// Two players go through the same message sequence as Client.exe:
	// lobby, room, character, start, loading, wind, turn end. Guards the
	// normal flow against the session checks.
	bool scenario_client_flow()
	{
		constexpr RoomID room = 3;
		ScriptClient p1(60931);
		ScriptClient p2(60941);

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
		ch.ch_type = eCharacterType::MissileCharacter;
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
		if (init.player_count != 2 || init.character_type[1] != eCharacterType::MissileCharacter)
			return fail("GameInit does not carry both players and p2's character");
		if (init.character_type[0] != eCharacterType::CannonCharacter)
			return fail("p1 picked no character and should get the cannon, got " +
				std::to_string(static_cast<int>(init.character_type[0])));

		p1.send_header<LoadDoneMsg>(eMessageType::LoadDone, room, 1);
		p2.send_header<LoadDoneMsg>(eMessageType::LoadDone, room, 2);
		GameStartMsg go_start{};
		if (!p1.wait_for<GameStartMsg>(eMessageType::GameStart, &go_start)) return fail("p1 got no GameStart");
		if (!p2.wait_for<GameStartMsg>(eMessageType::GameStart, &go_start)) return fail("p2 got no GameStart");

		// Relays still reach the other player.
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

		DamageMsg damage{};
		damage.type = eMessageType::Damage;
		damage.room_id = room;
		damage.player_id = 2;
		damage.shooter_type = eCharacterType::CannonCharacter;
		damage.victim_type = eCharacterType::MissileCharacter;
		damage.prj_owner_id = 1;
		damage.prj_type = Fortress::eProjectileType::Main;
		damage.prj_position = {100.0f, 100.0f};
		damage.ch_position = {100.0f, 130.0f};
		p2.send(damage);
		DamageMsg reply{};
		if (!p2.wait_for<DamageMsg>(eMessageType::Damage, &reply)) return fail("victim got no damage reply");
		if (!p1.wait_for<DamageMsg>(eMessageType::Damage, &reply)) return fail("shooter got no damage broadcast");

		RspWindMsg wind{};
		p1.send_header<ReqWindMsg>(eMessageType::ReqWind, room, 1);
		if (!p1.wait_for<RspWindMsg>(eMessageType::RspWind, &wind)) return fail("p1 got no wind");

		p1.send_header<TurnEndMsg>(eMessageType::TurnEnd, room, 1);
		p2.send_header<TurnEndMsg>(eMessageType::TurnEnd, room, 2);
		GOMsg go{};
		if (!p1.wait_for<GOMsg>(eMessageType::GO, &go)) return fail("p1 got no GO after turn end");
		if (!p2.wait_for<GOMsg>(eMessageType::GO, &go)) return fail("p2 got no GO after turn end");

		return true;
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

	// Room messages are accepted only from a member of the room, with the
	// player id bound to the sender.
	bool scenario_room_membership()
	{
		constexpr RoomID room = 0;
		ScriptClient p1(60981);
		ScriptClient p2(60991);
		LobbyInfoMsg lobby{};
		RoomInfoMsg info{};

		p1.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 1);
		if (!p1.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby)) return fail("p1 got no LobbyInfo");
		p1.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, 1);
		if (!p1.wait_for<RoomInfoMsg>(eMessageType::RoomInfo, &info)) return fail("p1 got no RoomInfo");
		p2.send_header<LobbyJoinMsg>(eMessageType::LobbyJoin, -1, 2);
		if (!p2.wait_for<LobbyInfoMsg>(eMessageType::LobbyInfo, &lobby)) return fail("p2 got no LobbyInfo");

		const auto double_damage = [&](const PlayerID pid)
		{
			ItemMsg item{};
			item.type = eMessageType::Item;
			item.room_id = room;
			item.player_id = pid;
			item.item_type = eItemType::DoubleDamage;
			p2.send(item);
		};

		double_damage(2);  // p2 is not in the room yet
		double_damage(1);  // p2 pretends to be p1
		Sleep(300);

		p2.send_header<RoomJoinMsg>(eMessageType::RoomJoin, room, 2);
		if (!p2.wait_for<RoomInfoMsg>(eMessageType::RoomInfo, &info)) return fail("p2 got no RoomInfo");

		DebugStateRspMsg state{};
		if (!p1.request_state(room, &state)) return fail("no debug state reply");

		const auto* s1 = find_slot(state, 1);
		const auto* s2 = find_slot(state, 2);
		if (s1 == nullptr || s2 == nullptr) return fail("both players should be in the room");
		if (s2->double_damage) return fail("item from a non-member was applied");
		if (s1->double_damage) return fail("item sent with another player's id was applied");
		return true;
	}

	const std::map<std::string, std::function<bool()>> scenarios = {
		{"bad-ids", scenario_bad_ids},
		{"session-takeover", scenario_session_takeover},
		{"room-membership", scenario_room_membership},
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
