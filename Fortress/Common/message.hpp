#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include "pch.h"
#include "Crc32.h"
#include "vector2.hpp"

#undef max

namespace Fortress::Network
{
	enum class eObjectType
	{
		Character = 0x10,
		Projectile = 0x11,
		Ground = 0x12
	};

	enum class eMessageType
	{
		// Latency
		PING = 0x101,
		PONG = 0x102,

		// Validation
		GO = 0x201,
		NOGO = 0x202,

		// interpolation
		DeltaTime = 0x301,
		ReqDeltaTime = 0x302,

		LobbyJoin = 0x401,
		LobbyInfo = 0x402,

		RoomJoin = 0x501,
		RoomInfo = 0x502,
		RoomStart = 0x503,
		RoomSelectCh = 0x504,
		RoomSelectIt = 0x505,

		GameInit = 0x601,
		LoadDone = 0x602,
		GameStart = 0x603,

		Position = 0x701,
		Stop = 0x702,
		Firing = 0x703,
		Fire = 0x704,
		ProjectileSelect = 0x705,
		Item = 0x706,
		ItemFire = 0x707,
		Hit = 0x708,
		Damage = 0x709,
		Destroyed,

		RoundStart = 0x800,
		ReqWind = 0x801,
		RspWind = 0x802,
		TurnEnd = 0x803,

		ProjectileFire = 0x900,
		ProjectileFlying = 0x901,
		ProjectileHit = 0x902,

		// Server-authoritative shots. The shooter asks, the server simulates
		// and sends the result in chunks; turns are decided by the server.
		ShotRequest = 0xA01,
		ShotPath = 0xA02,
		ShotEvents = 0xA03,
		ShotAck = 0xA04,
		TurnChange = 0xA05,
		TurnAck = 0xA06,
		// Client -> server: the movement key the current player holds.
		MoveInput = 0xA08,
		// Server -> client: positions of every character (20 per second).
		CharacterState = 0xA09,
		// Test hook (server started with --debug-state): place a character.
		DebugPlace = 0xA0A,

		// Test harness only. The server answers these only when it was
		// started with --debug-state.
		DebugStateReq = 0xF01,
		DebugStateRsp = 0xF02,
	};

	enum class eCharacterType
	{
		None = 0x10,
		CannonCharacter = 0x11,
		MissileCharacter = 0x12,
		SecwindCharacter = 0x13,
	};

	enum class eItemType
	{
		None = 0x10,
		DoubleShot = 0x11,
		Teleport = 0x12,
		Repair = 0x13,
		DoubleDamage = 0x14
	};

	enum class eMapType
	{
		DesertMap = 0x10,
		SkyValleyMap = 0x11,
	};

	using PlayerID = int;
	using RoomID = int;
	using CRC32 = uint32_t;

	struct Room
	{
		char name[15][5];
	};

	struct Message
	{
		CRC32 crc32;
		eMessageType type;
		RoomID room_id;
		PlayerID player_id;
	};

	struct PingMsg : Message
	{
	};

	struct PongMsg : Message
	{
	};

	struct DeltaTimeMsg : Message
	{
		float deltaTime;
	};

	struct ReqDeltaTimeMsg : Message
	{
		CRC32 last_message;
	};

	struct LobbyJoinMsg : Message
	{
	};

	struct LobbyInfoMsg : Message
	{
		int room_count;
		Room room_info[15];
		int player_count;
		wchar_t player_names[15][15];
	};

	struct RoomJoinMsg : Message
	{
	};

	struct RoomInfoMsg : Message
	{
		wchar_t room_name[15];
		int player_count;
		wchar_t player_names[15][15];
	};

	struct RoomSelectChMsg : Message
	{
		eCharacterType ch_type{};
	};

	struct RoomSelectItMsg : Message
	{
		unsigned int index;
		eItemType item_type;
	};

	struct RoomStartMsg : Message
	{
		eMapType map_type;
	};

	struct GameInitMsg : Message
	{
		uint8_t player_count;
		PlayerID players[15];
		eMapType map_type;
		eCharacterType character_type[15];
		eItemType equied_item [15][4];
	};

