#pragma once

namespace donut::math
{
#define AFFINE_MEMBERS(T, n) \
    affine() { } \
    vector<T, n> transformPoint(const vector<T, n>& v) const \
    { return v * m_linear + m_translation; } \
    vector<T, n> transformVector(const vector<T, n>& v) const \
    { return v * m_linear; }

    // Generic affine transform struct, with a matrix and translation vector

    template <typename T, int n>
    struct affine
    {
        cassert(n > 1);

        matrix<T, n, n>	m_linear;
        vector<T, n>	m_translation;

        template<typename U>
        affine(const affine<U, n>& a)
            : m_linear(a.m_linear)
            , m_translation(a.m_translation) { }

        static affine<T, n> identity()
        {
            affine<T, n> result = { matrix<T, n, n>::identity(), vector<T, n>(static_cast<T>(0)) };
            return result;
        }

        AFFINE_MEMBERS(T, n)
    };

    template<typename T>
    struct affine<T, 2>
    {
        matrix<T, 2, 2>	m_linear;
        vector<T, 2>	m_translation;

        template<typename U>
        constexpr affine(const affine<U, 2>& a)
            : m_linear(a.m_linear)
            , m_translation(a.m_translation) { }

        constexpr affine(T m00, T m01, T m10, T m11, T t0, T t1)
            : m_linear(m00, m01, m10, m11)
            , m_translation(t0, t1) { }

        constexpr affine(const vector<T, 2>& row0, const vector<T, 2>& row1, const vector<T, 2>& translation)
            : m_linear(row0, row1)
            , m_translation(translation) { }

        constexpr affine(const matrix<T, 2, 2>& linear, const vector<T, 2>& translation)
            : m_linear(linear)
            , m_translation(translation) { }

        static constexpr affine from_cols(const vector<T, 2>& col0, const vector<T, 2>& col1, const vector<T, 2>& translation)
        {
            return affine(matrix<T, 2, 2>::from_cols(col0, col1), translation);
        }

        static constexpr affine identity()
        {
            return affine(matrix<T, 2, 2>::identity(), vector<T, 2>::zero());
        }

        AFFINE_MEMBERS(T, 2)
    };

    template<typename T>
    struct affine<T, 3>
    {
        matrix<T, 3, 3>	m_linear;
        vector<T, 3>	m_translation;

        template<typename U>
        constexpr affine(const affine<U, 3>& a)
            : m_linear(a.m_linear)
            , m_translation(a.m_translation) { }

        constexpr affine(T m00, T m01, T m02, T m10, T m11, T m12, T m20, T m21, T m22, T t0, T t1, T t2)
            : m_linear(m00, m01, m02, m10, m11, m12, m20, m21, m22)
            , m_translation(t0, t1, t2) { }

        constexpr affine(const vector<T, 3>& row0, const vector<T, 3>& row1, const vector<T, 3>& row2, const vector<T, 3>& translation)
            : m_linear(row0, row1, row2)
            , m_translation(translation) { }

        constexpr affine(const matrix<T, 3, 3>& linear, const vector<T, 3>& translation)
            : m_linear(linear)
            , m_translation(translation) { }

        static constexpr affine from_cols(const vector<T, 3>& col0, const vector<T, 3>& col1, const vector<T, 3>& col2, const vector<T, 3>& translation)
        {
            return affine(matrix<T, 3, 3>::from_cols(col0, col1, col2), translation);
        }

        static constexpr affine identity()
        {
            return affine(matrix<T, 3, 3>::identity(), vector<T, 3>::zero());
        }

        AFFINE_MEMBERS(T, 3)
    };
#undef AFFINE_MEMBERS


