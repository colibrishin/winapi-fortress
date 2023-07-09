#pragma once

#ifndef MATH_H
#define MATH_H

#define _USE_MATH_DEFINES

#include <math.h>

namespace Fortress::Math
{
	/**
	 * \brief Pi
	 */
	constexpr float PI = M_PI;

	/**
	 * \brief Gravity constant
	 */
	constexpr float G = 6.6742E-11;

	/**
	 * \brief Gravitational acceleration constant
	 */
	constexpr float G_ACC = 9.80f;
}

#endif
