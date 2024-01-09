#pragma once

#include "../../Common.h"

#include <cmath>

namespace nCine
{
	/// A two component vector based on templates
	template<class T>
	class Vector2
	{
	public:
		T X, Y;

		Vector2() noexcept
			: X(0), Y(0) {}
		explicit Vector2(T s) noexcept
			: X(s), Y(s) {}
		Vector2(T x, T y) noexcept
			: X(x), Y(y) {}
		Vector2(const Vector2& other) noexcept
			: X(other.X), Y(other.Y) {}
		Vector2(Vector2&& other) noexcept
			: X(other.X), Y(other.Y) {}
		Vector2& operator=(const Vector2& other) noexcept;
		Vector2& operator=(Vector2&& other) noexcept;

		void Set(T x, T y);

		T* Data();
		const T* Data() const;

		T& operator[](unsigned int index);
		const T& operator[](unsigned int index) const;

		bool operator==(const Vector2& v) const;
		bool operator!=(const Vector2& v) const;
		Vector2 operator-() const;

		Vector2& operator+=(const Vector2& v);
		Vector2& operator-=(const Vector2& v);
		Vector2& operator*=(const Vector2& v);
		Vector2& operator/=(const Vector2& v);

		Vector2& operator+=(T s);
		Vector2& operator-=(T s);
		Vector2& operator*=(T s);
		Vector2& operator/=(T s);

		Vector2 operator+(const Vector2& v) const;
		Vector2 operator-(const Vector2& v) const;
		Vector2 operator*(const Vector2& v) const;
		Vector2 operator/(const Vector2& v) const;

		Vector2 operator+(T s) const;
		Vector2 operator-(T s) const;
		Vector2 operator*(T s) const;
		Vector2 operator/(T s) const;

		template<class S>
		friend Vector2<S> operator*(S s, const Vector2<S>& v);

		T Length() const;
		T SqrLength() const;
		Vector2 Normalized() const;
		Vector2& Normalize();

		template<class S>
		Vector2<S> As() {
			return Vector2<S>(static_cast<S>(X), static_cast<S>(Y));
		}

		static T Dot(const Vector2& v1, const Vector2& v2);
		static Vector2 Lerp(const Vector2& a, const Vector2& b, float t);
		static Vector2 FromAngleLength(T angle, T length);

		/// A vector with all zero elements
		static const Vector2 Zero;
		/// A unit vector on the X axis
		static const Vector2 XAxis;
		/// A unit vector on the Y axis
		static const Vector2 YAxis;
	};

	using Vector2f = Vector2<float>;
	using Vector2i = Vector2<int>;

	template<class T>
	inline Vector2<T>& Vector2<T>::operator=(const Vector2<T>& other) noexcept
	{
		X = other.X;
		Y = other.Y;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator=(Vector2<T>&& other) noexcept
	{
		X = other.X;
		Y = other.Y;

		return *this;
	}

	template<class T>
	inline void Vector2<T>::Set(T x, T y)
	{
		X = x;
		Y = y;
	}

	template<class T>
	inline T* Vector2<T>::Data()
	{
		return &X;
	}

	template<class T>
	inline const T* Vector2<T>::Data() const
	{
		return &X;
	}

	template<class T>
	inline T& Vector2<T>::operator[](unsigned int index)
	{
		ASSERT(index < 2);
		return (&X)[index];
	}

	template<class T>
	inline const T& Vector2<T>::operator[](unsigned int index) const
	{
		ASSERT(index < 2);
		return (&X)[index];
	}

	template<class T>
	inline bool Vector2<T>::operator==(const Vector2& v) const
	{
		return (X == v.X && Y == v.Y);
	}

	template<class T>
	inline bool Vector2<T>::operator!=(const Vector2& v) const
	{
		return (X != v.X || Y != v.Y);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator-() const
	{
		return Vector2(-X, -Y);
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator+=(const Vector2& v)
	{
		X += v.X;
		Y += v.Y;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator-=(const Vector2& v)
	{
		X -= v.X;
		Y -= v.Y;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator*=(const Vector2& v)
	{
		X *= v.X;
		Y *= v.Y;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator/=(const Vector2& v)
	{
		X /= v.X;
		Y /= v.Y;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator+=(T s)
	{
		X += s;
		Y += s;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator-=(T s)
	{
		X -= s;
		Y -= s;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator*=(T s)
	{
		X *= s;
		Y *= s;

		return *this;
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::operator/=(T s)
	{
		X /= s;
		Y /= s;

		return *this;
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator+(const Vector2& v) const
	{
		return Vector2(X + v.X, Y + v.Y);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator-(const Vector2& v) const
	{
		return Vector2(X - v.X, Y - v.Y);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator*(const Vector2& v) const
	{
		return Vector2(X * v.X, Y * v.Y);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator/(const Vector2& v) const
	{
		return Vector2(X / v.X, Y / v.Y);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator+(T s) const
	{
		return Vector2(X + s, Y + s);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator-(T s) const
	{
		return Vector2(X - s, Y - s);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator*(T s) const
	{
		return Vector2(X * s, Y * s);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::operator/(T s) const
	{
		return Vector2(X / s, Y / s);
	}

	template<class S>
	inline Vector2<S> operator*(S s, const Vector2<S>& v)
	{
		return Vector2<S>(s * v.X,
						  s * v.Y);
	}

	template<class T>
	inline T Vector2<T>::Length() const
	{
		return (T)sqrt(X * X + Y * Y);
	}

	template<class T>
	inline T Vector2<T>::SqrLength() const
	{
		return X * X + Y * Y;
	}

	template<class T>
	inline Vector2<T> Vector2<T>::Normalized() const
	{
		const T len = Length();
		return Vector2(X / len, Y / len);
	}

	template<class T>
	inline Vector2<T>& Vector2<T>::Normalize()
	{
		const T len = Length();

		X /= len;
		Y /= len;

		return *this;
	}

	template<class T>
	inline T Vector2<T>::Dot(const Vector2<T>& v1, const Vector2<T>& v2)
	{
		return static_cast<T>(v1.X * v2.X + v1.Y * v2.Y);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::Lerp(const Vector2<T>& a, const Vector2<T>& b, float t)
	{
		return Vector2<T>(t * (b.X - a.X) + a.X, t * (b.Y - a.Y) + a.Y);
	}

	template<class T>
	inline Vector2<T> Vector2<T>::FromAngleLength(T angle, T length)
	{
		return Vector2<T>((T)sinf(angle) * length, (T)cosf(angle) * -length);
	}

	template<class T>
	const Vector2<T> Vector2<T>::Zero(0, 0);
	template<class T>
	const Vector2<T> Vector2<T>::XAxis(1, 0);
	template<class T>
	const Vector2<T> Vector2<T>::YAxis(0, 1);

}
