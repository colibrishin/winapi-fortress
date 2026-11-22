#pragma once
#ifndef NUTSHELLPROJECTILE_HPP
#define NUTSHELLPROJECTILE_HPP

#include "ClientProjectile.hpp"
#include "CharacterProperties.hpp"
#include "../Common/projectile.hpp"

namespace Fortress::Object
{
	class NutShellProjectile final : public Network::Client::Object::ClientProjectile
	{
	public:
		NutShellProjectile(const unsigned int id, const ObjectBase::character* shooter) : ClientProjectile(
			id,
			shooter,
			L"Nutshell Projectile",
			shooter->get_short_name(),
			{}, 
			Math::identity,
			5.0f,
			Property::projectile_speed_getter(shooter->get_short_name(), L"main"), 
			{}, 
			Property::Table::nutshell_damage,
			Property::Table::nutshell_radius,
			Property::Table::nutshell_hit_count,
			Property::Table::nutshell_fire_count,
			Property::Table::nutshell_penetration)
		{
			NutShellProjectile::initialize();
		}

		NutShellProjectile& operator=(const NutShellProjectile& other) = default;
		NutShellProjectile& operator=(NutShellProjectile&& other) = default;
		NutShellProjectile(const NutShellProjectile& other) = default;
		NutShellProjectile(NutShellProjectile&& other) = default;
		~NutShellProjectile() override = default;

		void initialize() override;
		virtual void play_hit_sound() override;
		virtual void play_fire_sound() override;
		eProjectileType get_type() const override;
	};

	inline void NutShellProjectile::initialize()
	{
		ClientProjectile::initialize();
	}

	inline void NutShellProjectile::play_hit_sound()
	{
		m_sound_pack.get_sound(L"main-explosion").lock()->play(false);
	}

	inline void NutShellProjectile::play_fire_sound()
	{
		m_sound_pack.get_sound(L"main-fire").lock()->play(false);
	}

	inline eProjectileType NutShellProjectile::get_type() const
	{
		return eProjectileType::Nutshell;
	}
}
#endif // NUTSHELLPROJECTILE_HPP
