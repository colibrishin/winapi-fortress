#pragma once
#ifndef SECNWINDCHARACTER_HPP
#define SECNWINDCHARACTER_HPP
#include "ClientCharacter.hpp"
#include "../Common/character.hpp"
#include "EnergyBallProjectile.hpp"
#include "MultiEnergyBallProjectile.hpp"

namespace Fortress::Object
{
	class SecwindCharacter final : public Network::Client::Object::ClientCharacter
	{
	public:
		SecwindCharacter(
			int player_id,
			const std::wstring& name, 
			const Math::Vector2& position, 
			const Math::Vector2& orientation)
			:
			ClientCharacter(
				player_id,
				name,
				L"secwind",
				orientation,
				position,
				{0.0f, 0.0f},
				1.0f,
				{25.0f, 1.0f},
				{},
				ObjectBase::character_full_hp,
				ObjectBase::character_full_mp,
				Property::character_armor_getter(L"secwind"))
		{
			initialize();
		}

		SecwindCharacter& operator=(const SecwindCharacter& other) = default;
		SecwindCharacter& operator=(SecwindCharacter&& other) = default;
		SecwindCharacter(const SecwindCharacter& other) = default;
		SecwindCharacter(SecwindCharacter&& other) = default;
		~SecwindCharacter() override = default;

		void initialize() override
		{
			set_sprite_offset(L"fire", L"right", {45.0f, 0.0f});
			set_sprite_offset(L"fire_sub", L"right", {45.0f, 0.0f});
			set_sprite_offset(L"charging", L"right", {15.0f, 0.0f});
			set_sprite_offset(L"idle", L"right", {30.0f, 0.0f});
			set_sprite_offset(L"idle_low", L"right", {30.0f, 0.0f});

			set_sprite_offset(L"projectile", L"left", {39.5f, 0.0f});
			set_sprite_rotation_offset(L"projectile", L"left", {-39.5f, 0.0f});

			character::initialize();
		}

		Network::eCharacterType get_type() const override;

	protected:
		std::weak_ptr<ObjectBase::projectile> get_main_projectile(const unsigned int id) override;
		std::weak_ptr<ObjectBase::projectile> get_sub_projectile(const unsigned int id) override;
	};

	inline Network::eCharacterType SecwindCharacter::get_type() const
	{
		return Network::eCharacterType::SecwindCharacter;
	}

	inline std::weak_ptr<ObjectBase::projectile> SecwindCharacter::get_main_projectile(const unsigned int id)
	{
		return ObjectBase::ObjectManager::create_object<EnergyBallProjectile>(id, this).lock();
	}

	inline std::weak_ptr<ObjectBase::projectile> SecwindCharacter::get_sub_projectile(const unsigned int id)
	{
		return ObjectBase::ObjectManager::create_object<MultiEnergyBallProjectile>(id, this).lock();
	}
}
#endif // SECNWINDCHARACTER_HPP
