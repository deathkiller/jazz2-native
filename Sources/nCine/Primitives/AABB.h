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
			@brief Axis-aligned bounding box in a two-dimensional space
			
			Stored as the left, top, right and bottom edges (@ref L, @ref T, @ref R, @ref B).
			Provides width and height accessors, center, extents and perimeter queries,
			containment and overlap tests, intersection and combination of two boxes, and
			translation by a @ref Vector2.
		*/
		template<class S>
		class AABB
		{
		public:
			/** @brief Left edge X coordinate */
			S L;
			/** @brief Top edge Y coordinate */
			S T;
			/** @brief Right edge X coordinate */
			S R;
			/** @brief Bottom edge Y coordinate */
			S B;

			constexpr AABB() noexcept
				: L(S(0)), T(S(0)), R(S(0)), B(S(0)) {}

			explicit AABB(NoInitT) noexcept {}

			constexpr AABB(S l, S t, S r, S b) noexcept
				: L(l), T(t), R(r), B(b) {}

			constexpr AABB(const Vector2<S>& min, const Vector2<S>& max) noexcept
				: L(std::min(min.X, max.X)), T(std::min(min.Y, max.Y)), R(std::max(min.X, max.X)), B(std::max(min.Y, max.Y)) {}

			/**
			 * @brief Converts the bounding box to a different element type
			 */
			template<class U>
			constexpr AABB<U> As() {
				return AABB<S>(static_cast<U>(L), static_cast<U>(T), static_cast<U>(R), static_cast<U>(B));
			}

			/** @brief Returns the width of the box */
			constexpr S GetWidth() const {
				return R - L;
			}
			/** @brief Returns the height of the box */
			constexpr S GetHeight() const {
				return B - T;
			}

			/** @brief Returns the center of the box */
			Vector2<S> GetCenter() const;
			/** @brief Returns the half-width and half-height of the box */
			Vector2<S> GetExtents() const;
			/** @brief Returns the perimeter of the box */
			S GetPerimeter() const;

			/** @brief Returns `true` if the point is inside the box */
			bool Contains(S px, S py) const;
			/** @brief Returns `true` if the point vector is inside the box */
			bool Contains(const Vector2<S>& p) const;

			/** @brief Returns `true` if the other box is fully contained within this one */
			bool Contains(const AABB<S>& aabb) const;
			/** @brief Returns `true` if this box overlaps the other one in any way */
			bool Overlaps(const AABB<S>& aabb) const;

			/** @brief Returns the intersection of two boxes, or an empty box if they do not overlap */
			static AABB<S> Intersect(const AABB<S>& a, const AABB<S>& b);

			/** @brief Returns the smallest box that contains both boxes */
			static AABB<S> Combine(const AABB<S>& a, const AABB<S>& b);

			bool operator==(const AABB& aabb) const;
			bool operator!=(const AABB& aabb) const;

			AABB& operator+=(const Vector2<S>& v);
			AABB& operator-=(const Vector2<S>& v);

			AABB operator+(const Vector2<S>& v) const;
			AABB operator-(const Vector2<S>& v) const;
		};

		/** @brief Axis-aligned bounding box in a two-dimensional space of floats */
		using AABBf = AABB<float>;
		/** @brief Axis-aligned bounding box in a two-dimensional space of 32-bit integers */
		using AABBi = AABB<int>;

		template<class S>
		inline Vector2<S> AABB<S>::GetCenter() const
		{
			return Vector2<S>((L + R) / 2, (T + B) / 2);
		}

		template<class S>
		inline Vector2<S> AABB<S>::GetExtents() const
		{
			return Vector2<S>((R - L) / 2, (B - T) / 2);
		}

		template<class S>
		inline S AABB<S>::GetPerimeter() const
		{
			S wx = R - L;
			S wy = B - T;
			return 2 * (wx + wy);
		}

		template<class S>
		inline bool AABB<S>::Contains(S px, S py) const
		{
			// Using epsilon to try and guard against float rounding errors
			return (px > (L + std::numeric_limits<S>::epsilon) && px < (R - std::numeric_limits<S>::epsilon) &&
				   (py > (T + std::numeric_limits<S>::epsilon) && py < (B - std::numeric_limits<S>::epsilon)));
		}

		template<>
		inline bool AABB<std::int32_t>::Contains(std::int32_t px, std::int32_t py) const
		{
			return (px >= L && px <= R && py >= T && py <= B);
		}

		template<class S>
		inline bool AABB<S>::Contains(const Vector2<S>& p) const
		{
			return Contains(p.X, p.Y);
		}

		template<class S>
		inline bool AABB<S>::Contains(const AABB& aabb) const
		{
			return (L <= aabb.L && T <= aabb.T && aabb.R <= R && aabb.B <= B);
		}

		template<class S>
		inline bool AABB<S>::Overlaps(const AABB& aabb) const
		{
			return (L <= aabb.R && T <= aabb.B && R >= aabb.L && B >= aabb.T);
		}

		template<class S>
		inline AABB<S> AABB<S>::Intersect(const AABB<S>& a, const AABB<S>& b)
		{
			if (!a.Overlaps(b)) {
				return AABB();
			}

			return AABB(std::max(a.L, b.L), std::max(a.T, b.T), std::min(a.R, b.R), std::min(a.B, b.B));
		}

		template<class S>
		inline AABB<S> AABB<S>::Combine(const AABB<S>& a, const AABB<S>& b)
		{
			return AABB(std::min(a.L, b.L), std::min(a.T, b.T), std::max(a.R, b.R), std::max(a.B, b.B));
		}

		template<class S>
		inline bool AABB<S>::operator==(const AABB& aabb) const
		{
			return (L == aabb.L && T == aabb.T &&
					R == aabb.R && B == aabb.B);
		}

		template<class S>
		inline bool AABB<S>::operator!=(const AABB& aabb) const
		{
			return (L != aabb.L || T != aabb.T ||
					R != aabb.R || B != aabb.B);
		}

		template<class S>
		inline AABB<S>& AABB<S>::operator+=(const Vector2<S>& v)
		{
			L += v.X;
			T += v.Y;
			R += v.X;
			B += v.Y;
			return *this;
		}

		template<class S>
		inline AABB<S>& AABB<S>::operator-=(const Vector2<S>& v)
		{
			L -= v.X;
			T -= v.Y;
			R -= v.X;
			B -= v.Y;
			return *this;
		}

		template<class S>
		inline AABB<S> AABB<S>::operator+(const Vector2<S>& v) const
		{
			return AABB(L + v.X, T + v.Y, R + v.X, B + v.Y);
		}

		template<class S>
		inline AABB<S> AABB<S>::operator-(const Vector2<S>& v) const
		{
			return AABB(L - v.X, T - v.Y, R - v.X, B - v.Y);
		}
	}
}
