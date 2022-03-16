#pragma once

namespace donut::math
{

#pragma warning(push)
#pragma warning(disable: 4201)	// Nameless struct/union

	struct quat
	{
		union {
			float m_data[4];
			struct { float w, x, y, z; };
		};

		// Conversions to C arrays of fixed size
		typedef float (&array_t)[4];
		operator array_t ()
			{ return m_data; }
		typedef const float (&const_array_t)[4];
		operator const_array_t () const
			{ return m_data; }

		// Subscript operators - built-in subscripts are ambiguous without these
		float & operator [] (int i)
			{ return m_data[i]; }
		const float & operator [] (int i) const
			{ return m_data[i]; }

		// Convert to a matrix
		float3x3 toFloat3x3() const
		{
			return float3x3(
							1 - 2*(y*y + z*z), 2*(x*y + z*w), 2*(x*z - y*w),
							2*(x*y - z*w), 1 - 2*(x*x + z*z), 2*(y*z + x*w),
							2*(x*z + y*w), 2*(y*z - x*w), 1 - 2*(x*x + y*y));
		}

		// Convert to an affine transform
		affine3 toAffine() const
		{
			return affine3(toFloat3x3(), 0.f);
		}

		// Conversion to bool is not allowed (otherwise would
		// happen implicitly through array conversions)
		private: operator bool();
	};

#pragma warning(pop)

	typedef const quat & quat_arg;



	// Makers

	inline quat makequatIdentity()
	{
		quat result = { 1, 0, 0, 0 };
		return result;
	}

	inline quat makequat(float a)
	{
		quat result = { a, a, a, a };
		return result;
	}

	inline quat makequat(const float * a)
	{
		quat result = { a[0], a[1], a[2], a[3] };
		return result;
	}

	inline quat makequat(float w, float x, float y, float z)
	{
		quat result = { w, x, y, z };
		return result;
	}

	template <typename T>
	inline quat makequat(float w, vector<T, 3> const & xyz)
	{
		quat result = { w, xyz.x, xyz.y, xyz.z };
		return result;
	}



	// Overloaded math operators

#define DEFINE_UNARY_OPERATOR(op) \
			inline quat operator op (quat_arg a) \
			{ \
				quat result; \
				for (int i = 0; i < 4; ++i) \
					result[i] = op a[i]; \
				return result; \
			}

#define DEFINE_BINARY_SCALAR_OPERATORS(op) \
			/* Scalar-quat op */ \
			inline quat operator op (float a, quat_arg b) \
			{ \
				quat result; \
				for (int i = 0; i < 4; ++i) \
					result.m_data[i] = a op b.m_data[i]; \
				return result; \
			} \
			/* Quat-scalar op */ \
			inline quat operator op (quat_arg a, float b) \
			{ \
				quat result; \
				for (int i = 0; i < 4; ++i) \
					result.m_data[i] = a.m_data[i] op b; \
				return result; \
			}

#define DEFINE_BINARY_OPERATORS(op) \
			/* Quat-quat op */ \
			inline quat operator op (quat_arg a, quat_arg b) \
			{ \
				quat result; \
				for (int i = 0; i < 4; ++i) \
					result.m_data[i] = a.m_data[i] op b.m_data[i]; \
				return result; \
			} \
			DEFINE_BINARY_SCALAR_OPERATORS(op)

#define DEFINE_INPLACE_SCALAR_OPERATOR(op) \
			/* Quat-scalar op */ \
			inline quat & operator op (quat & a, float b) \
			{ \
				for (int i = 0; i < 4; ++i) \
					a.m_data[i] op b; \
				return a; \
			}

#define DEFINE_INPLACE_OPERATORS(op) \
			/* Quat-quat op */ \
			inline quat & operator op (quat & a, quat_arg b) \
			{ \
				for (int i = 0; i < 4; ++i) \
					a.m_data[i] op b.m_data[i]; \
				return a; \
			} \
			DEFINE_INPLACE_SCALAR_OPERATOR(op)

#define DEFINE_RELATIONAL_OPERATORS(op) \
			/* Quat-quat op */ \
			inline bool4 operator op (quat_arg a, quat_arg b) \
			{ \
				bool4 result; \
				for (int i = 0; i < 4; ++i) \
					result[i] = a[i] op b[i]; \
				return result; \
			} \
			/* Scalar-vector op */ \
			inline bool4 operator op (float a, quat_arg b) \
			{ \
				bool4 result; \
				for (int i = 0; i < 4; ++i) \
					result[i] = a op b[i]; \
				return result; \
			} \
			/* Vector-scalar op */ \
			inline bool4 operator op (quat_arg a, float b) \
			{ \
				bool4 result; \
				for (int i = 0; i < 4; ++i) \
					result[i] = a[i] op b; \
				return result; \
			}

