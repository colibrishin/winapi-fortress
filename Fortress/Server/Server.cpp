#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../Common/Socket.hpp"
#include "../Common/message.hpp"
#include "../Common/vector2.hpp"
#include "../Common/message.hpp"

#include "../Common/TimerManager.hpp"
#include "../Common/deltatime.hpp"
#include "../Common/sceneManager.hpp"
#include "../Common/resourceManager.hpp"

#include "ClientSide.hpp"
#include "RoomGame.hpp"

#include <filesystem>

#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "Common.lib")

namespace Fortress::Network::Server
{
	static Socket server_socket = Socket(51211, true);

	const sockaddr* extract_ip(const SOCKADDR_IN& client_info)
	{
		return reinterpret_cast<const sockaddr*>(&client_info);
	}

	struct Client
	{
		SOCKADDR_IN ip;
		PlayerID pid;
		std::time_t last_contact;

		struct RoomInfo
		{
			eCharacterType character{};
			eItemType items[4]{};
			float delta_time;
		} room_info;
	};

	const std::map<PlayerID, std::wstring> client_names = {{1, L"Test1"}, {2, L"Test2"}};

	// Room and player ids index fixed arrays below. -1 is the lobby.
	constexpr int max_rooms = 15;
	constexpr int max_players = 15;

	std::wstring player_name(const PlayerID pid)
	{
		const auto it = client_names.find(pid);
		return it != client_names.end() ? it->second : L"Player" + std::to_wstring(pid);
	}

	static std::mutex player_list_lock;
	static bool player_list[15]{ false, };
	static bool load_done[15][15] { false, };

	// Running matches, one per room, and the GameInit each was started with.
	static RoomGame room_games[15];
	static GameInitMsg room_init[15];

	// Path of the cloud mask BMP for SkyValley (see find_cloud_mask).
	static std::string cloud_mask_path;

	static std::map<std::pair<RoomID, PlayerID>, Client> client_list = {};

	// Set by --debug-state. Lets the scripted test client inspect state.
	static bool debug_state_enabled = false;

	unsigned int get_room_player_count(RoomID room_id)
	{
		int player_count = 0;

		for (int i = 0; i < 15; ++i)
		{
			if (client_list.find({room_id, i}) != client_list.end())
			{
				player_count++;
			}
		}

		return player_count;
	}

	std::vector<Client> get_room_client(RoomID room_id)
	{
		std::vector<Client> clients;

		for(int i = 0; i < 15; ++i)
		{
			if(client_list.find({room_id, i}) != client_list.end())
			{
				clients.push_back(client_list[{room_id, i}]);
			}
		}

		return clients;
	}

	std::vector<Client> get_lobby_client()
	{
		std::vector<Client> clients;

		for(int i = 0; i < 15; ++i)
		{
			if(client_list.find({-1, i}) != client_list.end())
			{
				clients.push_back(client_list[{-1, i}]);
			}
		}

		return clients;
	}

	void add_client(RoomID id, PlayerID pid, const SOCKADDR_IN& client_info, const std::time_t time)
	{
		client_list[{id, pid}] = {client_info, pid, time};
	}

	void reply_lobby_info(const std::vector<Client>& clients, const SOCKADDR_IN& client_info)
	{
		wchar_t player_names[15][15]{};
		int pos = 0;

		{
			std::lock_guard _(player_list_lock);

			for(const auto& client : clients)
			{
				const std::wstring name = player_name(client.pid).substr(0, 14);
				std::wmemcpy(player_names[pos], name.c_str(), name.length());
				pos++;
			}
		}

		LobbyInfoMsg li{{0, eMessageType::LobbyInfo, -1, -1},
			0, {}, pos, {}};

		std::wmemcpy(li.player_names[0], player_names[0], 15 * 15);

		auto reply = create_prewritten_network_message<LobbyInfoMsg>(li);
		server_socket.send_message(&reply, client_info);
	}

	void reply_ping(const SOCKADDR_IN& client_info)
	{
		auto reply = create_network_message<PongMsg>(
			eMessageType::PONG, -1, -1);
		server_socket.send_message<PongMsg>(&reply, client_info);
	}

	void broadcast_lobby_info()
	{
		const auto clients = get_lobby_client();
		for(const auto& client : clients)
		{
			reply_lobby_info(clients, client.ip);
		}
	}

