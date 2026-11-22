#pragma once
// Minimal check helpers shared by the SimTests translation units.
// Failed checks are printed; main returns the failure count.

#include <cmath>
#include <iostream>
#include <string>

namespace SimTests
{
	inline int g_failed = 0;
	inline int g_checked = 0;

	inline bool check(const bool ok, const std::string& what)
	{
		++g_checked;

		if (!ok)
		{
			++g_failed;
			std::cout << "FAIL " << what << std::endl;
		}

		return ok;
	}

	inline bool check_near(const float actual, const float expected, const float tolerance, const std::string& what)
	{
		++g_checked;

		if (std::fabs(actual - expected) > tolerance)
		{
			++g_failed;
			std::cout << "FAIL " << what << ": expected " << expected << ", got " << actual << std::endl;
			return false;
		}

		return true;
	}
}
