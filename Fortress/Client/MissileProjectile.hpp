#pragma once
#ifndef MISSILEPROJECTILE_HPP
#define MISSILEPROJECTILE_HPP

#include "ClientProjectile.hpp"
#include "CharacterProperties.hpp"
#include "../Common/projectile.hpp"

namespace Fortress::Object
{
	class MissileProjectile final : public Network::Client::Object::ClientProjectile
	{
	public:
		MissileProjectile(const unsigned int id, const ObjectBase::character* shooter) : ClientProjectile(
			id,
			shooter,
			L"Precision Projectile",
			L"missile",
			{}, 
			Math::identity,
			5.0f,
			Property::projectile_speed_getter(L"missile", L"main"), 
			{}, 
			Property::projectile_damage_getter(L"missile", L"main"),
			Property::projectile_radius_getter(L"missile", L"main"),
			2,
			1,
			Property::projectile_pen_rate_getter(L"missile", L"main"))
		{
			MissileProjectile::initialize();
		}

		MissileProjectile& operator=(const MissileProjectile& other) = default;
		MissileProjectile& operator=(MissileProjectile&& other) = default;
		MissileProjectile(const MissileProjectile& other) = default;
		MissileProjectile(MissileProjectile&& other) = default;
		~MissileProjectile() override = default;

		void initialize() override;
		virtual void play_hit_sound() override;
		virtual void play_fire_sound() override;
		eProjectileType get_type() const override;
	};

	inline void MissileProjectile::initialize()
	{
		ClientProjectile::initialize();
	}

	inline void MissileProjectile::play_hit_sound()
	{
		m_sound_pack.get_sound(L"main-explosion").lock()->play(false);
	}

	inline void MissileProjectile::play_fire_sound()
	{
		m_sound_pack.get_sound(L"main-fire").lock()->play(false);
	}

	inline eProjectileType MissileProjectile::get_type() const
	{
		return eProjectileType::Main;
	}
}
#endif // CANNONPROJECTILE_HPP
