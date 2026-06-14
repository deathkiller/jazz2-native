#pragma once

#include "Matrix4x4.h"
#include "../../Main.h"

namespace nCine
{
	inline namespace Primitives
	{
		using Death::Containers::NoInitT;

		/**
			@brief Quaternion
			
			Represents a rotation as four components @ref X, @ref Y, @ref Z and @ref W. Provides
			component-wise and scalar arithmetic, quaternion multiplication (rotation composition),
			magnitude and normalization, conjugation, conversion to a rotation @ref Matrix4x4 and
			construction from an axis and an angle.
		*/
		template<class T>
		class Quaternion
		{
		public:
			T X, Y, Z, W;

			constexpr Quaternion() noexcept
				: X(T(0)), Y(T(0)), Z(T(0)), W(T(1)) {}
			explicit Quaternion(NoInitT) noexcept {}
			constexpr Quaternion(T x, T y, T z, T w) noexcept
				: X(x), Y(y), Z(z), W(w) {}
			explicit Quaternion(const Vector4<T>& v) noexcept
				: X(v.X), Y(v.Y), Z(v.Z), W(v.W) {}
			constexpr Quaternion(const Quaternion& other) noexcept
				: X(other.X), Y(other.Y), Z(other.Z), W(other.W) {}
			Quaternion& operator=(const Quaternion& other);

			void Set(T x, T y, T z, T w);

			T* Data();
			const T* Data() const;

			T& operator[](std::size_t index);
			const T& operator[](std::size_t index) const;

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

			/** @brief Returns the magnitude (norm) of the quaternion */
			T Magnitude() const;
			/** @brief Returns the squared magnitude of the quaternion */
			T SqrMagnitude() const;
			/** @brief Returns a normalized copy of the quaternion */
			Quaternion Normalized() const;
			/** @brief Normalizes the quaternion in place and returns it */
			Quaternion& Normalize();
			/** @brief Returns the conjugate of the quaternion */
			Quaternion Conjugated() const;
			/** @brief Conjugates the quaternion in place and returns it */
			Quaternion& Conjugate();

			/** @brief Converts the quaternion to an equivalent rotation matrix */
			Matrix4x4<T> ToMatrix4x4() const;
			/** @brief Creates a quaternion from a rotation axis and an angle in degrees */
			static Quaternion FromAxisAngle(T xx, T yy, T zz, T degrees);
			/** @overload */
			static Quaternion FromAxisAngle(const Vector3<T>& axis, T degrees);
			/** @brief Creates a quaternion representing a rotation around the X axis */
			static Quaternion FromXAxisAngle(T degrees);
			/** @brief Creates a quaternion representing a rotation around the Y axis */
			static Quaternion FromYAxisAngle(T degrees);
			/** @brief Creates a quaternion representing a rotation around the Z axis */
			static Quaternion FromZAxisAngle(T degrees);

			/** @{ @name Constants */

			/** @brief Quaternion with all components set to zero */
			static const Quaternion Zero;
			/** @brief Identity quaternion (no rotation) */
			static const Quaternion Identity;

			/** @} */
		};

		/** @brief Quaternion of floats */
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
		inline T& Quaternion<T>::operator[](std::size_t index)
		{
			DEATH_ASSERT(index < 4);
			return (&X)[index];
		}

		template<class T>
		inline const T& Quaternion<T>::operator[](std::size_t index) const
		{
			DEATH_ASSERT(index < 4);
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
}
