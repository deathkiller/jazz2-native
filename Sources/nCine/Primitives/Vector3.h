#pragma once

#include "Vector2.h"
#include "../../Common.h"

#include <cmath>

namespace nCine
{
	/// A three component vector based on templates
	template <class T>
	class Vector3
	{
	public:
		T X, Y, Z;

		Vector3() noexcept
			: X(0), Y(0), Z(0) {}
		explicit Vector3(T s) noexcept
			: X(s), Y(s), Z(s) {}
		Vector3(T xx, T yy, T zz) noexcept
			: X(xx), Y(yy), Z(zz) {}
		Vector3(const Vector3& other) noexcept
			: X(other.X), Y(other.Y), Z(other.Z) {}
		Vector3(Vector3&& other) noexcept
			: X(other.X), Y(other.Y), Z(other.Z) {}
		Vector3& operator=(const Vector3& other) noexcept;
		Vector3& operator=(Vector3&& other) noexcept;

		void Set(T xx, T yy, T zz);

		T* Data();
		const T* Data() const;

		T& operator[](unsigned int index);
		const T& operator[](unsigned int index) const;

		bool operator==(const Vector3& v) const;
		bool operator!=(const Vector3& v) const;
		Vector3 operator-() const;

		Vector3& operator+=(const Vector3& v);
		Vector3& operator-=(const Vector3& v);
		Vector3& operator*=(const Vector3& v);
		Vector3& operator/=(const Vector3& v);

		Vector3& operator+=(T s);
		Vector3& operator-=(T s);
		Vector3& operator*=(T s);
		Vector3& operator/=(T s);

		Vector3 operator+(const Vector3& v) const;
		Vector3 operator-(const Vector3& v) const;
		Vector3 operator*(const Vector3& v) const;
		Vector3 operator/(const Vector3& v) const;

		Vector3 operator+(T s) const;
		Vector3 operator-(T s) const;
		Vector3 operator*(T s) const;
		Vector3 operator/(T s) const;

		template <class S>
		friend Vector3<S> operator*(S s, const Vector3<S>& v);

		T Length() const;
		T SqrLength() const;
		Vector3 Normalized() const;
		Vector3& Normalize();

		Vector2<T> ToVector2() const;

		template <class S>
		friend S Dot(const Vector3<S>& v1, const Vector3<S>& v2);
		template <class S>
		friend Vector3<S> Cross(const Vector3<S>& v1, const Vector3<S>& v2);

		/// A vector with all zero elements
		static const Vector3 Zero;
		/// A unit vector on the X axis
		static const Vector3 XAxis;
		/// A unit vector on the Y axis
		static const Vector3 YAxis;
		/// A unit vector on the Z axis
		static const Vector3 ZAxis;
	};

	using Vector3f = Vector3<float>;
	using Vector3i = Vector3<int>;

