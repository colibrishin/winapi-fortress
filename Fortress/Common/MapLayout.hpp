#pragma once
#ifndef MAP_LAYOUT_HPP
#define MAP_LAYOUT_HPP

// Ground layout of each map, shared by the client map classes
// (Client/DesertMap.hpp, Client/SkyValleyMap.hpp) and the server
// simulation (Common/Sim/SimShot.hpp build_map). No Windows includes.

namespace Fortress::Map::Layout
{
	enum class GroundShape
	{
		Flat,
		StairwayRampLeft,   // Object::Stairway with Math::left
		StairwayRampRight,  // Object::Stairway with Math::right
		Cloud,              // Object::CloudGround: shape from cloud_mask_path
	};

	struct GroundEntry
	{
		const wchar_t* name;
		GroundShape shape;
		float x;       // centre
		float y;
		float width;   // ignored for Cloud (taken from the mask)
		float height;
	};

	inline constexpr GroundEntry desert[] = {
		{L"left_upper", GroundShape::StairwayRampLeft, -300.0f, 0.0f, 500.0f, 100.0f},
		{L"mid_lower", GroundShape::Flat, 0.0f, 100.0f, 500.0f, 100.0f},
		{L"right_upper", GroundShape::StairwayRampRight, 300.0f, 0.0f, 500.0f, 100.0f},
	};

	inline constexpr GroundEntry sky_valley[] = {
		{L"cloudy", GroundShape::Cloud, 0.0f, 0.0f, 0.0f, 0.0f},
	};

	// Relative to the client's working directory (Fortress/Client).
	inline constexpr const char* cloud_mask_path = "./resources/misc/tiles/cloud-mask.bmp";
}

#endif // MAP_LAYOUT_HPP