	// Concrete affines, and their maker functions,
	// for the most common types and dimensions
#define DEFINE_CONCRETE_AFFINES(type, name) \
			typedef affine<type, 2> name##2; \
			typedef affine<type, 3> name##3; \
			typedef affine<type, 2> const & name##2_arg; \
			typedef affine<type, 3> const & name##3_arg; \
			[[deprecated]] inline name##2 make##name##2(type m0, type m1, type m2, type m3, type tx, type ty) \
				{ return name##2(m0, m1, m2, m3, tx, ty); } \
			[[deprecated]] inline name##2 make##name##2(type##2_arg row0, type##2_arg row1, type##2_arg translation) \
				{ return name##2(row0, row1, translation); } \
			[[deprecated]] inline name##2 make##name##2Cols(type##2_arg col0, type##2_arg col1, type##2_arg translation) \
				{ return name##2::from_cols(col0, col1, translation); } \
			[[deprecated]] inline name##2 make##name##2(type##2x2_arg linear, type##2_arg translation) \
				{ return name##2(linear, translation); } \
			[[deprecated]] inline name##3 make##name##3(type m0, type m1, type m2, type m3, type m4, type m5, type m6, type m7, type m8, type tx, type ty, type tz) \
				{ return name##3(m0, m1, m2, m3, m4, m5, m6, m7, m8, tx, ty, tz); } \
			[[deprecated]] inline name##3 make##name##3(type##3_arg row0, type##3_arg row1, type##3_arg row2, type##3_arg translation) \
				{ return name##3(row0, row1, row2, translation); } \
			[[deprecated]] inline name##3 make##name##3Cols(type##3_arg col0, type##3_arg col1, type##3_arg col2, type##3_arg translation) \
				{ return name##3::from_cols(col0, col1, col2, translation); } \
			[[deprecated]] inline name##3 make##name##3(type##3x3_arg linear, type##3_arg translation) \
				{ return name##3(linear, translation); } \
			[[deprecated]] inline name##3 make##name##3(type##4x4_arg m) \
				{ return name##3(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]); }

	DEFINE_CONCRETE_AFFINES(float, affine);
	DEFINE_CONCRETE_AFFINES(int, iaffine);

#undef DEFINE_CONCRETE_AFFINES

	// Overloaded math operators

	// Relational operators
	// !!! these don't match the behavior of relational ops for vectors and matrices -
	// return single results rather than componentwise results

	template <typename T, int n>
	bool operator == (affine<T, n> const & a, affine<T, n> const & b)
	{
		return all(a.m_linear == b.m_linear) && all(a.m_translation == b.m_translation);
	}

	template <typename T, int n>
	bool operator != (affine<T, n> const & a, affine<T, n> const & b)
	{
		return any(a.m_linear != b.m_linear) || any(a.m_translation != b.m_translation);
	}

	// Affine composition (row-vector math)

	template <typename T, int n>
	affine<T, n> operator * (affine<T, n> const & a, affine<T, n> const & b)
	{
		affine<T, n> result =
		{
			a.m_linear * b.m_linear,
			a.m_translation * b.m_linear + b.m_translation
		};
		return result;
	}

	template <typename T, int n>
	affine<T, n> & operator *= (affine<T, n> & a, affine<T, n> const & b)
	{
		a = a*b;
		return a;
	}
    

	// Other math functions

	template <typename T, int n>
	affine<T, n> transpose(affine<T, n> const & a)
	{
		auto mTransposed = transpose(a.m_linear);
		affine<T, n> result =
		{
			mTransposed,
			-a.m_translation * mTransposed
		};
		return result;
	}

	template <typename T, int n>
	affine<T, n> pow(affine<T, n> const & a, int b)
	{
		if (b <= 0)
			return affine<T, n>::identity();
		if (b == 1)
			return a;
		auto oddpart = affine<T, n>::identity(), evenpart = a;
		while (b > 1)
		{
			if (b % 2 == 1)
				oddpart *= evenpart;

			evenpart *= evenpart;
			b /= 2;
		}
		return oddpart * evenpart;
	}

	template <typename T, int n>
	affine<T, n> inverse(affine<T, n> const & a)
	{
		auto mInverted = inverse(a.m_linear);
		affine<T, n> result =
		{
			mInverted,
			-a.m_translation * mInverted
		};
		return result;
	}

	template <typename T, int n>
	matrix<T, n+1, n+1> affineToHomogeneous(affine<T, n> const & a)
	{
		matrix<T, n+1, n+1> result;
		for (int i = 0; i < n; ++i)
		{
			for (int j = 0; j < n; ++j)
				result[i][j] = a.m_linear[i][j];
			result[i][n] = T(0);
		}
		for (int j = 0; j < n; ++j)
			result[n][j] = a.m_translation[j];
		result[n][n] = T(1);
		return result;
	}

	template <typename T, int n>
	affine<T, n-1> homogeneousToAffine(matrix<T, n, n> const & a)
	{
		// Extract the relevant components of the matrix; note, NO checking
		// that the matrix actually represents an affine transform!
		affine<T, n-1> result;
		for (int i = 0; i < n-1; ++i)
			for (int j = 0; j < n-1; ++j)
				result.m_linear[i][j] = T(a[i][j]);
		for (int j = 0; j < n-1; ++j)
			result.m_translation[j] = T(a[n-1][j]);
		return result;
	}

	// !!! this doesn't match the behavior of isnear() for vectors and matrices -
	// returns a single result rather than a componentwise result
	template <typename T, int n>
	bool isnear(affine<T, n> const & a, affine<T, n> const & b, float epsilon = dm::epsilon)
	{
		return all(isnear(a.m_linear, b.m_linear, epsilon)) &&
			   all(isnear(a.m_translation, b.m_translation, epsilon));
	}

	// !!! this doesn't match the behavior of isfinite() for vectors and matrices -
	// returns a single result rather than a componentwise result
	template <typename T, int n>
	bool isfinite(affine<T, n> const & a)
	{
		return all(isfinite(a.m_linear)) && all(isfinite(a.m_translation));
	}

	template <typename T, int n>
	affine<int, n> round(affine<T, n> const & a)
	{
		return makeaffine(round(a.m_linear), round(a.m_translation));
	}



	// Generate various types of transformations (row-vector math)

	template <typename T, int n>
	affine<T, n> translation(vector<T, n> const & a)
	{
		affine<T, n> result = { matrix<T, n, n>::identity(), a };
		return result;
	}

	template <typename T, int n>
	affine<T, n> scaling(T a)
	{
		affine<T, n> result = { diagonal<T, n>(a), vector<T, n>(static_cast<T>(0)) };
		return result;
	}

	template <typename T, int n>
	affine<T, n> scaling(vector<T, n> const & a)
	{
		affine<T, n> result = { diagonal(a), vector<T, n>(static_cast<T>(0)) };
		return result;
	}

	affine2 rotation(float radians);
	affine3 rotation(float3_arg axis, float radians);
	affine3 rotation(float3_arg euler);

    affine3 yawPitchRoll(radians_f yaw, radians_f pitch, radians_f roll);

	affine2 lookat(float2_arg look);

	// lookatX: rotate so X axis faces 'look' and Z axis faces 'up', if specified.
	// lookatZ: rotate so -Z axis faces 'look' and Y axis faces 'up', if specified.

	affine3 lookatX(float3_arg look);
	affine3 lookatX(float3_arg look, float3_arg up);
	affine3 lookatZ(float3_arg look);
	affine3 lookatZ(float3_arg look, float3_arg up);
}