	struct LoadDoneMsg : Message
	{
	};

	struct GameStartMsg : Message
	{
	};

	struct GOMsg : Message
	{
		CRC32 last_message;
	};

	struct NOGOMsg : Message
	{
		CRC32 last_message;
	};

	struct PositionMsg : Message
	{
		eObjectType object_type;
		Math::Vector2 position;
		Math::Vector2 offset;
	};

	struct ProjectileSelectMsg : Message
	{
		eProjectileType prj_type;
	};

	struct StopMsg : PositionMsg
	{
	};

	struct FiringMsg : PositionMsg
	{
	};

	struct CharacterFireMsg : PositionMsg
	{
		float charged;
	};

	struct ItemMsg : PositionMsg
	{
		unsigned int index;
		eItemType item_type;
	};

	struct ItemFireMsg : PositionMsg
	{
		unsigned int index;
		eItemType item_type;
		float charged;
	};

	struct ProjectileFireMsg : PositionMsg
	{
		unsigned int prj_id;
		eProjectileType prj_type;
	};

	struct ProjectileFlyingMsg : PositionMsg
	{
		unsigned int prj_id;
		eProjectileType prj_type;
	};

	struct ProjectileHitMsg : PositionMsg
	{
		eObjectType obj_type;
		unsigned int prj_id;
		eProjectileType prj_type;
	};

	struct DamageMsg : Message
	{
		CRC32 last_message;
		eCharacterType shooter_type;
		eCharacterType victim_type;
		PlayerID prj_owner_id;
		unsigned int prj_id;
		eProjectileType prj_type;
		Math::Vector2 prj_position;
		Math::Vector2 ch_position;
		float damage;
	};

	struct DestroyedMsg : Message
	{
		eObjectType object_type;
		PlayerID object_player_id;
	};
	
	struct ReqWindMsg : Message
	{
	};

	struct RspWindMsg : Message
	{
		int wind;
	};

	struct TurnEndMsg : Message
	{
	};

	// Client -> server: the current player fires (or uses an instant item)
	// from where the server has the character.
	struct ShotRequestMsg : Message
	{
		uint32_t turn_no;
		eProjectileType prj_type;
		eItemType item;
		float charged;
		float facing;  // +1 right, -1 left: a quick turn may not reach the server's walk
		float aim_degree;  // above the barrel's slope; the server clamps it to the tank's range
	};

	// Client -> server: held movement key (0 none, 1 left, 2 right). Sent on
	// change and repeated; seq orders them.
	struct MoveInputMsg : Message
	{
		uint32_t turn_no;
		uint32_t seq;
		uint8_t input;
	};

	// Server -> client: server-side state of every character.
	struct CharacterStateMsg : Message
	{
		struct Entry
		{
			PlayerID pid;
			float x;
			float y;
			float facing;
			float pitch;
			float velocity_x;
			float mp;
			uint8_t moving;
			uint8_t alive;
		};

		uint32_t turn_no;
		uint32_t tick;
		// Turn of the last simulated shot: until a client has replayed it,
		// these positions already contain its result.
		uint32_t shot_turn;
		uint8_t count;
		Entry entries[15];
	};

	struct DebugPlaceMsg : Message
	{
		PlayerID target;
		float x;
		float y;
	};

	// Server -> client: one chunk of the shot's projectile path.
	struct ShotPathMsg : Message
	{
		struct Sample
		{
			uint16_t tick;
			uint8_t projectile;
			uint8_t projectile_kind;  // eProjectileType of the shot
			float x;
			float y;
		};

		uint32_t turn_no;
		PlayerID shooter;
		uint16_t chunk;
		uint16_t path_chunks;
		uint16_t event_chunks;
		uint16_t end_tick;
		uint16_t count;
		Sample samples[80];
	};

	// Same values as Fortress::Sim::ShotEventType (checked in Server/RoomGame.hpp).
	enum class eShotEvent : uint8_t
	{
		Spawn,
		Explosion,
		Damage,
		Death,
		Teleport,
		End,
	};

