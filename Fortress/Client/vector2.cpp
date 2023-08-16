#include "vector2.hpp"

#include <objidl.h>
#include <gdiplus.h>
#include <numeric>

#pragma comment (lib,"Gdiplus.lib")

namespace Fortress::Math
{
	Vector2::operator Gdiplus::PointF() const
	{
		return Gdiplus::PointF{m_x, m_y};
	}
}