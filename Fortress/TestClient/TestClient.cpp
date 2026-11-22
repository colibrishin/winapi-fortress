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

	const std::map<std::string, std::function<bool()>> scenarios = {
		{"smoke", scenario_smoke},
		{"spoof-room-slot", scenario_spoof_room_slot},
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