	void add_player(PlayerID player_id)
	{
		std::cout << "Add to lobby player list..." << std::endl;
		{
			std::lock_guard _(player_list_lock);
			player_list[player_id] = true;
		}
	}

	void remove_client_from_lobby(PlayerID player_id)
	{
		std::cout << player_id << " Leaves Lobby" << std::endl;
		client_list.erase({-1, player_id});
	}

	void add_player_to_room(RoomID room_id, PlayerID player_id, const Client& client)
	{
		std::cout << player_id << " Joined " << room_id << std::endl;

		// A player is in one room at a time.
		for (RoomID other = 0; other < max_rooms; ++other)
		{
			client_list.erase({other, player_id});
		}

		client_list[{room_id, player_id}] = client;
	}

	void send_room_info(const RoomID room_id, const sockaddr_in& client_info)
	{
		std::cout << "Sending Room info to new client" << std::endl;
		RoomInfoMsg rif{};

		int count = 0;
		const auto room_clients = get_room_client(room_id);

		for(const auto& client : room_clients)
		{
			const std::wstring name = player_name(client.pid).substr(0, 14);
			std::wmemcpy(rif.player_names[count], name.c_str(), name.length());
			count++;
		}

		rif.type = eMessageType::RoomInfo;
		rif.player_count = count;
		rif.player_id = -1;
		rif.room_id = room_id;

		auto msg = create_prewritten_network_message<RoomInfoMsg>(rif);
		server_socket.send_message(&msg, client_info);
	}

	void notify_join(RoomID room_id)
	{
		std::cout << "Broadcasting new client to pre-existing clients" << std::endl;
		RoomInfoMsg rif{};

		int count = 0;
		const auto room_clients = get_room_client(room_id);

		for(const auto& client : room_clients)
		{
			const std::wstring name = player_name(client.pid).substr(0, 14);
			std::wmemcpy(rif.player_names[count], name.c_str(), name.length());
			count++;
		}

		rif.type = eMessageType::RoomInfo;
		rif.player_count = count;
		rif.player_id = -1;
		rif.room_id = room_id;

		const auto msg = create_prewritten_network_message<RoomInfoMsg>(rif);

		for(int i = 0; i < 15; ++i)
		{
			if(client_list.find({room_id, i}) != client_list.end())
			{
				server_socket.send_message(&msg, client_list[{room_id, i}].ip);
			}
		}
	}

	void change_character(const Message* message)
	{
		const RoomID room_id = message->room_id;
		const PlayerID player_id = message->player_id;
		const eCharacterType ch_type = reinterpret_cast<const RoomSelectChMsg*>(message)->ch_type;

		client_list[{room_id, player_id}].room_info.character = ch_type;
	}

	void change_items(const Message* message)
	{
		std::cout << "Equip item : " << message->player_id;
		const RoomID room_id = message->room_id;
		const PlayerID player_id = message->player_id;
		const unsigned int it_idx = reinterpret_cast<const RoomSelectItMsg*>(message)->index;
		const eItemType it_type = reinterpret_cast<const RoomSelectItMsg*>(message)->item_type;
		std::cout << static_cast<int>(it_type) << std::endl;

		if (it_idx >= std::size(client_list[{room_id, player_id}].room_info.items))
		{
			return;
		}

		client_list[{room_id, player_id}].room_info.items[it_idx] = it_type;
	}

	void aggregate_send_room_info(const Message* message)
	{
		std::cout << "Room start received, Aggregates room info" << std::endl;
		const auto* recv_msg = reinterpret_cast<const RoomStartMsg*>(message);
		const RoomID room_id = recv_msg->room_id;
		const eMapType map = recv_msg->map_type;
		GameInitMsg gi{};
		uint8_t pos = 0;
		const auto room_clients = get_room_client(room_id);

		for(const auto& client : room_clients)
		{
			gi.players[pos] = client.pid;

			// A player who never picked a character has a zero-initialised
			// value, which is not eCharacterType::None; the client then created
			// no character and crashed. Anything that is not a playable type
			// becomes the cannon.
			switch (client.room_info.character)
			{
			case eCharacterType::CannonCharacter:
			case eCharacterType::MissileCharacter:
			case eCharacterType::SecwindCharacter:
				gi.character_type[pos] = client.room_info.character;
				break;
			default:
				gi.character_type[pos] = eCharacterType::CannonCharacter;
				break;
			}

			std::copy(std::begin(client.room_info.items), std::end(client.room_info.items), gi.equied_item[pos]);
			pos++;
		}

		gi.type = eMessageType::GameInit;
		gi.map_type = map;
		gi.player_count = pos;
		gi.player_id = -1;
		gi.room_id = room_id;
		
		std::cout << "Room ID: " << gi.room_id << std::endl;
		std::cout << "Player Count: " << static_cast<int>(gi.player_count) << std::endl;
		std::cout << "Map: " << static_cast<int>(gi.map_type) << std::endl;

		room_init[room_id] = gi;
		const auto msg = create_prewritten_network_message<GameInitMsg>(gi);

		for(const auto& client : room_clients)
		{
			server_socket.send_message<GameInitMsg>(&msg, client.ip);
		}
	}

