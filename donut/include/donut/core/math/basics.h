#pragma once
#include <float.h>
#include <limits>
#include <cmath>

// Compile-time array size
template <typename T, int N> char(&dim_helper(T(&)[N]))[N];
#define dim(x) (sizeof(dim_helper(x)))
#define dim_field(S, m) dim(((S*)0)->m)
#define sizeof_field(S, m) (sizeof(((S*)0)->m))

// Compile-time assert
#define cassert(x) static_assert(x, #x)

namespace donut::math
{
	// "uint" is a lot shorter than "unsigned int"
	typedef unsigned int uint;

    // descriptive types for angles
    typedef float radians_f;
    typedef float degrees_f;
    typedef double radians_d;
    typedef double degrees_d;

    constexpr radians_f PI_f = 3.141592654f;
    constexpr radians_d PI_d = 3.14159265358979323;

	// Convenient float constants
    constexpr float epsilon = 1e-6f;		// A reasonable general-purpose epsilon
    constexpr float infinity = std::numeric_limits<float>::infinity();
    constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

    // Generic min/max/abs/clamp/saturate
    template <typename T>
    constexpr T min(T a, T b) { return (a < b) ? a : b; }
    template <typename T>
    constexpr T max(T a, T b) { return (a < b) ? b : a; }
    template <typename T>
    constexpr T abs(T a) { return (a < T(0)) ? -a : a; }
	template <typename T>
    constexpr T clamp(T value, T lower, T upper) { return min(max(value, lower), upper); }
	template <typename T>
    constexpr T saturate(T value) { return clamp(value, T(0), T(1)); }

	// Generic lerp
	template <typename T>
    constexpr T lerp(T a, T b, float u) { return a + (b - a) * u; }

	// Generic square
	template <typename T>
    constexpr T square(T a) { return a*a; }

	// Equality test with epsilon
    constexpr bool isnear(float a, float b, float eps = dm::epsilon)
	{ return (abs(b - a) < eps); }

	// Test for finiteness
	inline bool isfinite(float f)
	{
		union { uint i; float f; } u;
		u.f = f;
		return ((u.i & 0x7f800000) != 0x7f800000);
	}

	// Rounding to nearest integer
	inline int round(float f)
		{ return int(floor(f + 0.5f)); }

	// Modulus with always positive remainders (assuming positive divisor)
	inline int modPositive(int dividend, int divisor)
	{
		int result = dividend % divisor;
		if (result < 0)
			result += divisor;
		return result;
	}
	inline float modPositive(float dividend, float divisor)
	{
		float result = fmod(dividend, divisor);
		if (result < 0)
			result += divisor;
		return result;
	}

	// Base-2 exp and log
	inline float exp2f(float x) { return expf(0.693147181f * x); }
	inline float log2f(float x) { return 1.442695041f * logf(x); }
    
	inline bool ispow2(int x) { return (x > 0) && ((x & (x - 1)) == 0); }
	
	// Integer division, with rounding up (assuming positive arguments)
	inline int div_ceil(int dividend, int divisor) { return (dividend + (divisor - 1)) / divisor; }

	// Integer rounding to multiples
	inline int roundDown(int i, int multiple) { return (i / multiple) * multiple; }
	inline int roundUp(int i, int multiple) { return ((i + (multiple - 1)) / multiple) * multiple; }

	// Advance a pointer by a given number of bytes, regardless of pointer's type
	// (note: number of bytes can be negative)
	template <typename T>
	inline T * advanceBytes(T * ptr, int bytes)
		{ return (T *)((char *)ptr + bytes); }

    inline degrees_f degrees(radians_f rad) { return rad * (180.f / PI_f); }
    inline radians_f radians(degrees_f deg) { return deg * (PI_f / 180.f); }
    inline degrees_d degrees(radians_d rad) { return rad * (180.0 / PI_d); }
    inline radians_d radians(degrees_d deg) { return deg * (PI_d / 180.0); }

    template<typename T>
    constexpr T insertBits(T value, int width, int offset)
    {
        return T((value & ((T(1) << width) - 1)) << offset);
    }

    template<typename T>
    constexpr T extractBits(T value, int width, int offset)
    {
        return T((value >> offset) & ((T(1) << width) - 1));
    }
}
