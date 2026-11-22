#pragma once
#include "CharacterProperties.hpp"
#include "../Common/projectile.hpp"
#include "../Common/debug.hpp"

namespace Fortress::Network::Client::Object
{
	// Projectiles on the client only replay the server's simulation
	// (projectile::start_replay); they send nothing.
	class ClientProjectile : public ObjectBase::projectile
	{
	public:
		ClientProjectile() = delete;
		~ClientProjectile() override = default;

	protected:
		ClientProjectile(const unsigned int id, const ObjectBase::character* shooter, const std::wstring& name, const std::wstring& short_name,
			const Math::Vector2& position, const Math::Vector2& velocity, const float mass, const Math::Vector2& speed,
			const Math::Vector2& acceleration, const float damage, const float radius, const int hit_count, const int fire_count,
			const float armor_penetration)
			: projectile(
				  id, shooter, name, short_name, position, velocity,
				  Fortress::Object::Property::projectile_hitbox_getter(), mass, speed, acceleration, damage, radius,
				  hit_count, fire_count, armor_penetration)
		{
		}
	};
}