	void request_deltatime(RoomID room_id, const Message* message, const sockaddr_in& client_info)
	{
		auto msg = create_network_message<ReqDeltaTimeMsg>(
			eMessageType::ReqDeltaTime, -1, room_id, message->crc32);
		server_socket.send_message<ReqDeltaTimeMsg>(&msg, client_info);
	}

	void set_delta_time(const Message* message)
	{
		const RoomID room_id = message->room_id;
		const PlayerID player_id = message->player_id;
		const float delta_time = reinterpret_cast<const DeltaTimeMsg*>(message)->deltaTime;

		client_list[{room_id, player_id}].room_info.delta_time = delta_time;
	}

	bool check_deltatime_aggregation(RoomID room_id)
	{
		const unsigned int player_count = get_room_player_count(room_id);
		const auto clients = get_room_client(room_id);

		unsigned int dt_count = 0;

		for(const auto& client : clients)
		{
			dt_count += (client.room_info.delta_time != 0.0f);
		}

		return player_count == dt_count;
	}

	void game_gtg(RoomID room_id)
	{
		std::cout << "All Players are ready, game is good to go" << std::endl;

		const auto clients = get_room_client(room_id);
		auto msg = create_network_message<GameStartMsg>(
			eMessageType::GameStart, -1, room_id);

		for(const auto& client : clients)
		{
			server_socket.send_message<GameStartMsg>(&msg, client.ip);
		}

		if (!room_games[room_id].start(room_id, room_init[room_id], cloud_mask_path, std::chrono::steady_clock::now()))
		{
			std::cout << "Could not build the map for room " << room_id << " (cloud mask: " << cloud_mask_path << ")" << std::endl;
		}

		std::fill(std::begin(load_done[room_id]), std::end(load_done[room_id]), false);
	}

	// Sends what the room's game queued, to each player's bound address.
	void flush_room_game(const RoomID room_id)
	{
		for (auto& [target, payload] : room_games[room_id].take_outbox())
		{
			const auto member = client_list.find({room_id, target});
			if (member == client_list.end())
			{
				continue;
			}

			std::visit([&](const auto& msg) { server_socket.send_message(&msg, member->second.ip); }, payload);
		}
	}

	void tick_room_games()
	{
		const auto now = std::chrono::steady_clock::now();
		for (RoomID room = 0; room < max_rooms; ++room)
		{
			if (room_games[room].active())
			{
				room_games[room].tick(now);
				flush_room_game(room);
			}
		}
	}

	// The cloud mask lives with the client's resources. Look next to the
	// server first, then in the source tree (x64/<config> -> Client).
	std::string find_cloud_mask(const std::string& resources)
	{
		const std::string relative = "misc/tiles/cloud-mask.bmp";
		const std::vector<std::string> candidates = {
			resources.empty() ? std::string() : resources + "/" + relative,
			"./resources/" + relative,
			"../../Client/resources/" + relative,
		};

		for (const auto& candidate : candidates)
		{
			if (!candidate.empty() && std::filesystem::exists(candidate))
			{
				return candidate;
			}
		}

		return candidates.back();
	}

	bool check_priority(RoomID room_id, PlayerID player_id)
	{
		const auto room_clients = get_room_client(room_id);
		const auto min = std::min_element(room_clients.begin(), room_clients.end(), [](const Client& l, const Client& r)
		{
			return l.pid < r.pid;
		});

		if(min == room_clients.end())
		{
			return false;
		}

		return player_id == min->pid;
	}

