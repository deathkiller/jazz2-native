#pragma once

#include "Vector2.h"
#include "Vector3.h"
#include "../../Common.h"

#include <cmath>

namespace nCine
{
	/// A four component vector based on templates
	template <class T>
	class Vector4
	{
	public:
		T X, Y, Z, W;

		Vector4() noexcept
			: X(0), Y(0), Z(0), W(0) {}
		explicit Vector4(T s) noexcept
			: X(s), Y(s), Z(s), W(s) {}
		Vector4(T xx, T yy, T zz, T ww) noexcept
			: X(xx), Y(yy), Z(zz), W(ww) {}
		Vector4(const Vector4& other) noexcept
			: X(other.X), Y(other.Y), Z(other.Z), W(other.W) {}
		Vector4(Vector4&& other) noexcept
			: X(other.X), Y(other.Y), Z(other.Z), W(other.W) {}
		Vector4& operator=(const Vector4& other) noexcept;
		Vector4& operator=(Vector4&& other) noexcept;

		void Set(T xx, T yy, T zz, T ww);

		T* Data();
		const T* Data() const;

		T& operator[](unsigned int index);
		const T& operator[](unsigned int index) const;

		bool operator==(const Vector4& v) const;
		bool operator!=(const Vector4& v) const;
		Vector4 operator-() const;

		Vector4& operator+=(const Vector4& v);
		Vector4& operator-=(const Vector4& v);
		Vector4& operator*=(const Vector4& v);
		Vector4& operator/=(const Vector4& v);

		Vector4& operator+=(T s);
		Vector4& operator-=(T s);
		Vector4& operator*=(T s);
		Vector4& operator/=(T s);

		Vector4 operator+(const Vector4& v) const;
		Vector4 operator-(const Vector4& v) const;
		Vector4 operator*(const Vector4& v) const;
		Vector4 operator/(const Vector4& v) const;

		Vector4 operator+(T s) const;
		Vector4 operator-(T s) const;
		Vector4 operator*(T s) const;
		Vector4 operator/(T s) const;

		template <class S>
		friend Vector4<S> operator*(S s, const Vector4<S>& v);

		T Length() const;
		T SqrLength() const;
		Vector4 Normalized() const;
		Vector4& Normalize();

		Vector2<T> ToVector2() const;
		Vector3<T> ToVector3() const;

		template <class S>
		friend S Dot(const Vector4<S>& v1, const Vector4<S>& v2);

		/// A vector with all zero elements
		static const Vector4 Zero;
		/// A unit vector on the X axis
		static const Vector4 XAxis;
		/// A unit vector on the Y axis
		static const Vector4 YAxis;
		/// A unit vector on the Z axis
		static const Vector4 ZAxis;
		/// A unit vector on the W axis
		static const Vector4 WAxis;
	};

	using Vector4f = Vector4<float>;
	using Vector4i = Vector4<int>;

