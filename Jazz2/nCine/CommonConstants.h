#pragma once

#include <cmath>

namespace nCine
{
	constexpr double Pi = 3.141592653589793;
	constexpr float fPi = 3.1415927f;

	constexpr float fPiOver2 = fPi / 2.0f;
	constexpr float fPiOver3 = fPi / 3.0f;
	constexpr float fPiOver4 = fPi / 4.0f;
	constexpr float fPiOver6 = fPi / 6.0f;
	constexpr float fTwoPi = fPi * 2.0f;

	constexpr double DegToRad = Pi / 180.0;
	constexpr float fDegToRad = fPi / 180.0f;
	constexpr double RadToDeg = 180.0 / Pi;
	constexpr float fRadToDeg = 180.0f / fPi;

	constexpr float fRadAngle1 = fTwoPi / 360.0f;
	constexpr float fRadAngle30 = fPiOver6;
	constexpr float fRadAngle45 = fPiOver4;
	constexpr float fRadAngle90 = fPiOver2;
	constexpr float fRadAngle180 = fPi;
	constexpr float fRadAngle270 = fPi + fPiOver2;
	constexpr float fRadAngle360 = fTwoPi;
}