	bool add_load_done(const Message* message)
	{
		const auto clients = get_room_client(message->room_id);
		int load_finished = 0;
		load_done[message->room_id][message->player_id] = true;

		for(const auto& client: clients)
		{
			if(load_done[message->room_id][client.pid])
			{
				load_finished++;
			}
		}

		return load_finished == clients.size();
	}

	template <typename T>
	void broadcast(const Message* message)
	{
		std::cout << message->room_id << " -> " << message->player_id << " : Received." << std::endl;
		const auto clients = get_room_client(message->room_id);
		const auto* casted_msg = reinterpret_cast<const T*>(message);

		for(const auto& client : clients)
		{
			if(client.pid != message->player_id)
			{
				server_socket.send_message<T>(casted_msg, client.ip);
			}
		}
	}

	void reply_debug_state(const Message* message, const sockaddr_in& client_info)
	{
		const RoomID room_id = message->room_id;
		const bool is_game_room = room_id >= 0 && room_id < 15;

		if (!is_game_room && room_id != -1)
		{
			return;
		}

		DebugStateRspMsg rsp{};
		rsp.type = eMessageType::DebugStateRsp;
		rsp.room_id = room_id;
		rsp.player_id = -1;
		rsp.current_player = -1;

		const RoomGame* game = is_game_room && room_games[room_id].active() ? &room_games[room_id] : nullptr;
		if (game != nullptr)
		{
			rsp.current_player = game->current();
			rsp.turn_no = game->turn_no();
		}

		for (const auto& [key, client] : client_list)
		{
			if (key.first != room_id || rsp.slot_count >= std::size(rsp.slots))
			{
				continue;
			}

			auto& slot = rsp.slots[rsp.slot_count++];
			slot.pid = key.second;
			slot.address = client.ip.sin_addr.s_addr;
			slot.port = ntohs(client.ip.sin_port);

			if (game != nullptr)
			{
				for (const auto& ch : game->world().characters)
				{
					if (ch.id == key.second)
					{
						slot.in_game = 1;
						slot.alive = ch.alive;
						slot.hp = ch.hp;
						slot.x = ch.position.x;
						slot.y = ch.position.y;
					}
				}
			}
		}

		const auto reply = create_prewritten_network_message<DebugStateRspMsg>(rsp);
		server_socket.send_message<DebugStateRspMsg>(&reply, client_info);
	}


	// Which source address speaks for which player. A player id is claimed
	// by PING or LobbyJoin from an address and stays bound to it while that
	// address keeps talking; every other message must come from the bound
	// address with the same player id, so a client cannot act as someone else.
	struct Session
	{
		PlayerID pid;
		std::chrono::steady_clock::time_point last_seen;
	};

	// A binding is released after this long without any message from its
	// address. Client.exe pings every second once it is in the lobby.
	constexpr auto session_timeout = std::chrono::seconds(30);

	static std::map<uint64_t, Session> sessions;
	static std::map<PlayerID, uint64_t> session_by_pid;

	uint64_t address_key(const sockaddr_in& address)
	{
		return static_cast<uint64_t>(address.sin_addr.s_addr) << 16 | ntohs(address.sin_port);
	}

	// Forgets a binding together with everything the player had joined, so
	// the old address stops receiving broadcasts and rooms do not wait for it.
	void release_session(const uint64_t key)
	{
		const auto it = sessions.find(key);
		if (it == sessions.end())
		{
			return;
		}

		const PlayerID pid = it->second.pid;

		for (auto entry = client_list.begin(); entry != client_list.end();)
		{
			const bool owned = entry->first.second == pid && address_key(entry->second.ip) == key;
			if (owned && entry->first.first >= 0)
			{
				load_done[entry->first.first][pid] = false;
			}
			entry = owned ? client_list.erase(entry) : std::next(entry);
		}

		const auto owner = session_by_pid.find(pid);
		if (owner != session_by_pid.end() && owner->second == key)
		{
			session_by_pid.erase(owner);
		}

		sessions.erase(it);
		std::cout << "Released player " << pid << std::endl;
	}

	void expire_sessions()
	{
		const auto now = std::chrono::steady_clock::now();
		std::vector<uint64_t> expired;

		for (const auto& [key, session] : sessions)
		{
			if (now - session.last_seen >= session_timeout)
			{
				expired.push_back(key);
			}
		}

		for (const uint64_t key : expired)
		{
			release_session(key);
		}
	}

