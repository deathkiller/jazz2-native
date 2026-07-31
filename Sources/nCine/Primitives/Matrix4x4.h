#pragma once

#include "Vector3.h"
#include "Vector4.h"
#include "../CommonConstants.h"
#include "../../Main.h"

#include <Containers/Tags.h>

namespace nCine
{
	inline namespace Primitives
	{
		using Death::Containers::NoInitT;

		/**
			@brief Four-by-four matrix
			
			Column-major 4×4 matrix stored as four @ref Vector4 columns. Provides component-wise
			and scalar arithmetic, matrix and vector multiplication, transposition and inversion,
			in-place and standalone transform builders (translation, rotation, scaling) and the
			@ref Ortho(), @ref Frustum() and @ref Perspective() projection builders.
		*/
		template<class T>
		class Matrix4x4
		{
		public:
			constexpr Matrix4x4() noexcept
				: vecs_{Vector4<T>(T(1), T(0), T(0), T(0)), Vector4<T>(T(0), T(1), T(0), T(0)), Vector4<T>(T(0), T(0), T(1), T(0)), Vector4<T>(T(0), T(0), T(0), T(1))} {}

			explicit Matrix4x4(NoInitT) noexcept {}

			Matrix4x4(const Vector4<T>& v0, const Vector4<T>& v1, const Vector4<T>& v2, const Vector4<T>& v3) noexcept;

			void Set(const Vector4<T>& v0, const Vector4<T>& v1, const Vector4<T>& v2, const Vector4<T>& v3);

			T* Data();
			const T* Data() const;

			Vector4<T>& operator[](std::size_t index);
			const Vector4<T>& operator[](std::size_t index) const;

			bool operator==(const Matrix4x4& m) const;
			bool operator!=(const Matrix4x4& m) const;
			Matrix4x4 operator-() const;

			Matrix4x4& operator+=(const Matrix4x4& m);
			Matrix4x4& operator-=(const Matrix4x4& m);
			Matrix4x4& operator*=(const Matrix4x4& m);
			Matrix4x4& operator/=(const Matrix4x4& m);

			Matrix4x4& operator+=(T s);
			Matrix4x4& operator-=(T s);
			Matrix4x4& operator*=(T s);
			Matrix4x4& operator/=(T s);

			Vector4<T> operator*(const Vector4<T>& v) const;
			Vector3<T> operator*(const Vector3<T>& v) const;

			template<class S>
			friend Vector4<S> operator*(const Vector4<S>& v, const Matrix4x4<S>& m);
			template<class S>
			friend Vector3<S> operator*(const Vector3<S>& v, const Matrix4x4<S>& m);

			Matrix4x4 operator+(const Matrix4x4& m) const;
			Matrix4x4 operator-(const Matrix4x4& m) const;
			Matrix4x4 operator*(const Matrix4x4& m) const;
			Matrix4x4 operator/(const Matrix4x4& m) const;

			Matrix4x4 operator+(T s) const;
			Matrix4x4 operator-(T s) const;
			Matrix4x4 operator*(T s) const;
			Matrix4x4 operator/(T s) const;

			template<class S>
			friend Matrix4x4<S> operator*(S s, const Matrix4x4<S>& m);

			/** @brief Returns the transpose of the matrix */
			Matrix4x4 Transposed() const;
			/** @brief Transposes the matrix in place and returns it */
			Matrix4x4& Transpose();
			/** @brief Returns the inverse of the matrix */
			Matrix4x4 Inverse() const;

			/** @brief Applies a translation to the matrix in place */
			Matrix4x4& Translate(T xx, T yy, T zz);
			/** @overload */
			Matrix4x4& Translate(const Vector3<T>& v);
			/** @brief Applies a rotation around the X axis to the matrix in place */
			Matrix4x4& RotateX(T radians);
			/** @brief Applies a rotation around the Y axis to the matrix in place */
			Matrix4x4& RotateY(T radians);
			/** @brief Applies a rotation around the Z axis to the matrix in place */
			Matrix4x4& RotateZ(T radians);
			/** @brief Applies a non-uniform scaling to the matrix in place */
			Matrix4x4& Scale(T xx, T yy, T zz);
			/** @overload */
			Matrix4x4& Scale(const Vector3<T>& v);
			/** @brief Applies a uniform scaling to the matrix in place */
			Matrix4x4& Scale(T s);

			/** @brief Creates a translation matrix */
			static Matrix4x4 Translation(T xx, T yy, T zz);
			/** @overload */
			static Matrix4x4 Translation(const Vector3<T>& v);
			// SH4 (Dreamcast) codegen note, which the rotation helpers below depend on: the negation of a
			// sine *value* is lost wherever the result is consumed by arithmetic in the same optimized
			// region - it survives neither a temporary, a volatile, nor a memory barrier - leaving the
			// matrix symmetric, i.e. a shear along a screen diagonal instead of a rotation. It is exact at
			// 0 degrees and worst at 45, so it only appears once something actually rotates. Two things
			// avoid it together: taking the opposite-signed sine from the negated ANGLE (sin(-x)), and
			// keeping these builders real calls so their result cannot be folded into the caller.
#if defined(DEATH_TARGET_DREAMCAST)
#	define DEATH_MATRIX_ROTATION_BUILDER DEATH_NEVER_INLINE
#else
#	define DEATH_MATRIX_ROTATION_BUILDER
#endif
			/** @brief Creates a rotation matrix around the X axis */
			DEATH_MATRIX_ROTATION_BUILDER static Matrix4x4 RotationX(T radians);
			/** @brief Creates a rotation matrix around the Y axis */
			DEATH_MATRIX_ROTATION_BUILDER static Matrix4x4 RotationY(T radians);
			/** @brief Creates a rotation matrix around the Z axis */
			DEATH_MATRIX_ROTATION_BUILDER static Matrix4x4 RotationZ(T radians);
			/** @brief Creates a non-uniform scaling matrix */
			static Matrix4x4 Scaling(T xx, T yy, T zz);
			/** @overload */
			static Matrix4x4 Scaling(const Vector3<T>& v);
			/** @brief Creates a uniform scaling matrix */
			static Matrix4x4 Scaling(T s);

			/** @brief Creates an orthographic projection matrix */
			static Matrix4x4 Ortho(T left, T right, T bottom, T top, T near, T far);
			/** @brief Creates a perspective projection matrix from frustum boundaries */
			static Matrix4x4 Frustum(T left, T right, T bottom, T top, T near, T far);
			/** @brief Creates a perspective projection matrix from a vertical field of view and aspect ratio */
			static Matrix4x4 Perspective(T fovY, T aspect, T near, T far);

			/** @{ @name Constants */

			/** @brief Matrix with all elements set to zero */
			static const Matrix4x4 Zero;
			/** @brief Identity matrix */
			static const Matrix4x4 Identity;

			/** @} */

		private:
			Vector4<T> vecs_[4];
		};

		/** @brief Four-by-four matrix of floats */
		using Matrix4x4f = Matrix4x4<float>;

		template<class T>
		inline Matrix4x4<T>::Matrix4x4(const Vector4<T>& v0, const Vector4<T>& v1, const Vector4<T>& v2, const Vector4<T>& v3) noexcept
		{
			Set(v0, v1, v2, v3);
		}

		template<class T>
		inline void Matrix4x4<T>::Set(const Vector4<T>& v0, const Vector4<T>& v1, const Vector4<T>& v2, const Vector4<T>& v3)
		{
			vecs_[0] = v0;
			vecs_[1] = v1;
			vecs_[2] = v2;
			vecs_[3] = v3;
		}

		template<class T>
		inline T* Matrix4x4<T>::Data()
		{
			return &vecs_[0][0];
		}

		template<class T>
		inline const T* Matrix4x4<T>::Data() const
		{
			return &vecs_[0][0];
		}

		template<class T>
		inline Vector4<T>& Matrix4x4<T>::operator[](std::size_t index)
		{
			DEATH_ASSERT(index < 4);
			return vecs_[index];
		}

		template<class T>
		inline const Vector4<T>& Matrix4x4<T>::operator[](std::size_t index) const
		{
			DEATH_ASSERT(index < 4);
			return vecs_[index];
		}

		template<class T>
		inline bool Matrix4x4<T>::operator==(const Matrix4x4& m) const
		{
			return (vecs_[0] == m[0] && vecs_[1] == m[1] && vecs_[2] == m[2] && vecs_[3] == m[3]);
		}

		template<class T>
		inline bool Matrix4x4<T>::operator!=(const Matrix4x4& m) const
		{
			return (vecs_[0] != m[0] || vecs_[1] != m[1] || vecs_[2] != m[2] || vecs_[3] != m[3]);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator-() const
		{
			return Matrix4x4(-vecs_[0], -vecs_[1], -vecs_[2], -vecs_[3]);
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator+=(const Matrix4x4& m)
		{
			vecs_[0] += m[0];
			vecs_[1] += m[1];
			vecs_[2] += m[2];
			vecs_[3] += m[3];

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator-=(const Matrix4x4& m)
		{
			vecs_[0] -= m[0];
			vecs_[1] -= m[1];
			vecs_[2] -= m[2];
			vecs_[3] -= m[3];

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator*=(const Matrix4x4& m)
		{
			return (*this = *this * m);
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator/=(const Matrix4x4& m)
		{
			vecs_[0] /= m[0];
			vecs_[1] /= m[1];
			vecs_[2] /= m[2];
			vecs_[3] /= m[3];

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator+=(T s)
		{
			vecs_[0] += s;
			vecs_[1] += s;
			vecs_[2] += s;
			vecs_[3] += s;

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator-=(T s)
		{
			vecs_[0] -= s;
			vecs_[1] -= s;
			vecs_[2] -= s;
			vecs_[3] -= s;

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator*=(T s)
		{
			vecs_[0] *= s;
			vecs_[1] *= s;
			vecs_[2] *= s;
			vecs_[3] *= s;

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::operator/=(T s)
		{
			vecs_[0] /= s;
			vecs_[1] /= s;
			vecs_[2] /= s;
			vecs_[3] /= s;

			return *this;
		}

		template<class T>
		inline Vector4<T> Matrix4x4<T>::operator*(const Vector4<T>& v) const
		{
			const Matrix4x4& m = *this;

			return Vector4<T>(m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[0][3] * v[3],
				m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + m[1][3] * v[3],
				m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[2][3] * v[3],
				m[3][0] * v[0] + m[3][1] * v[1] + m[3][2] * v[2] + m[3][3] * v[3]);
		}

		template<class T>
		inline Vector3<T> Matrix4x4<T>::operator*(const Vector3<T>& v) const
		{
			const Matrix4x4& m = *this;

			return Vector3<T>(m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[3][0],
				m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + m[3][1],
				m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[3][2]);
		}

		template<class S>
		inline Vector4<S> operator*(const Vector4<S>& v, const Matrix4x4<S>& m)
		{
			return Vector4<S>(m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2] + m[3][0] * v[3],
				m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2] + m[3][1] * v[3],
				m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2] + m[3][2] * v[3],
				m[0][3] * v[0] + m[1][3] * v[1] + m[2][3] * v[2] + m[3][3] * v[3]);
		}

		template<class S>
		inline Vector3<S> operator*(const Vector3<S>& v, const Matrix4x4<S>& m)
		{
			return Vector3<S>(m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2] + m[3][0],
				m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2] + m[3][1],
				m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2] + m[3][2]);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator+(const Matrix4x4& m) const
		{
			return Matrix4x4(vecs_[0] + m[0],
				vecs_[1] + m[1],
				vecs_[2] + m[2],
				vecs_[3] + m[3]);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator-(const Matrix4x4& m) const
		{
			return Matrix4x4(vecs_[0] - m[0],
				vecs_[1] - m[1],
				vecs_[2] - m[2],
				vecs_[3] - m[3]);
		}

		template<class T>
		// Not inlined on Dreamcast, and written over the flat storage rather than as four Vector4
		// expressions: both keep the composition of a freshly built rotation from being folded into the
		// caller, where its negated sine term loses its sign (see the note next to RotationZ's declaration).
		// This is also the shape the software and PVR backends use to combine matrices.
#if defined(DEATH_TARGET_DREAMCAST)
		DEATH_NEVER_INLINE
#endif
		inline Matrix4x4<T> Matrix4x4<T>::operator*(const Matrix4x4& m2) const
		{
			Matrix4x4 result;
			const T* DEATH_RESTRICT a = Data();
			const T* DEATH_RESTRICT b = m2.Data();
			T* DEATH_RESTRICT out = result.Data();
			for (std::int32_t col = 0; col < 4; col++) {
				for (std::int32_t row = 0; row < 4; row++) {
					out[col * 4 + row] =
						a[0 * 4 + row] * b[col * 4 + 0] +
						a[1 * 4 + row] * b[col * 4 + 1] +
						a[2 * 4 + row] * b[col * 4 + 2] +
						a[3 * 4 + row] * b[col * 4 + 3];
				}
			}

			return result;
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator/(const Matrix4x4& m) const
		{
			return Matrix4x4(vecs_[0] / m[0],
				vecs_[1] / m[1],
				vecs_[2] / m[2],
				vecs_[3] / m[3]);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator+(T s) const
		{
			return Matrix4x4(vecs_[0] + s,
				vecs_[1] + s,
				vecs_[2] + s,
				vecs_[3] + s);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator-(T s) const
		{
			return Matrix4x4(vecs_[0] - s,
				vecs_[1] - s,
				vecs_[2] - s,
				vecs_[3] - s);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator*(T s) const
		{
			return Matrix4x4(vecs_[0] * s,
				vecs_[1] * s,
				vecs_[2] * s,
				vecs_[3] * s);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::operator/(T s) const
		{
			return Matrix4x4(vecs_[0] / s,
				vecs_[1] / s,
				vecs_[2] / s,
				vecs_[3] / s);
		}

		template<class S>
		inline Matrix4x4<S> operator*(S s, const Matrix4x4<S>& m)
		{
			return Matrix4x4<S>(s * m.vecs_[0],
				s * m.vecs_[1],
				s * m.vecs_[2],
				s * m.vecs_[3]);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Transposed() const
		{
			const Matrix4x4& m = *this;
			Matrix4x4 result;

			result[0][0] = m[0][0];
			result[0][1] = m[1][0];
			result[0][2] = m[2][0];
			result[0][3] = m[3][0];

			result[1][0] = m[0][1];
			result[1][1] = m[1][1];
			result[1][2] = m[2][1];
			result[1][3] = m[3][1];

			result[2][0] = m[0][2];
			result[2][1] = m[1][2];
			result[2][2] = m[2][2];
			result[2][3] = m[3][2];

			result[3][0] = m[0][3];
			result[3][1] = m[1][3];
			result[3][2] = m[2][3];
			result[3][3] = m[3][3];

			return result;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::Transpose()
		{
			Matrix4x4& m = *this;
			T x;

			// clang-format off
			x = m[0][1]; m[0][1] = m[1][0]; m[1][0] = x;
			x = m[0][2]; m[0][2] = m[2][0]; m[2][0] = x;
			x = m[0][3]; m[0][3] = m[3][0]; m[3][0] = x;
			x = m[1][2]; m[1][2] = m[2][1]; m[2][1] = x;
			x = m[1][3]; m[1][3] = m[3][1]; m[3][1] = x;
			x = m[2][3]; m[2][3] = m[3][2]; m[3][2] = x;
			// clang-format on

			return *this;
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Inverse() const
		{
			const Matrix4x4& m = *this;

			const T coef00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
			const T coef02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
			const T coef03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];

			const T coef04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
			const T coef06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
			const T coef07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];

			const T coef08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
			const T coef10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
			const T coef11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];

			const T coef12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
			const T coef14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
			const T coef15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];

			const T coef16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
			const T coef18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
			const T coef19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];

			const T coef20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
			const T coef22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
			const T coef23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

			const Vector4<T> fac0(coef00, coef00, coef02, coef03);
			const Vector4<T> fac1(coef04, coef04, coef06, coef07);
			const Vector4<T> fac2(coef08, coef08, coef10, coef11);
			const Vector4<T> fac3(coef12, coef12, coef14, coef15);
			const Vector4<T> fac4(coef16, coef16, coef18, coef19);
			const Vector4<T> fac5(coef20, coef20, coef22, coef23);

			const Vector4<T> vec0(m[1][0], m[0][0], m[0][0], m[0][0]);
			const Vector4<T> vec1(m[1][1], m[0][1], m[0][1], m[0][1]);
			const Vector4<T> vec2(m[1][2], m[0][2], m[0][2], m[0][2]);
			const Vector4<T> vec3(m[1][3], m[0][3], m[0][3], m[0][3]);

			const Vector4<T> inv0(vec1 * fac0 - vec2 * fac1 + vec3 * fac2);
			const Vector4<T> inv1(vec0 * fac0 - vec2 * fac3 + vec3 * fac4);
			const Vector4<T> inv2(vec0 * fac1 - vec1 * fac3 + vec3 * fac5);
			const Vector4<T> inv3(vec0 * fac2 - vec1 * fac4 + vec2 * fac5);

			const Vector4<T> signA(+1, -1, +1, -1);
			const Vector4<T> signB(-1, +1, -1, +1);
			const Matrix4x4 inverse(inv0 * signA, inv1 * signB, inv2 * signA, inv3 * signB);

			const Vector4<T> row0(inverse[0][0], inverse[1][0], inverse[2][0], inverse[3][0]);

			const Vector4<T> dot0(m[0] * row0);
			const T dot1 = (dot0.X + dot0.Y) + (dot0.Z + dot0.W);

			const T oneOverDeterminant = 1 / dot1;

			return inverse * oneOverDeterminant;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::Translate(T xx, T yy, T zz)
		{
			Matrix4x4& m = *this;

			m[3][0] += xx * m[0][0] + yy * m[1][0] + zz * m[2][0];
			m[3][1] += xx * m[0][1] + yy * m[1][1] + zz * m[2][1];
			m[3][2] += xx * m[0][2] + yy * m[1][2] + zz * m[2][2];

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::Translate(const Vector3<T>& v)
		{
			return Translate(v.X, v.Y, v.Z);
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::RotateX(T radians)
		{
			// Composed from RotationX() instead of updating the columns in place - see the note next to the
			// builder declarations. The barrier forces the freshly built rotation out to memory so the
			// composition below cannot re-derive (and drop the sign of) its negated sine term.
			Matrix4x4 r = RotationX(radians);
#if defined(DEATH_TARGET_DREAMCAST)
			asm volatile("" : "+m"(r));
#endif
			return (*this = *this * r);
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::RotateY(T radians)
		{
			// Composed from RotationY() instead of updating the columns in place - see the note next to the
			// builder declarations. The barrier forces the freshly built rotation out to memory so the
			// composition below cannot re-derive (and drop the sign of) its negated sine term.
			Matrix4x4 r = RotationY(radians);
#if defined(DEATH_TARGET_DREAMCAST)
			asm volatile("" : "+m"(r));
#endif
			return (*this = *this * r);
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::RotateZ(T radians)
		{
			// Composed from RotationZ() instead of updating the columns in place - see the note next to the
			// builder declarations. The barrier forces the freshly built rotation out to memory so the
			// composition below cannot re-derive (and drop the sign of) its negated sine term.
			Matrix4x4 r = RotationZ(radians);
#if defined(DEATH_TARGET_DREAMCAST)
			asm volatile("" : "+m"(r));
#endif
			return (*this = *this * r);
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::Scale(T xx, T yy, T zz)
		{
			Matrix4x4& m = *this;

			m[0][0] *= xx;
			m[0][1] *= xx;
			m[0][2] *= xx;

			m[1][0] *= yy;
			m[1][1] *= yy;
			m[1][2] *= yy;

			m[2][0] *= zz;
			m[2][1] *= zz;
			m[2][2] *= zz;

			return *this;
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::Scale(const Vector3<T>& v)
		{
			return Scale(v.X, v.Y, v.Z);
		}

		template<class T>
		inline Matrix4x4<T>& Matrix4x4<T>::Scale(T s)
		{
			return Scale(s, s, s);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Translation(T xx, T yy, T zz)
		{
			return Matrix4x4(Vector4<T>(1, 0, 0, 0),
				Vector4<T>(0, 1, 0, 0),
				Vector4<T>(0, 0, 1, 0),
				Vector4<T>(xx, yy, zz, 1));
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Translation(const Vector3<T>& v)
		{
			return Translation(v.X, v.Y, v.Z);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::RotationX(T radians)
		{
			const T c = cos(radians);
			const T s = sin(radians);
			const T ns = sin(-radians);	// Never "-s", see the note next to the declarations

			return Matrix4x4(Vector4<T>(1, 0, 0, 0),
				Vector4<T>(0, c, s, 0),
				Vector4<T>(0, ns, c, 0),
				Vector4<T>(0, 0, 0, 1));
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::RotationY(T radians)
		{
			const T c = cos(radians);
			const T s = sin(radians);
			const T ns = sin(-radians);	// Never "-s", see the note next to the declarations

			return Matrix4x4(Vector4<T>(c, 0, ns, 0),
				Vector4<T>(0, 1, 0, 0),
				Vector4<T>(s, 0, c, 0),
				Vector4<T>(0, 0, 0, 1));
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::RotationZ(T radians)
		{
			const T c = cos(radians);
			const T s = sin(radians);
			const T ns = sin(-radians);	// Never "-s", see the note next to the declarations

			return Matrix4x4(Vector4<T>(c, s, 0, 0),
				Vector4<T>(ns, c, 0, 0),
				Vector4<T>(0, 0, 1, 0),
				Vector4<T>(0, 0, 0, 1));
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Scaling(T xx, T yy, T zz)
		{
			return Matrix4x4(Vector4<T>(xx, 0, 0, 0),
				Vector4<T>(0, yy, 0, 0),
				Vector4<T>(0, 0, zz, 0),
				Vector4<T>(0, 0, 0, 1));
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Scaling(const Vector3<T>& v)
		{
			return Scaling(v.X, v.Y, v.Z);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Scaling(T s)
		{
			return Scaling(s, s, s);
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Ortho(T left, T right, T bottom, T top, T near, T far)
		{
			float invRL = 1.0f / (right - left);
			float invTB = 1.0f / (top - bottom);
			float invFN = 1.0f / (far - near);

			return Matrix4x4(Vector4<T>(2 * invRL, 0, 0, 0),
				Vector4<T>(0, 2 * invTB, 0, 0),
				Vector4<T>(0, 0, -2 * invFN, 0),
				Vector4<T>(-(right + left) * invRL, -(top + bottom) * invTB, -(far + near) * invFN, 1));
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Frustum(T left, T right, T bottom, T top, T near, T far)
		{
			return Matrix4x4(Vector4<T>((2 * near) / (right - left), 0, 0, 0),
				Vector4<T>(0, (2 * near) / (top - bottom), 0, 0),
				Vector4<T>((right + left) / (right - left), (top + bottom) / (top - bottom), -(far + near) / (far - near), -1),
				Vector4<T>(0, 0, (-2 * far * near) / (far - near), 0));
		}

		template<class T>
		inline Matrix4x4<T> Matrix4x4<T>::Perspective(T fovY, T aspect, T near, T far)
		{
			const T yMax = near * tan(fovY * static_cast<T>(Pi) / 360);
			const T yMin = -yMax;
			const T xMin = yMin * aspect;
			const T xMax = yMax * aspect;

			return Frustum(xMin, xMax, yMin, yMax, near, far);
		}

		template<class T>
		const Matrix4x4<T> Matrix4x4<T>::Zero(Vector4<T>(0, 0, 0, 0), Vector4<T>(0, 0, 0, 0), Vector4<T>(0, 0, 0, 0), Vector4<T>(0, 0, 0, 0));
		template<class T>
		const Matrix4x4<T> Matrix4x4<T>::Identity(Vector4<T>(1, 0, 0, 0), Vector4<T>(0, 1, 0, 0), Vector4<T>(0, 0, 1, 0), Vector4<T>(0, 0, 0, 1));
	}
}
