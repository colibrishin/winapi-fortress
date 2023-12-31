#ifndef CAMERAMANAGER_HPP
#define CAMERAMANAGER_HPP
#include "camera.hpp"
#include "object.hpp"

namespace Fortress
{
	class CameraManager
	{
	public:
		template <typename T, typename... Args>
		static std::weak_ptr<T> create_camera(Args ... args);
		static void cleanup();

	private:
		inline static std::vector<std::shared_ptr<Camera>> m_cameras = {};
	};

	inline void CameraManager::cleanup()
	{
		for(auto& cam : m_cameras)
		{
			cam.reset();
		}
	}

	template<typename T, typename... Args>
	inline std::weak_ptr<T> CameraManager::create_camera(Args... args)
	{
		m_cameras.push_back(std::make_shared<T>(args...));
		return m_cameras.back();
	}
}
#endif // CAMERAMANAGER_HPP
