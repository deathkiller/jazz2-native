#pragma once

#include "Vector2.h"

#include <algorithm>

#include <Containers/Tags.h>

namespace nCine
{
	inline namespace Primitives
	{
		using Death::Containers::NoInitT;

		/// Axis-Aligned Bounding Box in a two dimensional space
		template<class S>
		class AABB
		{
		public:
			/// Top-left X coordinate
			S L;
			/// Top-left Y coordinate
			S T;
			/// Bottom-right X coordinate
			S R;
			/// Bottom-right Y coordinate
			S B;

			constexpr AABB() noexcept
				: L(S(0)), T(S(0)), R(S(0)), B(S(0)) {
			}

			explicit AABB(NoInitT) noexcept
			{
			}

			constexpr AABB(S l, S t, S r, S b) noexcept
				: L(l), T(t), R(r), B(b) {
			}

			constexpr AABB(const Vector2<S>& min, const Vector2<S>& max) noexcept
				: L(std::min(min.X, max.X)), T(std::min(min.Y, max.Y)), R(std::max(min.X, max.X)), B(std::max(min.Y, max.Y)) {
			}

			template<class U>
			constexpr AABB<U> As() {
				return AABB<S>(static_cast<U>(L), static_cast<U>(T), static_cast<U>(R), static_cast<U>(B));
			}

			constexpr S GetWidth() const {
				return R - L;
			}
			constexpr S GetHeight() const {
				return B - T;
			}

			/// Calculates the center of the rectangle
			Vector2<S> GetCenter() const;
			/// Returns extents
			Vector2<S> GetExtents() const;
			/// Returns perimeter
			S GetPerimeter() const;

			/// Returns `true` if the point is inside this rectangle
			bool Contains(S px, S py) const;
			/// Returns `true` if the point vector is inside this rectangle
			bool Contains(const Vector2<S>& p) const;

			/// Returns `true` if the other rectangle is contained inside this one
			bool Contains(const AABB<S>& aabb) const;
			/// Returns `true` if this rect does overlap the other rectangle in any way
			bool Overlaps(const AABB<S>& aabb) const;

			/// Intersects this AABB with the other AABB
			static AABB<S> Intersect(const AABB<S>& a, const AABB<S>& b);

			/// Combines two AABBs
			static AABB<S> Combine(const AABB<S>& a, const AABB<S>& b);

			/// Eqality operator
			bool operator==(const AABB& aabb) const;
			bool operator!=(const AABB& aabb) const;

			AABB& operator+=(const Vector2<S>& v);
			AABB& operator-=(const Vector2<S>& v);

			AABB operator+(const Vector2<S>& v) const;
			AABB operator-(const Vector2<S>& v) const;
		};

		/// Axis-Aligned Bounding Box in a two dimensional space of floats
		using AABBf = AABB<float>;
		/// Axis-Aligned Bounding Box in a two dimensional space of 32-bit integers
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
