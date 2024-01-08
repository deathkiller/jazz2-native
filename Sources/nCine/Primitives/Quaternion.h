#pragma once

#include "Matrix4x4.h"
#include "../../Common.h"

namespace nCine
{
	/// A quaternion class based on templates
	template<class T>
	class Quaternion
	{
	public:
		T X, Y, Z, W;

		Quaternion()
			: X(0), Y(0), Z(0), W(1) {}
		Quaternion(T x, T y, T z, T w)
			: X(x), Y(y), Z(z), W(w) {}
		explicit Quaternion(const Vector4<T>& v)
			: X(v.X), Y(v.Y), Z(v.Z), W(v.W) {}
		Quaternion(const Quaternion& other)
			: X(other.X), Y(other.Y), Z(other.Z), W(other.W) {}
		Quaternion& operator=(const Quaternion& other);

		void Set(T x, T y, T z, T w);

		T* Data();
		const T* Data() const;

		T& operator[](unsigned int index);
		const T& operator[](unsigned int index) const;

		bool operator==(const Quaternion& q) const;
		bool operator!=(const Quaternion& q) const;
		Quaternion operator-() const;

		Quaternion& operator+=(const Quaternion& q);
		Quaternion& operator-=(const Quaternion& q);
		Quaternion& operator*=(const Quaternion& q);

		Quaternion& operator*=(T s);
		Quaternion& operator/=(T s);

		Quaternion operator+(const Quaternion& q) const;
		Quaternion operator-(const Quaternion& q) const;
		Quaternion operator*(const Quaternion& q) const;

		Quaternion operator*(T s) const;
		Quaternion operator/(T s) const;

		T Magnitude() const;
		T SqrMagnitude() const;
		Quaternion Normalized() const;
		Quaternion& Normalize();
		Quaternion Conjugated() const;
		Quaternion& Conjugate();

		Matrix4x4<T> ToMatrix4x4() const;
		static Quaternion FromAxisAngle(T xx, T yy, T zz, T degrees);
		static Quaternion FromAxisAngle(const Vector3<T>& axis, T degrees);
		static Quaternion FromXAxisAngle(T degrees);
		static Quaternion FromYAxisAngle(T degrees);
		static Quaternion FromZAxisAngle(T degrees);

		/// A quaternion with all zero elements
		static const Quaternion Zero;
		/// An identity quaternion
		static const Quaternion Identity;
	};

	using Quaternionf = Quaternion<float>;

	template<class T>
	inline Quaternion<T>& Quaternion<T>::operator=(const Quaternion<T>& other)
	{
		X = other.X;
		Y = other.Y;
		Z = other.Z;
		W = other.W;

		return *this;
	}

	template<class T>
	inline void Quaternion<T>::Set(T x, T y, T z, T w)
	{
		X = x;
		Y = y;
		Z = z;
		W = w;
	}

	template<class T>
	inline T* Quaternion<T>::Data()
	{
		return &X;
	}

	template<class T>
	inline const T* Quaternion<T>::Data() const
	{
		return &X;
	}

	template<class T>
	inline T& Quaternion<T>::operator[](unsigned int index)
	{
		ASSERT(index < 4);
		return (&X)[index];
	}

	template<class T>
	inline const T& Quaternion<T>::operator[](unsigned int index) const
	{
		ASSERT(index < 4);
		return (&X)[index];
	}

	template<class T>
	inline bool Quaternion<T>::operator==(const Quaternion& q) const
	{
		return (X == q.X && Y == q.Y && Z == q.Z && W == q.W);
	}

