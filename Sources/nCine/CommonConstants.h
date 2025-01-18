#pragma once

#include <cmath>

namespace nCine
{
	/** @brief 3.1415... (double) */
	constexpr double Pi = 3.141592653589793;
	/** @brief 3.1415... */
	constexpr float fPi = 3.1415927f;

	/** @brief 3.1415... / 2 */
	constexpr float fPiOver2 = fPi / 2.0f;
	/** @brief 3.1415... / 3 */
	constexpr float fPiOver3 = fPi / 3.0f;
	/** @brief 3.1415... / 4 */
	constexpr float fPiOver4 = fPi / 4.0f;
	/** @brief 3.1415... / 6 */
	constexpr float fPiOver6 = fPi / 6.0f;
	/** @brief 3.1415... * 2 */
	constexpr float fTwoPi = fPi * 2.0f;

	/** @brief Multiply to convert degrees to radians (double) */
	constexpr double DegToRad = Pi / 180.0;
	/** @brief Multiply to convert degrees to radians */
	constexpr float fDegToRad = fPi / 180.0f;
	/** @brief Multiply to convert radians to degrees (double) */
	constexpr double RadToDeg = 180.0 / Pi;
	/** @brief Multiply to convert radians to degrees */
	constexpr float fRadToDeg = 180.0f / fPi;

	/** @brief 1 degree as radians */
	constexpr float fRadAngle1 = fTwoPi / 360.0f;
	/** @brief 30 degrees as radians */
	constexpr float fRadAngle30 = fPiOver6;
	/** @brief 45 degrees as radians */
	constexpr float fRadAngle45 = fPiOver4;
	/** @brief 90 degrees as radians */
	constexpr float fRadAngle90 = fPiOver2;
	/** @brief 180 degrees as radians */
	constexpr float fRadAngle180 = fPi;
	/** @brief 270 degrees as radians */
	constexpr float fRadAngle270 = fPi + fPiOver2;
	/** @brief 360 degrees as radians */
	constexpr float fRadAngle360 = fTwoPi;
}