	template <class T>
	inline Vector4<T>& Vector4<T>::operator=(const Vector4<T>& other) noexcept
	{
		X = other.X;
		Y = other.Y;
		Z = other.Z;
		W = other.W;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator=(Vector4<T>&& other) noexcept
	{
		X = other.X;
		Y = other.Y;
		Z = other.Z;
		W = other.W;

		return *this;
	}

	template <class T>
	inline void Vector4<T>::Set(T xx, T yy, T zz, T ww)
	{
		X = xx;
		Y = yy;
		Z = zz;
		W = ww;
	}

	template <class T>
	inline T* Vector4<T>::Data()
	{
		return &X;
	}

	template <class T>
	inline const T* Vector4<T>::Data() const
	{
		return &X;
	}

	template <class T>
	inline T& Vector4<T>::operator[](unsigned int index)
	{
		ASSERT(index < 4);
		return (&X)[index];
	}

	template <class T>
	inline const T& Vector4<T>::operator[](unsigned int index) const
	{
		ASSERT(index < 4);
		return (&X)[index];
	}

	template <class T>
	inline bool Vector4<T>::operator==(const Vector4& v) const
	{
		return (X == v.X && Y == v.Y && Z == v.Z && W == v.W);
	}

	template <class T>
	inline bool Vector4<T>::operator!=(const Vector4& v) const
	{
		return (X != v.X || Y != v.Y || Z != v.Z || W != v.W);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator-() const
	{
		return Vector4(-X, -Y, -Z, -W);
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator+=(const Vector4& v)
	{
		X += v.X;
		Y += v.Y;
		Z += v.Z;
		W += v.W;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator-=(const Vector4& v)
	{
		X -= v.X;
		Y -= v.Y;
		Z -= v.Z;
		W -= v.W;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator*=(const Vector4& v)
	{
		X *= v.X;
		Y *= v.Y;
		Z *= v.Z;
		W *= v.W;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator/=(const Vector4& v)
	{
		X /= v.X;
		Y /= v.Y;
		Z /= v.Z;
		W /= v.W;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator+=(T s)
	{
		X += s;
		Y += s;
		Z += s;
		W += s;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator-=(T s)
	{
		X -= s;
		Y -= s;
		Z -= s;
		W -= s;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator*=(T s)
	{
		X *= s;
		Y *= s;
		Z *= s;
		W *= s;

		return *this;
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::operator/=(T s)
	{
		X /= s;
		Y /= s;
		Z /= s;
		W /= s;

		return *this;
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator+(const Vector4& v) const
	{
		return Vector4(X + v.X,
					   Y + v.Y,
					   Z + v.Z,
					   W + v.W);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator-(const Vector4& v) const
	{
		return Vector4(X - v.X,
					   Y - v.Y,
					   Z - v.Z,
					   W - v.W);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator*(const Vector4& v) const
	{
		return Vector4(X * v.X,
					   Y * v.Y,
					   Z * v.Z,
					   W * v.W);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator/(const Vector4& v) const
	{
		return Vector4(X / v.X,
					   Y / v.Y,
					   Z / v.Z,
					   W / v.W);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator+(T s) const
	{
		return Vector4(X + s,
					   Y + s,
					   Z + s,
					   W + s);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator-(T s) const
	{
		return Vector4(X - s,
					   Y - s,
					   Z - s,
					   W - s);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator*(T s) const
	{
		return Vector4(X * s,
					   Y * s,
					   Z * s,
					   W * s);
	}

	template <class T>
	inline Vector4<T> Vector4<T>::operator/(T s) const
	{
		return Vector4(X / s,
					   Y / s,
					   Z / s,
					   W / s);
	}

	template <class S>
	inline Vector4<S> operator*(S s, const Vector4<S>& v)
	{
		return Vector4<S>(s * v.X,
						  s * v.Y,
						  s * v.Z,
						  s * v.W);
	}

	template <class T>
	inline T Vector4<T>::Length() const
	{
		return (T)sqrt(X * X + Y * Y + Z * Z + W * W);
	}

	template <class T>
	inline T Vector4<T>::SqrLength() const
	{
		return X * X + Y * Y + Z * Z + W * W;
	}

	template <class T>
	inline Vector4<T> Vector4<T>::Normalized() const
	{
		const T len = Length();
		return Vector4(X / len, Y / len, Z / len, W / len);
	}

	template <class T>
	inline Vector4<T>& Vector4<T>::Normalize()
	{
		const T len = Length();

		X /= len;
		Y /= len;
		Z /= len;
		W /= len;

		return *this;
	}

	template <class T>
	inline Vector2<T> Vector4<T>::ToVector2() const
	{
		return Vector2<T>(X, Y);
	}

	template <class T>
	inline Vector3<T> Vector4<T>::ToVector3() const
	{
		return Vector3<T>(X, Y, Z);
	}

	template <class S>
	inline S Dot(const Vector4<S>& v1, const Vector4<S>& v2)
	{
		return static_cast<S>(v1.X * v2.X +
							  v1.Y * v2.Y +
							  v1.Z * v2.Z +
							  v1.W * v2.W);
	}

	template <class T>
	const Vector4<T> Vector4<T>::Zero(0, 0, 0, 0);
	template <class T>
	const Vector4<T> Vector4<T>::XAxis(1, 0, 0, 0);
	template <class T>
	const Vector4<T> Vector4<T>::YAxis(0, 1, 0, 0);
	template <class T>
	const Vector4<T> Vector4<T>::ZAxis(0, 0, 1, 0);
	template <class T>
	const Vector4<T> Vector4<T>::WAxis(0, 0, 0, 1);

}
