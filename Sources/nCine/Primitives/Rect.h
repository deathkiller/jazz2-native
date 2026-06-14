#pragma once

#include "Vector2.h"

#include <algorithm>

#include <Containers/Tags.h>

namespace nCine
{
	inline namespace Primitives
	{
		using Death::Containers::NoInitT;

		/**
			@brief Rectangle in a two-dimensional space
			
			Stored as a top-left point (@ref X, @ref Y) and a size (@ref W, @ref H). The size may
			be negative, in which case @ref X / @ref Y no longer mark the top-left corner; the
			@ref Min() and @ref Max() helpers normalize this. Provides construction from a center
			or from minimum and maximum coordinates, containment and overlap tests, and
			intersection and union operations.
		*/
		template<class T>
		class Rect
		{
		public:
			/** @brief Top-left X coordinate (left edge for a positive width) */
			T X;
			/** @brief Top-left Y coordinate (top edge for a positive height) */
			T Y;
			/** @brief Width */
			T W;
			/** @brief Height */
			T H;

			/** @brief Default constructor, all zeros */
			constexpr Rect() noexcept
				: X(T(0)), Y(T(0)), W(T(0)), H(T(0)) {}
			explicit Rect(NoInitT) noexcept {}
			/** @brief Constructs a rectangle from a top-left point and a size */
			constexpr Rect(T x, T y, T w, T h) noexcept
				: X(x), Y(y), W(w), H(h) {}
			/** @brief Constructs a rectangle from a top-left point and a size as two @ref Vector2 */
			constexpr Rect(const Vector2<T>& point, const Vector2<T>& size) noexcept
				: X(point.X), Y(point.Y), W(size.X), H(size.Y) {}

			/** @brief Creates a rectangle from a center point and a size */
			static Rect FromCenterSize(T xx, T yy, T ww, T hh);
			/** @brief Creates a rectangle from a center point and a size as two @ref Vector2 */
			static Rect FromCenterSize(const Vector2<T>& center, const Vector2<T>& size);

			/** @brief Creates a rectangle from minimum and maximum coordinates */
			static Rect FromMinMax(T minX, T minY, T maxX, T maxY);
			/** @brief Creates a rectangle from minimum and maximum coordinates as two @ref Vector2 */
			static Rect FromMinMax(const Vector2<T>& min, const Vector2<T>& max);

			/** @brief Returns the size as a vector */
			Vector2<T> GetSize() const;
			/** @brief Returns the top-left point as a vector */
			Vector2<T> GetLocation() const;

			/** @brief Returns the center of the rectangle */
			Vector2<T> Center() const;
			/** @brief Returns the minimum coordinates, accounting for a negative size */
			Vector2<T> Min() const;
			/** @brief Returns the maximum coordinates, accounting for a negative size */
			Vector2<T> Max() const;

			/** @brief Sets the top-left point and the size */
			void Set(T x, T y, T w, T h);
			/** @brief Sets the top-left point and the size as two @ref Vector2 */
			void Set(const Vector2<T>& point, const Vector2<T>& size);
			/** @brief Moves the center to a new position, keeping the size */
			void SetCenter(float cx, float cy);
			/** @brief Moves the center to a new position as a @ref Vector2, keeping the size */
			void SetCenter(const Vector2<T>& center);
			/** @brief Changes the size, keeping the center */
			void SetSize(float ww, float hh);
			/** @brief Changes the size as a @ref Vector2, keeping the center */
			void SetSize(const Vector2<T>& size);

			/** @brief Sets the center and the size */
			void SetCenterSize(T xx, T yy, T ww, T hh);
			/** @brief Sets the center and the size as two @ref Vector2 */
			void SetCenterSize(const Vector2<T>& center, const Vector2<T>& size);

			/** @brief Sets the minimum and maximum coordinates */
			void SetMinMax(T minX, T minY, T maxX, T maxY);
			/** @brief Sets the minimum and maximum coordinates as two @ref Vector2 */
			void SetMinMax(const Vector2<T>& min, const Vector2<T>& max);

			/**
			 * @brief Converts the rectangle to a different element type
			 */
			template<class S>
			Rect<S> As() {
				return Rect<S>(static_cast<S>(X), static_cast<S>(Y), static_cast<S>(W), static_cast<S>(H));
			}

			/** @brief Negates the size while keeping the same covered area, flipping the top-left corner */
			void InvertSize();

			/** @brief Returns `true` if the point is inside the rectangle */
			bool Contains(T px, T py) const;
			/** @brief Returns `true` if the point vector is inside the rectangle */
			bool Contains(const Vector2<T>& p) const;

			/** @brief Returns `true` if the other rectangle is fully contained within this one */
			bool Contains(const Rect<T>& rect) const;
			/** @brief Returns `true` if this rectangle overlaps the other one in any way */
			bool Overlaps(const Rect<T>& rect) const;

			/** @brief Clips this rectangle to its intersection with the other rectangle */
			void Intersect(const Rect<T>& rect);

			/** @brief Expands this rectangle to the bounding box of both rectangles */
			void Union(const Rect<T>& rect);

			bool operator==(const Rect& rect) const;
			bool operator!=(const Rect& rect) const;

			/** @{ @name Constants */

			/** @brief Empty rectangle */
			static const Rect Empty;

			/** @} */
		};

		/** @brief Rectangle in a two-dimensional space of floats */
		using Rectf = Rect<float>;
		/** @brief Rectangle in a two-dimensional space of 32-bit integers */
		using Recti = Rect<int>;

		template<class T>
		inline Rect<T> Rect<T>::FromCenterSize(T xx, T yy, T ww, T hh)
		{
			return Rect(xx - static_cast<T>(ww * 0.5f),
						yy - static_cast<T>(hh * 0.5f),
						ww, hh);
		}

		template<class T>
		inline Rect<T> Rect<T>::FromCenterSize(const Vector2<T>& center, const Vector2<T>& size)
		{
			return Rect(center.X - static_cast<T>(size.X * 0.5f),
						center.Y - static_cast<T>(size.Y * 0.5f),
						size.X, size.Y);
		}

		template<class T>
		inline Rect<T> Rect<T>::FromMinMax(T minX, T minY, T maxX, T maxY)
		{
			return Rect(minX, minY, maxX - minX, maxY - minY);
		}

		template<class T>
		inline Rect<T> Rect<T>::FromMinMax(const Vector2<T>& min, const Vector2<T>& max)
		{
			return Rect(min.X, min.Y, max.X - min.X, max.Y - min.Y);
		}

		template<class T>
		inline Vector2<T> Rect<T>::GetSize() const
		{
			return Vector2<T>(W, H);
		}

		template<class T>
		inline Vector2<T> Rect<T>::GetLocation() const
		{
			return Vector2<T>(X, Y);
		}

		template<class T>
		inline Vector2<T> Rect<T>::Center() const
		{
			return Vector2<T>(X + static_cast<T>(W * 0.5f), Y + static_cast<T>(H * 0.5f));
		}

		template<class T>
		inline Vector2<T> Rect<T>::Min() const
		{
			return Vector2<T>((W > T(0)) ? X : X + W, (H > T(0)) ? Y : Y + H);
		}

		template<class T>
		inline Vector2<T> Rect<T>::Max() const
		{
			return Vector2<T>((W > T(0)) ? X + W : X, (H > T(0)) ? Y + H : Y);
		}

		template<class T>
		inline void Rect<T>::Set(T x, T y, T w, T h)
		{
			X = x;
			Y = y;
			W = w;
			H = h;
		}

		template<class T>
		inline void Rect<T>::Set(const Vector2<T>& point, const Vector2<T>& size)
		{
			X = point.X;
			Y = point.Y;
			W = size.X;
			H = size.Y;
		}

		template<class T>
		inline void Rect<T>::SetCenter(float cx, float cy)
		{
			X = cx - static_cast<T>(W * 0.5f);
			Y = cy - static_cast<T>(H * 0.5f);
		}

		template<class T>
		inline void Rect<T>::SetCenter(const Vector2<T>& center)
		{
			X = center.X - static_cast<T>(W * 0.5f);
			Y = center.Y - static_cast<T>(H * 0.5f);
		}

		template<class T>
		inline void Rect<T>::SetSize(float ww, float hh)
		{
			W = ww;
			H = hh;
		}

		template<class T>
		inline void Rect<T>::SetSize(const Vector2<T>& size)
		{
			W = size.X;
			H = size.Y;
		}

		template<class T>
		inline void Rect<T>::SetCenterSize(T xx, T yy, T ww, T hh)
		{
			X = xx - static_cast<T>(ww * 0.5f);
			Y = yy - static_cast<T>(hh * 0.5f);
			W = ww;
			H = hh;
		}

		template<class T>
		inline void Rect<T>::SetCenterSize(const Vector2<T>& center, const Vector2<T>& size)
		{
			X = center.X - static_cast<T>(size.X * 0.5f);
			Y = center.Y - static_cast<T>(size.Y * 0.5f);
			W = size.X;
			H = size.Y;
		}

		template<class T>
		inline void Rect<T>::SetMinMax(T minX, T minY, T maxX, T maxY)
		{
			X = minX;
			Y = minY;
			W = maxX - minX;
			H = maxY - minY;
		}

		template<class T>
		inline void Rect<T>::SetMinMax(const Vector2<T>& min, const Vector2<T>& max)
		{
			X = min.X;
			Y = min.Y;
			W = max.X - min.X;
			H = max.Y - min.Y;
		}

		template<class T>
		inline void Rect<T>::InvertSize()
		{
			X = X + W;
			Y = Y + H;
			W = -W;
			H = -H;
		}

		template<class T>
		inline bool Rect<T>::Contains(T px, T py) const
		{
			const bool xAxis = (W > T(0)) ? (px >= X && px <= X + W) : (px <= X && px >= X + W);
			const bool yAxis = (H > T(0)) ? (py >= Y && py <= Y + H) : (py <= Y && py >= Y + H);
			return xAxis && yAxis;
		}

		template<class T>
		inline bool Rect<T>::Contains(const Vector2<T>& p) const
		{
			return Contains(p.X, p.Y);
		}

		template<class T>
		inline bool Rect<T>::Contains(const Rect& rect) const
		{
			const bool containsMin = Contains(rect.Min());
			const bool containsMax = Contains(rect.Max());
			return (containsMin && containsMax);
		}

		template<class T>
		inline bool Rect<T>::Overlaps(const Rect& rect) const
		{
			const Vector2<T> rectMin = rect.Min();
			const Vector2<T> rectMax = rect.Max();
			const Vector2<T> thisMin = Min();
			const Vector2<T> thisMax = Max();

			const bool disjoint = (rectMax.X < thisMin.X || rectMin.X > thisMax.X ||
								   rectMax.Y < thisMin.Y || rectMin.Y > thisMax.Y);
			return !disjoint;
		}

		template<class T>
		inline void Rect<T>::Intersect(const Rect& rect)
		{
			const Vector2<T> rectMin = rect.Min();
			const Vector2<T> rectMax = rect.Max();

			Vector2<T> newMin = Min();
			Vector2<T> newMax = Max();
			if (newMin.X < rectMin.X)
				newMin.X = rectMin.X;
			if (newMin.Y < rectMin.Y)
				newMin.Y = rectMin.Y;
			if (rectMax.X < newMax.X)
				newMax.X = rectMax.X;
			if (rectMax.Y < newMax.Y)
				newMax.Y = rectMax.Y;

			if (W < T(0))
				std::swap(newMin.X, newMax.X);
			if (H < T(0))
				std::swap(newMin.Y, newMax.Y);

			SetMinMax(newMin, newMax);
		}

		template<class T>
		inline void Rect<T>::Union(const Rect& rect)
		{
			const Vector2<T> rectMin = rect.Min();
			const Vector2<T> rectMax = rect.Max();

			Vector2<T> newMin = Min();
			Vector2<T> newMax = Max();
			if (rectMin.X < newMin.X)
				newMin.X = rectMin.X;
			if (rectMin.Y < newMin.Y)
				newMin.Y = rectMin.Y;
			if (newMax.X < rectMax.X)
				newMax.X = rectMax.X;
			if (newMax.Y < rectMax.Y)
				newMax.Y = rectMax.Y;

			if (W < T(0))
				std::swap(newMin.X, newMax.X);
			if (H < T(0))
				std::swap(newMin.Y, newMax.Y);

			SetMinMax(newMin, newMax);
		}

		template<class T>
		inline bool Rect<T>::operator==(const Rect& rect) const
		{
			return (X == rect.X && Y == rect.Y &&
					W == rect.W && H == rect.H);
		}

		template<class T>
		inline bool Rect<T>::operator!=(const Rect& rect) const
		{
			return (X != rect.X || Y != rect.Y ||
					W != rect.W || H != rect.H);
		}

		template<class T>
		const Rect<T> Rect<T>::Empty(0, 0, 0, 0);
	}
}
