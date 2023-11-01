#pragma once

#include "Vector2.h"

#include <algorithm>

namespace nCine
{
	/// A template-based Axis-Aligned Bounding Box in a two dimensional space
	template<class S>
	class AABB
	{
	public:
		/// Top-left X coordinate as a public property
		S L;
		/// Top-left Y coordinate as a public property
		S T;
		/// Bottom-right X coordinate as a public property
		S R;
		/// Bottom-right Y coordinate as a public property
		S B;

		AABB()
			: L(0), T(0), R(0), B(0) { }

		AABB(S ll, S tt, S rr, S bb)
			: L(ll), T(tt), R(rr), B(bb) { }

		AABB(const Vector2<S>& min, const Vector2<S>& max)
			: L(std::min(min.X, max.X)), T(std::min(min.Y, max.Y)), R(std::max(min.X, max.X)), B(std::max(min.Y, max.Y)) { }

		S GetWidth() const {
			return R - L;
		}
		S GetHeight() const {
			return B - T;
		}

		/// Calculates the center of the rectangle
		Vector2<S> GetCenter() const;

		Vector2<S> GetExtents() const;

		S GetPerimeter() const;

		/// \returns True if the point is inside this rectangle
		bool Contains(S px, S py) const;
		/// \returns True if the point vector is inside this rectangle
		bool Contains(const Vector2<S>& p) const;

		/// \returns True if the other rectangle is contained inside this one
		bool Contains(const AABB<S>& aabb) const;
		/// \returns True if this rect does overlap the other rectangle in any way
		bool Overlaps(const AABB<S>& aabb) const;

		/// Intersects this rectangle with the other rectangle
		static AABB<S> Intersect(const AABB<S>& a, const AABB<S>& b);

		/// Combine two AABBs into this one.
		static AABB<S> Combine(const AABB<S>& a, const AABB<S>& b);

		/// Eqality operator
		bool operator==(const AABB& aabb) const;
		bool operator!=(const AABB& aabb) const;

		AABB& operator+=(const Vector2<S>& v);
		AABB& operator-=(const Vector2<S>& v);

		AABB operator+(const Vector2<S>& v) const;
		AABB operator-(const Vector2<S>& v) const;
	};

	using AABBf = AABB<float>;
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