	DEFINE_BINARY_OPERATORS(+);
	DEFINE_BINARY_OPERATORS(-);
	DEFINE_UNARY_OPERATOR(-);
	DEFINE_BINARY_SCALAR_OPERATORS(*);
	DEFINE_BINARY_SCALAR_OPERATORS(/);

	DEFINE_INPLACE_OPERATORS(+=);
	DEFINE_INPLACE_OPERATORS(-=);
	DEFINE_INPLACE_SCALAR_OPERATOR(*=);
	DEFINE_INPLACE_SCALAR_OPERATOR(/=);

	DEFINE_RELATIONAL_OPERATORS(==);
	DEFINE_RELATIONAL_OPERATORS(!=);
	DEFINE_RELATIONAL_OPERATORS(<);
	DEFINE_RELATIONAL_OPERATORS(>);
	DEFINE_RELATIONAL_OPERATORS(<=);
	DEFINE_RELATIONAL_OPERATORS(>=);

#undef DEFINE_UNARY_OPERATOR
#undef DEFINE_BINARY_OPERATORS
#undef DEFINE_INPLACE_OPERATORS
#undef DEFINE_RELATIONAL_OPERATORS

	// Quaternion multiplication

	inline quat operator * (quat_arg a, quat_arg b)
	{
		return makequat(
				a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
				a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
				a.w*b.y + a.y*b.w + a.z*b.x - a.x*b.z,
				a.w*b.z + a.z*b.w + a.x*b.y - a.y*b.x);
	}

	inline quat & operator *= (quat & a, quat_arg b)
	{
		a = a*b;
		return a;
	}



	// Other math functions

	inline float dot(quat_arg a, quat_arg b)
		{ return a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z; }

	inline float lengthSquared(quat_arg a)
		{ return dot(a, a); }

	inline float length(quat_arg a)
		{ return sqrt(lengthSquared(a)); }

	inline quat normalize(quat_arg a)
		{ return a / length(a); }

	inline quat conjugate(quat_arg a)
		{ return makequat(a.w, -a.x, -a.y, -a.z); }

	inline quat pow(quat_arg a, int b)
	{
		if (b <= 0)
			return makequatIdentity();
		if (b == 1)
			return a;
		quat oddpart = makequatIdentity(), evenpart = a;
		while (b > 1)
		{
			if (b % 2 == 1)
				oddpart *= evenpart;

			evenpart *= evenpart;
			b /= 2;
		}
		return oddpart * evenpart;
	}

	inline quat inverse(quat_arg a)
		{ return conjugate(a) / lengthSquared(a); }

	// Apply a normalized quat as a rotation to a vector or point

	template <typename T>
	vector<T, 3> applyQuat(quat_arg a, vector<T, 3> const & b)
	{
		quat v = { 0, b.x, b.y, b.z };
		quat resultQ = a * v * conjugate(a);
		vector<T, 3> result = { resultQ.x, resultQ.y, resultQ.z };
		return result;
	}

	inline bool4 isnear(quat_arg a, quat_arg b, float eps = dm::epsilon)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isnear(a[i], b[i], eps);
		return result;
	}

	inline bool4 isnear(quat_arg a, float b, float eps = dm::epsilon)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isnear(a[i], b, eps);
		return result;
	}

	inline bool4 isnear(float a, quat_arg b, float eps = dm::epsilon)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isnear(a, b[i], eps);
		return result;
	}

	inline bool4 isfinite(quat_arg a)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isfinite(a[i]);
		return result;
	}

	inline quat select(bool4_arg cond, quat_arg a, quat_arg b)
	{
		quat result;
		for (int i = 0; i < 4; ++i)
			result[i] = cond[i] ? a[i] : b[i];
		return result;
	}

	inline quat min(quat_arg a, quat_arg b)
		{ return select(a < b, a, b); }

	inline quat max(quat_arg a, quat_arg b)
		{ return select(a < b, b, a); }

	inline quat abs(quat_arg a)
		{ return select(a < float(0), -a, a); }

	inline quat saturate(quat_arg value)
		{ return clamp(value, makequat(0.0f), makequat(1.0f)); }

	inline float minComponent(quat_arg a)
	{
		float result = a[0];
		for (int i = 1; i < 4; ++i)
			result = min(result, a[i]);
		return result;
	}

	inline float maxComponent(quat_arg a)
	{
		float result = a[0];
		for (int i = 1; i < 4; ++i)
			result = max(result, a[i]);
		return result;
	}

	quat rotationQuat(float3_arg axis, float radians);
	quat rotationQuat(float3_arg euler);
	quat slerp(quat_arg a, quat_arg b, float u);
}
