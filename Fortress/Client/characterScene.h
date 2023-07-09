#pragma once
#ifndef CHARACTERSCENE_H
#define CHARACTERSCENE_H

#include "character.hpp"
#include "sceneManager.hpp"

namespace Fortress::Scene
{
	class CharacterScene final : public SceneManager::_scene
	{
	public:
		CharacterScene() : _scene(L"Character Scene")
		{
		}

		void initialize() override;
		void update() override;
		void render() override;
		void deactivate() override;
		void activate() override;

	private:
		std::shared_ptr<ObjectBase::character> m_object;
		std::shared_ptr<Object::Ground> m_ground;
	};
}

#endif