	template<class T>
	inline bool Quaternion<T>::operator!=(const Quaternion& q) const
	{
		return (X != q.X || Y != q.Y || Z != q.Z || W != q.W);
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::operator-() const
	{
		return Quaternion(-X, -Y, -Z, W);
	}

	template<class T>
	inline Quaternion<T>& Quaternion<T>::operator+=(const Quaternion& q)
	{
		X += q.X;
		Y += q.Y;
		Z += q.Z;
		W += q.W;

		return *this;
	}

	template<class T>
	inline Quaternion<T>& Quaternion<T>::operator-=(const Quaternion& q)
	{
		X -= q.X;
		Y -= q.Y;
		Z -= q.Z;
		W -= q.W;

		return *this;
	}

	template<class T>
	inline Quaternion<T>& Quaternion<T>::operator*=(const Quaternion& q)
	{
		const Quaternion<T> q0 = *this;

		X = q0.W * q.X + q0.X * q.W + q0.Y * q.Z - q0.Z * q.Y;
		Y = q0.W * q.Y + q0.Y * q.W + q0.Z * q.X - q0.X * q.Z;
		Z = q0.W * q.Z + q0.Z * q.W + q0.X * q.Y - q0.Y * q.X;
		W = q0.W * q.W - q0.X * q.X - q0.Y * q.Y - q0.Z * q.Z;

		return *this;
	}

	template<class T>
	inline Quaternion<T>& Quaternion<T>::operator*=(T s)
	{
		X *= s;
		Y *= s;
		Z *= s;
		W *= s;

		return *this;
	}

	template<class T>
	inline Quaternion<T>& Quaternion<T>::operator/=(T s)
	{
		X /= s;
		Y /= s;
		Z /= s;
		W /= s;

		return *this;
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::operator+(const Quaternion& q) const
	{
		return Quaternion(X + q.X,
						  Y + q.Y,
						  Z + q.Z,
						  W + q.W);
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::operator-(const Quaternion& q) const
	{
		return Quaternion(X - q.X,
						  Y - q.Y,
						  Z - q.Z,
						  W - q.W);
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::operator*(const Quaternion& q) const
	{
		return Quaternion(W * q.X + X * q.W + Y * q.Z - Z * q.Y,
						  W * q.Y + Y * q.W + Z * q.X - X * q.Z,
						  W * q.Z + Z * q.W + X * q.Y - Y * q.X,
						  W * q.W - X * q.X - Y * q.Y - Z * q.Z);
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::operator*(T s) const
	{
		return Quaternion(X * s,
						  Y * s,
						  Z * s,
						  W * s);
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::operator/(T s) const
	{
		return Quaternion(X / s,
						  Y / s,
						  Z / s,
						  W / s);
	}

	template<class T>
	inline T Quaternion<T>::Magnitude() const
	{
		return sqrt(X * X + Y * Y + Z * Z + W * W);
	}

	template<class T>
	inline T Quaternion<T>::SqrMagnitude() const
	{
		return X * X + Y * Y + Z * Z + W * W;
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::Normalized() const
	{
		const T mag = magnitude();
		return Quaternion(X / mag, Y / mag, Z / mag, W / mag);
	}

	template<class T>
	inline Quaternion<T>& Quaternion<T>::Normalize()
	{
		const T mag = magnitude();

		X /= mag;
		Y /= mag;
		Z /= mag;
		W /= mag;

		return *this;
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::Conjugated() const
	{
		return Quaternion(-X, -Y, -Z, W);
	}

	template<class T>
	inline Quaternion<T>& Quaternion<T>::Conjugate()
	{
		X = -X;
		Y = -Y;
		Z = -Z;

		return *this;
	}

	template<class T>
	inline Matrix4x4<T> Quaternion<T>::ToMatrix4x4() const
	{
		const T x2 = X * 2;
		const T y2 = Y * 2;
		const T z2 = Z * 2;

		const T xx = X * x2;
		const T xy = X * y2;
		const T xz = X * z2;
		const T yy = Y * y2;
		const T yz = Y * z2;
		const T zz = Z * z2;

		const T xw = W * x2;
		const T yw = W * y2;
		const T zw = W * z2;

		return Matrix4x4<T>(Vector4<T>(1 - (yy + zz), xy + zw, xz - yw, 0),
							Vector4<T>(xy - zw, 1 - (xx + zz), yz + xw, 0),
							Vector4<T>(xz + yw, yz - xw, 1 - (xx + yy), 0),
							Vector4<T>(0, 0, 0, 1));
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::FromAxisAngle(T xx, T yy, T zz, T degrees)
	{
		const T halfRadians = static_cast<T>(degrees * 0.5f) * (static_cast<T>(Pi) / 180);
		const T sinus = sin(halfRadians);

		return Quaternion<T>(xx * sinus,
							 yy * sinus,
							 zz * sinus,
							 cos(halfRadians));
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::FromAxisAngle(const Vector3<T>& axis, T degrees)
	{
		return fromAxisAngle(axis.X, axis.Y, axis.Z, degrees);
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::FromXAxisAngle(T degrees)
	{
		const T halfRadians = static_cast<T>(degrees * 0.5f) * (static_cast<T>(Pi) / 180);
		return Quaternion<T>(sin(halfRadians), 0, 0, cos(halfRadians));
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::FromYAxisAngle(T degrees)
	{
		const T halfRadians = static_cast<T>(degrees * 0.5f) * (static_cast<T>(Pi) / 180);
		return Quaternion<T>(0, sin(halfRadians), 0, cos(halfRadians));
	}

	template<class T>
	inline Quaternion<T> Quaternion<T>::FromZAxisAngle(T degrees)
	{
		const T halfRadians = static_cast<T>(degrees * 0.5f) * (static_cast<T>(Pi) / 180);
		return Quaternion<T>(0, 0, sin(halfRadians), cos(halfRadians));
	}

	template<class T>
	const Quaternion<T> Quaternion<T>::Zero(0, 0, 0, 0);
	template<class T>
	const Quaternion<T> Quaternion<T>::Identity(0, 0, 0, 1);

}