	// Server -> client: one chunk of the shot's events.
	struct ShotEventsMsg : Message
	{
		struct Event
		{
			uint16_t tick;
			uint8_t type;
			uint8_t projectile;
			PlayerID player;
			float x;
			float y;
			float value;
			float hp;
		};

		uint32_t turn_no;
		PlayerID shooter;
		uint16_t chunk;
		uint16_t path_chunks;
		uint16_t event_chunks;
		uint16_t end_tick;
		uint16_t count;
		Event events[40];
	};

	struct ShotAckMsg : Message
	{
		uint32_t turn_no;
		uint8_t is_events;  // 0 = ShotPath chunk, 1 = ShotEvents chunk
		uint16_t chunk;
	};

	// Server -> client: start of a turn (or the end of the match). Carries
	// the authoritative hp of every player.
	struct TurnChangeMsg : Message
	{
		uint32_t turn_no;
		PlayerID current;     // -1 when the match is over
		PlayerID winner;      // -1 none yet, -2 draw
		int wind;
		float time_limit;     // seconds
		uint32_t terrain_crc;
		uint8_t count;
		PlayerID players[15];
		float hp[15];
		uint8_t alive[15];
	};

	struct TurnAckMsg : Message
	{
		uint32_t turn_no;
	};

	struct DebugStateReqMsg : Message
	{
	};

	// Snapshot of the server's bookkeeping for one room, used by the
	// scripted test client to judge outcomes by state instead of logs.
	struct DebugStateRspMsg : Message
	{
		struct Slot
		{
			PlayerID pid;
			uint32_t address; // network byte order
			uint16_t port;    // host byte order
			uint8_t in_game;  // the room's match knows this player
			uint8_t alive;
			float hp;
			float x;
			float y;
		};

		PlayerID current_player;  // -1 if no match is running
		uint32_t turn_no;
		uint8_t slot_count;
		Slot slots[15];
	};

	union Data final
	{
		PingMsg ping;
		PongMsg pong;
		DeltaTimeMsg dt;
		ReqDeltaTimeMsg rdt;
		GOMsg go;
		NOGOMsg nogo;
		PositionMsg pos;
		ProjectileSelectMsg prj_sel;
		FiringMsg firing;
		CharacterFireMsg fire;
		ItemMsg item;
		ProjectileHitMsg hit;
		DestroyedMsg destroyed;
		ReqWindMsg req_wind;
		RspWindMsg rsp_wind;
		LobbyJoinMsg lobby_join;
		LobbyInfoMsg lobby_info;
		RoomJoinMsg room_join;
		RoomInfoMsg room_info;
		RoomSelectChMsg room_ch;
		RoomSelectItMsg room_it;
		GameInitMsg init;
		LoadDoneMsg load_done;
		GameStartMsg start;
		GameInitMsg game_init;
	};

	// UDP Packet size limit
	// @todo: send separately.
	static_assert(sizeof(Data) <= 65507);

	template <typename T, typename... Args>
	static T create_network_message(Args... args)
	{
		T msg{0, args...};
		msg.crc32 = get_crc32(msg);
		return msg;
	}

	template <typename T, typename... Args>
	static T create_prewritten_network_message(const T& msg)
	{
		T msg_copy = msg;
		msg_copy.crc32 = get_crc32(msg_copy);
		return msg_copy;
	}

	template <typename T>
	static CRC32 get_crc32(const T& msg)
	{
		return crc32_fast(
			reinterpret_cast<const char*>(&msg) + sizeof(CRC32), 
			sizeof(T) - sizeof(CRC32));
	}
	
	constexpr unsigned int max_packet_size = sizeof(Data);

	// Socket::send_message silently drops anything bigger than this.
	static_assert(sizeof(DebugStateRspMsg) <= max_packet_size);
	static_assert(sizeof(ShotPathMsg) <= max_packet_size);
	static_assert(sizeof(ShotEventsMsg) <= max_packet_size);
	static_assert(sizeof(TurnChangeMsg) <= max_packet_size);
	static_assert(sizeof(CharacterStateMsg) <= max_packet_size);
}
#endif // MESSAGE_HPP
