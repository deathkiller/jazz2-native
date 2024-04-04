#pragma once

#include "Vector2.h"

#include <algorithm>

namespace nCine
{
	/// A template-based rectangle in a two dimensional space
	template<class T>
	class Rect
	{
	public:
		/// Top-left X coordinate as a public property (left with positive width)
		T X;
		/// Top-left Y coordinate as a public property (top with positive height)
		T Y;
		/// Width as a public property
		T W;
		/// Height as a public property
		T H;

		/// Default constructor, all zeros
		Rect()
			: X(0), Y(0), W(0), H(0) { }
		/// Constructs a rectangle from top-left point and size
		Rect(T x, T y, T w, T h)
			: X(x), Y(y), W(w), H(h) { }
		/// Constructs a rectangle from top-left point and size as two `Vector2`
		Rect(const Vector2<T>& point, const Vector2<T>& size)
			: X(point.X), Y(point.Y), W(size.X), H(size.Y) { }

		/// Creates a rectangle from center and size
		static Rect FromCenterSize(T xx, T yy, T ww, T hh);
		/// Creates a rectangle from center and size as two `Vector2`
		static Rect FromCenterSize(const Vector2<T>& center, const Vector2<T>& size);

		/// Creates a rectangle from minimum and maximum coordinates
		static Rect FromMinMax(T minX, T minY, T maxX, T maxY);
		/// Creates a rectangle from minimum and maximum coordinates as two `Vector2`
		static Rect FromMinMax(const Vector2<T>& min, const Vector2<T>& max);

		/// Calculates the center of the rectangle
		Vector2<T> Center() const;
		/// Calculates the minimum coordinates of the rectangle
		Vector2<T> Min() const;
		/// Calculates the maximum coordinates of the rectangle
		Vector2<T> Max() const;

		/// Sets rectangle top-left point and size
		void Set(T x, T y, T w, T h);
		/// Sets rectangle top-left point and size as two `Vector2`
		void Set(const Vector2<T>& point, const Vector2<T>& size);
		/// Retains rectangle size but moves its center to another position
		void SetCenter(float cx, float cy);
		/// Retains rectangle size but moves its center to another position with a `Vector2`
		void SetCenter(const Vector2<T>& center);
		/// Retains rectangle center but changes its size
		void SetSize(float ww, float hh);
		/// Retains rectangle center but changes its size with a `Vector2`
		void SetSize(const Vector2<T>& size);

		/// Sets rectangle center and size
		void SetCenterSize(T xx, T yy, T ww, T hh);
		/// Sets rectangle center and size as two `Vector2`
		void SetCenterSize(const Vector2<T>& center, const Vector2<T>& size);

		/// Sets rectangle minimum and maximum coordinates
		void SetMinMax(T minX, T minY, T maxX, T maxY);
		/// Sets rectangle minimum and maximum coordinates as two `Vector2`
		void SetMinMax(const Vector2<T>& min, const Vector2<T>& max);

		template<class S>
		Rect<S> As() {
			return Rect<S>(static_cast<S>(X), static_cast<S>(Y), static_cast<S>(W), static_cast<S>(H));
		}

		/// Inverts rectangle size and moves (x, y) to a different angle
		void InvertSize();

		/// \returns True if the point is inside this rectangle
		bool Contains(T px, T py) const;
		/// \returns True if the point vector is inside this rectangle
		bool Contains(const Vector2<T>& p) const;

		/// \returns True if the other rectangle is contained inside this one
		bool Contains(const Rect<T>& rect) const;
		/// \returns True if this rect does overlap the other rectangle in any way
		bool Overlaps(const Rect<T>& rect) const;

		/// Intersects this rectangle with the other rectangle
		void Intersect(const Rect<T>& rect);

		/// Eqality operator
		bool operator==(const Rect& rect) const;
		bool operator!=(const Rect& rect) const;

		/// Empty rectangle
		static const Rect Empty;
	};

	using Rectf = Rect<float>;
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
		const bool containsMin = Contains(rect.min());
		const bool containsMax = Contains(rect.max());
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
		if (rectMin.X > newMin.X)
			newMin.X = rectMin.X;
		if (rectMin.Y > newMin.Y)
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