	bool is_client_message(const eMessageType type)
	{
		switch (type)
		{
		case eMessageType::PING:
		case eMessageType::LobbyJoin:
		case eMessageType::RoomJoin:
		case eMessageType::RoomSelectCh:
		case eMessageType::RoomSelectIt:
		case eMessageType::RoomStart:
		case eMessageType::LoadDone:
		case eMessageType::DeltaTime:
		case eMessageType::Position:
		case eMessageType::Stop:
		case eMessageType::Firing:
		case eMessageType::ProjectileSelect:
		case eMessageType::Destroyed:
		case eMessageType::ShotRequest:
		case eMessageType::ShotAck:
		case eMessageType::TurnAck:
		case eMessageType::PositionReport:
		case eMessageType::DebugStateReq:
			return true;
		default:
			return false;
		}
	}

	// Decides whether a message may be processed. Checks the ids are in
	// range, binds or verifies the sender's player id, and requires
	// room-scoped messages to come from a member of that room.
	bool authorize(const Message* message, const sockaddr_in& from)
	{
		const eMessageType type = message->type;
		const PlayerID pid = message->player_id;
		const RoomID room = message->room_id;

		if (!is_client_message(type))
		{
			return false;
		}

		if (type == eMessageType::DebugStateReq)
		{
			return debug_state_enabled;
		}

		if (pid < 0 || pid >= max_players || room < -1 || room >= max_rooms)
		{
			std::cout << "Rejected: id out of range (room " << room << ", player " << pid << ")" << std::endl;
			return false;
		}

		const auto now = std::chrono::steady_clock::now();
		const uint64_t key = address_key(from);
		const auto session = sessions.find(key);
		const bool claims = type == eMessageType::PING || type == eMessageType::LobbyJoin;

		if (claims && room != -1)
		{
			std::cout << "Rejected: PING/LobbyJoin must use the lobby room id" << std::endl;
			return false;
		}

		if (session != sessions.end() && session->second.pid == pid)
		{
			session->second.last_seen = now;
		}
		else if (claims)
		{
			// Only a free player id can be claimed; a holder that went silent
			// was already released by expire_sessions().
			if (session_by_pid.find(pid) != session_by_pid.end())
			{
				std::cout << "Rejected: player " << pid << " is bound to another address" << std::endl;
				return false;
			}

			release_session(key);  // this address switches to another player id
			sessions[key] = {pid, now};
			session_by_pid[pid] = key;
		}
		else
		{
			std::cout << "Rejected: message type 0x" << std::hex << static_cast<int>(type) << std::dec
				<< " for player " << pid << " from an address not bound to it" << std::endl;
			return false;
		}

		if (claims)
		{
			return true;
		}

		if (room < 0)
		{
			std::cout << "Rejected: room message without a room" << std::endl;
			return false;
		}

		if (type == eMessageType::RoomJoin)
		{
			return true;
		}

		const auto member = client_list.find({room, pid});
		if (member == client_list.end() || address_key(member->second.ip) != key)
		{
			std::cout << "Rejected: player " << pid << " is not in room " << room << std::endl;
			return false;
		}

		return true;
	}

	// Send/receive errors are not treated as a disconnect: one bad packet
	// must not lock a player out. Silent players are released by
	// expire_sessions() instead.
	void drop_bad_clients()
	{
		std::lock_guard _(server_socket.bad_client_lock);
		server_socket.m_bad_client_list.clear();
	}

