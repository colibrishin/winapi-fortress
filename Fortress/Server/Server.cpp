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
	static bool turn_end[15][15] {false, };
	static bool double_damage_enabled[15][15] {false,};
	static int hit_count[15][15] {0, };

	static std::default_random_engine e;
	static std::uniform_real_distribution<float> dis{-50, 50};
	static float room_wind[15]{0.0f, };

	static std::map<std::pair<RoomID, PlayerID>, Client> client_list = {};

	// Set by --debug-state. Lets the scripted test client inspect state.
	static bool debug_state_enabled = false;

	int get_wind_acceleration(const RoomID room_id)
	{
		return static_cast<int>(room_wind[room_id] / 10.0f) * 10.0f;
	}

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

	void send_go(const Message* message, RoomID room_id)
	{
		const auto clients = get_room_client(room_id);

		const auto msg = create_network_message<GOMsg>(
			eMessageType::GO, message->room_id, -1, message->crc32);

		for(const auto& client : clients)
		{
			server_socket.send_message<GOMsg>(&msg, client.ip);
		}
	}

	void add_client(RoomID id, PlayerID pid, const SOCKADDR_IN& client_info, const std::time_t time)
	{
		if (client_list.find({id, pid}) != client_list.end())
		{
			const auto& [eip, epid, etime, _] = client_list[{id, pid}];

			if(std::strncmp(
				reinterpret_cast<const char*>(&eip),
				reinterpret_cast<const char*>(extract_ip(client_info)),
				sizeof(sockaddr)) != 0)
			{
				std::cout << "Client IP changed. Removing from known list." << std::endl;
				client_list.erase({id, pid});
				return;
			}
		}

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

	void send_wind(const Message* message, const sockaddr_in& client_info)
	{
		const int wind = get_wind_acceleration(message->room_id);
		const auto msg = create_network_message<RspWindMsg>(
			eMessageType::RspWind, message->room_id, message->player_id, wind);
		server_socket.send_message<RspWindMsg>(&msg, client_info);
	}

	void reset_wind(const RoomID room_id)
	{
		room_wind[room_id] = dis(e);
	}

	void aggregate_send_room_info(const Message* message)
	{
		std::cout << "Room start received, Aggregates room info" << std::endl;
		const auto* recv_msg = reinterpret_cast<const RoomStartMsg*>(message);
		const RoomID room_id = recv_msg->room_id;
		const eMapType map = recv_msg->map_type;
		std::fill_n(hit_count[message->room_id], std::size(hit_count[message->room_id]), 0);

		GameInitMsg gi{};
		uint8_t pos = 0;
		const auto room_clients = get_room_client(room_id);

		for(const auto& client : room_clients)
		{
			gi.players[pos] = client.pid;
			if (client.room_info.character == eCharacterType::None)
			{
				gi.character_type[pos] = eCharacterType::CannonCharacter;
			}
			else
			{
				gi.character_type[pos] = client.room_info.character;
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

		reset_wind(room_id);
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

	bool check_turn_done(const RoomID room_id)
	{
		const auto clients = get_room_client(room_id);
		int load_finished = 0;

		for(const auto& client: clients)
		{
			if(turn_end[room_id][client.pid])
			{
				load_finished++;
			}
		}

		return load_finished == clients.size();
	}

	void set_turn_done(const Message* message)
	{
		std::fill_n(hit_count[message->room_id], std::size(hit_count[message->room_id]), 0);
		turn_end[message->room_id][message->player_id] = true;
	}

	void clear_turn_done(const RoomID room_id)
	{
		std::fill(std::begin(turn_end[room_id]), std::end(turn_end[room_id]), false);
	}

	void calculate_damage_and_reply(Message* message, const sockaddr_in& client_info)
	{
		auto* casted = reinterpret_cast<DamageMsg*>(message);

		if (casted->prj_owner_id < 0 || casted->prj_owner_id >= max_players)
		{
			return;
		}

		auto& dd = double_damage_enabled[casted->room_id][casted->prj_owner_id];

		const float damage = calculate_damage(
			casted->shooter_type,
			casted->victim_type, 
			casted->prj_type, 
			hit_count[message->room_id][message->player_id],
			dd,
			Object::Property::projectile_hitbox_getter(),
			casted->prj_position,
			casted->ch_position);

		if(dd)
		{
			dd = false;
		}

		casted->damage = damage;
		const auto overwrite_message = create_prewritten_network_message<DamageMsg>(*casted);

		std::cout << " Damage : " << damage << std::endl;

		server_socket.send_message<DamageMsg>(&overwrite_message, client_info);
		broadcast<DamageMsg>(&overwrite_message);
	}

	void set_double_damage_flag(const Message* message)
	{
		auto* casted = reinterpret_cast<const ItemMsg*>(message);

		if(casted->item_type == eItemType::DoubleDamage)
		{
			double_damage_enabled[casted->room_id][casted->player_id] = true;
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
		rsp.wind = is_game_room ? get_wind_acceleration(room_id) : 0;

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

			if (is_game_room && key.second >= 0 && key.second < 15)
			{
				slot.turn_end = turn_end[room_id][key.second];
				slot.double_damage = double_damage_enabled[room_id][key.second];
				slot.hit_count = hit_count[room_id][key.second];
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

	// A silent binding can be taken over after this (e.g. a restarted client
	// on a new port).
	constexpr auto session_timeout = std::chrono::seconds(5);

	static std::map<uint64_t, Session> sessions;
	static std::map<PlayerID, uint64_t> session_by_pid;

	uint64_t address_key(const sockaddr_in& address)
	{
		return static_cast<uint64_t>(address.sin_addr.s_addr) << 16 | ntohs(address.sin_port);
	}

	void release_session(const uint64_t key)
	{
		const auto it = sessions.find(key);
		if (it == sessions.end())
		{
			return;
		}

		const auto owner = session_by_pid.find(it->second.pid);
		if (owner != session_by_pid.end() && owner->second == key)
		{
			session_by_pid.erase(owner);
		}

		sessions.erase(it);
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
		case eMessageType::Fire:
		case eMessageType::ProjectileSelect:
		case eMessageType::Item:
		case eMessageType::ItemFire:
		case eMessageType::Hit:
		case eMessageType::Damage:
		case eMessageType::Destroyed:
		case eMessageType::ReqWind:
		case eMessageType::TurnEnd:
		case eMessageType::ProjectileFire:
		case eMessageType::ProjectileFlying:
		case eMessageType::ProjectileHit:
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

		if (session != sessions.end() && session->second.pid == pid)
		{
			session->second.last_seen = now;
		}
		else if (claims)
		{
			// Take the player id if nobody holds it or its holder went silent.
			const auto owner = session_by_pid.find(pid);
			if (owner != session_by_pid.end())
			{
				const auto& holder = sessions.at(owner->second);
				if (now - holder.last_seen < session_timeout)
				{
					std::cout << "Rejected: player " << pid << " is bound to another address" << std::endl;
					return false;
				}
				release_session(owner->second);
			}

			release_session(key);
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

	void drop_bad_clients()
	{
		std::lock_guard _(server_socket.bad_client_lock);

		for (const auto& bad_client : server_socket.m_bad_client_list)
		{
			for (auto it = client_list.begin(); it != client_list.end();)
			{
				it = address_key(it->second.ip) == address_key(bad_client) ? client_list.erase(it) : std::next(it);
			}

			release_session(address_key(bad_client));
		}

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

			char buffer[max_packet_size + 1]{};
			sockaddr_in client_info{};
			std::time_t time;

			if(!server_socket.get_any_message(&client_info, time, buffer))
			{
				server_socket.block_until_queue_event();
				continue;
			}

			const Message* message = reinterpret_cast<Message*>(buffer);
			auto* writable_message = reinterpret_cast<Message*>(buffer);

			if (!authorize(message, client_info))
			{
				continue;
			}

			switch(message->type)
			{
			case eMessageType::GO:
			case eMessageType::NOGO:
			case eMessageType::LobbyInfo:
			case eMessageType::RoomInfo:
			case eMessageType::GameInit:
			case eMessageType::ReqDeltaTime:
			case eMessageType::GameStart:
			case eMessageType::RspWind:
			case eMessageType::PONG: break;
			case eMessageType::PING:
				add_client(message->room_id, message->player_id, client_info, time);
				reply_ping(client_info);
				break;
			case eMessageType::Position:
				// @todo: verification?
				std::cout << "Message type: Position ";
				broadcast<PositionMsg>(message);
				break;
			case eMessageType::ProjectileSelect:
				std::cout << "Message type: Projectile Selection ";
				broadcast<ProjectileSelectMsg>(message);
				break;
			case eMessageType::Stop:
				std::cout << "Message type: Stop ";
				broadcast<StopMsg>(message);
				break;
			case eMessageType::Firing:
				std::cout << "Message type: Firing ";
				broadcast<FiringMsg>(message);
				break;
			case eMessageType::Fire:
				std::cout << "Message type: Fire ";
				broadcast<CharacterFireMsg>(message);
				break;
			case eMessageType::Item:
				std::cout << "Message type: Item ";
				set_double_damage_flag(message);
				broadcast<ItemMsg>(message);
				break;
			case eMessageType::ItemFire:
				std::cout << "Message type: Item Fire ";
				broadcast<ItemFireMsg>(message);
				break;
			case eMessageType::ProjectileFire:
				std::cout << "Message type : Projectile Fire";
				broadcast<ProjectileFireMsg>(message);
				break;
			case eMessageType::ProjectileFlying:
				std::cout << "Message type : Projectile Flying";
				broadcast<ProjectileFlyingMsg>(message);
				break;
			case eMessageType::ProjectileHit:
				std::cout << "Message type : Projectile Hit ";
				broadcast<ProjectileHitMsg>(message);
				break;
			case eMessageType::TurnEnd:
				// @todo: check previous message.
				std::cout << "Turn ended" << std::endl;
				set_turn_done(message);
				if(check_turn_done(message->room_id))
				{
					reset_wind(message->room_id);
					clear_turn_done(message->room_id);
					send_go(message, message->room_id);
				}
				break;
			case eMessageType::Damage:
				std::cout << "Message type : Damage ";
				calculate_damage_and_reply(writable_message, client_info);
				break;
			case eMessageType::ReqWind:
				std::cout << "Got wind request" << std::endl;
				send_wind(message, client_info);
				break;
			case eMessageType::Destroyed:
				break;
			case eMessageType::LobbyJoin:
				add_client(message->room_id, message->player_id, client_info, time);
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

	for (int i = 1; i < argc; ++i)
	{
		if (std::string(argv[i]) == "--debug-state")
		{
			Fortress::Network::Server::debug_state_enabled = true;
			std::cout << "Debug state requests enabled\n";
		}
	}

	std::thread receiving_task(
		&Fortress::Network::Server::Socket::receiving_message, 
		&Fortress::Network::Server::server_socket);
	std::thread consume_task(Fortress::Network::Server::consume_message);

	receiving_task.detach();
	consume_task.detach();

	Sleep(0xffffffff);

	return 0;
}