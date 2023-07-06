#include "titleScene.h"

#include <random>

#include "application.h"
#include "input.hpp"
#include "sceneManager.hpp"

void Scene::TitleScene::initialize()
{
	_scene::initialize();

	for(auto& m_object : m_objects)
	{
		static thread_local std::mt19937 generator(static_cast<unsigned int>(time(nullptr)));
		std:: uniform_int_distribution h_distribution(0, Fortress::Application::get_window_width());
		std:: uniform_int_distribution w_distribution(0, Fortress::Application::get_window_height());

		Math::Vector2 random_pos = {static_cast<float>(w_distribution(generator)), static_cast<float>(h_distribution(generator))};
		m_object = {
			L"Ball",
			random_pos,
			{1.0f, 1.0f},
			{20.0f, 20.0f},
			500.0f,
			0.0f,
			0,
			0,
			Object::CharacterType::CANNON};
	}
}

void Scene::TitleScene::update()
{
	_scene::update();
	for(auto& obj : m_objects)
	{
		Fortress::Application::checkWindowFrame(obj);
	}

	if(Fortress::Input::getKey(Fortress::eKeyCode::SPACE))
	{
		SceneManager::SetActive(L"Character Scene");
	}
}

void Scene::TitleScene::render()
{
	_scene::render();

	for(auto & m_object : m_objects)
	{
		Ellipse(
			m_hdc,
			m_object.get_x(), 
			m_object.get_y(), 
			m_object.get_x() + m_object.m_hitbox.get_x(),
			m_object.get_y() + m_object.m_hitbox.get_y());
	}

	wchar_t notice[100] = {};
	swprintf_s(notice, 100,  L"Press SPACE to continue...");
	const size_t strlen = wcsnlen_s(notice, 100);
	TextOut(m_hdc, 300, 300, notice, strlen);
}