	template <class T>
	inline Vector3<T>& Vector3<T>::operator=(const Vector3<T>& other) noexcept
	{
		X = other.X;
		Y = other.Y;
		Z = other.Z;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator=(Vector3<T>&& other) noexcept
	{
		X = other.X;
		Y = other.Y;
		Z = other.Z;

		return *this;
	}

	template <class T>
	inline void Vector3<T>::Set(T xx, T yy, T zz)
	{
		X = xx;
		Y = yy;
		Z = zz;
	}

	template <class T>
	inline T* Vector3<T>::Data()
	{
		return &X;
	}

	template <class T>
	inline const T* Vector3<T>::Data() const
	{
		return &X;
	}

	template <class T>
	inline T& Vector3<T>::operator[](unsigned int index)
	{
		ASSERT(index < 3);
		return (&X)[index];
	}

	template <class T>
	inline const T& Vector3<T>::operator[](unsigned int index) const
	{
		ASSERT(index < 3);
		return (&X)[index];
	}

	template <class T>
	inline bool Vector3<T>::operator==(const Vector3& v) const
	{
		return (X == v.X && Y == v.Y && Z == v.Z);
	}

	template <class T>
	inline bool Vector3<T>::operator!=(const Vector3& v) const
	{
		return (X != v.X || Y != v.Y || Z != v.Z);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator-() const
	{
		return Vector3(-X, -Y, -Z);
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator+=(const Vector3& v)
	{
		X += v.X;
		Y += v.Y;
		Z += v.Z;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator-=(const Vector3& v)
	{
		X -= v.X;
		Y -= v.Y;
		Z -= v.Z;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator*=(const Vector3& v)
	{
		X *= v.X;
		Y *= v.Y;
		Z *= v.Z;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator/=(const Vector3& v)
	{
		X /= v.X;
		Y /= v.Y;
		Z /= v.Z;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator+=(T s)
	{
		X += s;
		Y += s;
		Z += s;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator-=(T s)
	{
		X -= s;
		Y -= s;
		Z -= s;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator*=(T s)
	{
		X *= s;
		Y *= s;
		Z *= s;

		return *this;
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::operator/=(T s)
	{
		X /= s;
		Y /= s;
		Z /= s;

		return *this;
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator+(const Vector3& v) const
	{
		return Vector3(X + v.X,
					   Y + v.Y,
					   Z + v.Z);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator-(const Vector3& v) const
	{
		return Vector3(X - v.X,
					   Y - v.Y,
					   Z - v.Z);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator*(const Vector3& v) const
	{
		return Vector3(X * v.X,
					   Y * v.Y,
					   Z * v.Z);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator/(const Vector3& v) const
	{
		return Vector3(X / v.X,
					   Y / v.Y,
					   Z / v.Z);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator+(T s) const
	{
		return Vector3(X + s,
					   Y + s,
					   Z + s);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator-(T s) const
	{
		return Vector3(X - s,
					   Y - s,
					   Z - s);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator*(T s) const
	{
		return Vector3(X * s,
					   Y * s,
					   Z * s);
	}

	template <class T>
	inline Vector3<T> Vector3<T>::operator/(T s) const
	{
		return Vector3(X / s,
					   Y / s,
					   Z / s);
	}

	template <class S>
	inline Vector3<S> operator*(S s, const Vector3<S>& v)
	{
		return Vector3<S>(s * v.X,
						  s * v.Y,
						  s * v.Z);
	}

	template <class T>
	inline T Vector3<T>::Length() const
	{
		return (T)sqrt(X * X + Y * Y + Z * Z);
	}

	template <class T>
	inline T Vector3<T>::SqrLength() const
	{
		return X * X + Y * Y + Z * Z;
	}

	template <class T>
	inline Vector3<T> Vector3<T>::Normalized() const
	{
		const T len = Length();
		return Vector3(X / len, Y / len, Z / len);
	}

	template <class T>
	inline Vector3<T>& Vector3<T>::Normalize()
	{
		const T len = Length();

		X /= len;
		Y /= len;
		Z /= len;

		return *this;
	}

	template <class T>
	inline Vector2<T> Vector3<T>::ToVector2() const
	{
		return Vector2<T>(X, Y);
	}

	template <class S>
	inline S dot(const Vector3<S>& v1, const Vector3<S>& v2)
	{
		return static_cast<S>(v1.X * v2.X +
							  v1.Y * v2.Y +
							  v1.Z * v2.Z);
	}

	template <class S>
	inline Vector3<S> Cross(const Vector3<S>& v1, const Vector3<S>& v2)
	{
		return Vector3<S>(v1.Y * v2.Z - v1.Z * v2.Y,
						  v1.Z * v2.X - v1.X * v2.Z,
						  v1.X * v2.Y - v1.Y * v2.X);
	}

	template <class T>
	const Vector3<T> Vector3<T>::Zero(0, 0, 0);
	template <class T>
	const Vector3<T> Vector3<T>::XAxis(1, 0, 0);
	template <class T>
	const Vector3<T> Vector3<T>::YAxis(0, 1, 0);
	template <class T>
	const Vector3<T> Vector3<T>::ZAxis(0, 0, 1);

}