	// The only thread that touches game state: handles every received
	// message, then periodic work (lobby broadcast, bad client cleanup).
	[[noreturn]] void consume_message()
	{
		constexpr auto lobby_broadcast_interval = std::chrono::seconds(3);
		auto next_lobby_broadcast = std::chrono::steady_clock::now();

		while(true) 
		{
			if (std::chrono::steady_clock::now() >= next_lobby_broadcast)
			{
				broadcast_lobby_info();
				next_lobby_broadcast = std::chrono::steady_clock::now() + lobby_broadcast_interval;
			}

			drop_bad_clients();
			expire_sessions();
			tick_room_games();

			char buffer[max_packet_size + 1]{};
			sockaddr_in client_info{};
			std::time_t time;

			if(!server_socket.get_any_message(&client_info, time, buffer))
			{
				// Short wait: turn timers and resends run in this loop.
				server_socket.block_until_queue_event(50);
				continue;
			}

			const Message* message = reinterpret_cast<Message*>(buffer);

			if (!authorize(message, client_info))
			{
				continue;
			}

			switch(message->type)
			{
			case eMessageType::PONG: break;
			case eMessageType::PING:
				// Keeps the session alive (authorize); does not join anything.
				reply_ping(client_info);
				break;
			// Movement is still decided by the moving client (until the server
			// simulates it); these are relayed for display only.
			case eMessageType::Position:
				broadcast<PositionMsg>(message);
				break;
			case eMessageType::ProjectileSelect:
				broadcast<ProjectileSelectMsg>(message);
				break;
			case eMessageType::Stop:
				broadcast<StopMsg>(message);
				break;
			case eMessageType::Firing:
				broadcast<FiringMsg>(message);
				break;
			case eMessageType::PositionReport:
			{
				const auto* report = reinterpret_cast<const PositionMsg*>(message);
				room_games[message->room_id].on_position(
					message->player_id, report->position, report->offset.get_x(), report->offset.get_y());
				break;
			}
			case eMessageType::ShotRequest:
				room_games[message->room_id].on_shot_request(
					message->player_id, *reinterpret_cast<const ShotRequestMsg*>(message), std::chrono::steady_clock::now());
				flush_room_game(message->room_id);
				break;
			case eMessageType::ShotAck:
				room_games[message->room_id].on_shot_ack(message->player_id, *reinterpret_cast<const ShotAckMsg*>(message));
				break;
			case eMessageType::TurnAck:
				room_games[message->room_id].on_turn_ack(message->player_id, *reinterpret_cast<const TurnAckMsg*>(message));
				break;
			case eMessageType::Destroyed:
			{
				// A client may only report its own character's fall.
				const auto* destroyed = reinterpret_cast<const DestroyedMsg*>(message);
				if (destroyed->object_type == eObjectType::Character && destroyed->object_player_id == message->player_id)
				{
					room_games[message->room_id].on_death_report(message->player_id);
				}
				break;
			}
			case eMessageType::LobbyJoin:
				add_client(-1, message->player_id, client_info, time);
				add_player(message->player_id);
				std::cout << "New player joined. Broadcasting new lobby info." << std::endl;
				broadcast_lobby_info();
				break;
			case eMessageType::RoomJoin:
				remove_client_from_lobby(message->player_id);
				broadcast_lobby_info();
				add_player_to_room(
					message->room_id, 
					message->player_id,
					Client 
					{
						client_info,
						message->player_id,
						time
					});
				send_room_info(message->room_id, client_info);
				notify_join(message->room_id);
				break;
			case eMessageType::RoomSelectCh:
				change_character(message);
				break;
			case eMessageType::RoomSelectIt:
				change_items(message);
				break;
			case eMessageType::RoomStart:
				if(check_priority(message->room_id, message->player_id))
				{
					aggregate_send_room_info(message);
				}
				break;
			case eMessageType::LoadDone:
				if(add_load_done(message))
				{
					game_gtg(message->room_id);
				}
				break;
			case eMessageType::DeltaTime:
				break;
			case eMessageType::DebugStateReq:
				reply_debug_state(message, client_info);
				break;
			default: break; // Unknown type of message
			}
		}
	}
}

int main(int argc, char** argv)
{
    std::cout << "Start server...\n";
	std::string resources;

	for (int i = 1; i < argc; ++i)
	{
		if (std::string(argv[i]) == "--debug-state")
		{
			Fortress::Network::Server::debug_state_enabled = true;
			std::cout << "Debug state requests enabled\n";
		}
		else if (std::string(argv[i]) == "--resources" && i + 1 < argc)
		{
			resources = argv[++i];
		}
	}

	Fortress::Network::Server::cloud_mask_path = Fortress::Network::Server::find_cloud_mask(resources);
	std::cout << "Cloud mask: " << Fortress::Network::Server::cloud_mask_path << "\n";

	std::thread receiving_task(
		&Fortress::Network::Server::Socket::receiving_message, 
		&Fortress::Network::Server::server_socket);
	std::thread consume_task(Fortress::Network::Server::consume_message);

	receiving_task.detach();
	consume_task.detach();

	Sleep(0xffffffff);

	return 0;
}