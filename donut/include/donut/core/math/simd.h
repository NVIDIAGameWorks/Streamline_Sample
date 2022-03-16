#pragma once
#include <emmintrin.h>

namespace donut::math
{
#if !__linux__ // not necessary on GCC, operators on intrinsics are already defined
	// Overloaded operators for __m128, so it can be used in vectors

	inline __m128 operator + (__m128 a, __m128 b)
	{
		return _mm_add_ps(a, b);
	}
	inline __m128 operator + (float a, __m128 b)
	{
		return _mm_add_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator + (__m128 a, float b)
	{
		return _mm_add_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator - (__m128 a, __m128 b)
	{
		return _mm_sub_ps(a, b);
	}
	inline __m128 operator - (float a, __m128 b)
	{
		return _mm_sub_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator - (__m128 a, float b)
	{
		return _mm_sub_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator - (__m128 a)
	{
		return _mm_sub_ps(_mm_setzero_ps(), a);
	}

	inline __m128 operator * (__m128 a, __m128 b)
	{
		return _mm_mul_ps(a, b);
	}
	inline __m128 operator * (float a, __m128 b)
	{
		return _mm_mul_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator * (__m128 a, float b)
	{
		return _mm_mul_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator / (__m128 a, __m128 b)
	{
		return _mm_div_ps(a, b);
	}
	inline __m128 operator / (float a, __m128 b)
	{
		return _mm_div_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator / (__m128 a, float b)
	{
		return _mm_div_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator & (__m128 a, __m128 b)
	{
		return _mm_and_ps(a, b);
	}
	inline __m128 operator & (float a, __m128 b)
	{
		return _mm_and_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator & (__m128 a, float b)
	{
		return _mm_and_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator | (__m128 a, __m128 b)
	{
		return _mm_or_ps(a, b);
	}
	inline __m128 operator | (float a, __m128 b)
	{
		return _mm_or_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator | (__m128 a, float b)
	{
		return _mm_or_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator ^ (__m128 a, __m128 b)
	{
		return _mm_xor_ps(a, b);
	}
	inline __m128 operator ^ (float a, __m128 b)
	{
		return _mm_xor_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator ^ (__m128 a, float b)
	{
		return _mm_xor_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator ! (__m128 a)
	{
		// Note: there's no bitwise-not, so we have to use "andnot" (= ~a & b) with b = true.
		return _mm_andnot_ps(a, _mm_castsi128_ps(_mm_set_epi32(~0, ~0, ~0, ~0)));
	}

	inline __m128 operator ~ (__m128 a)
	{
		// Note: there's no bitwise-not, so we have to use "andnot" (= ~a & b) with b = true.
		return _mm_andnot_ps(a, _mm_castsi128_ps(_mm_set_epi32(~0, ~0, ~0, ~0)));
	}

	inline __m128 & operator += (__m128 & a, __m128 b)
	{
		a = a + b;
		return a;
	}
	inline __m128 & operator += (__m128 & a, float b)
	{
		a = a + b;
		return a;
	}

	inline __m128 & operator -= (__m128 & a, __m128 b)
	{
		a = a - b;
		return a;
	}
	inline __m128 & operator -= (__m128 & a, float b)
	{
		a = a - b;
		return a;
	}

	inline __m128 & operator *= (__m128 & a, __m128 b)
	{
		a = a * b;
		return a;
	}
	inline __m128 & operator *= (__m128 & a, float b)
	{
		a = a * b;
		return a;
	}

	inline __m128 & operator /= (__m128 & a, __m128 b)
	{
		a = a / b;
		return a;
	}
	inline __m128 & operator /= (__m128 & a, float b)
	{
		a = a / b;
		return a;
	}

	inline __m128 & operator &= (__m128 & a, __m128 b)
	{
		a = a & b;
		return a;
	}
	inline __m128 & operator &= (__m128 & a, float b)
	{
		a = a & b;
		return a;
	}

	inline __m128 & operator |= (__m128 & a, __m128 b)
	{
		a = a | b;
		return a;
	}
	inline __m128 & operator |= (__m128 & a, float b)
	{
		a = a | b;
		return a;
	}

	inline __m128 & operator ^= (__m128 & a, __m128 b)
	{
		a = a ^ b;
		return a;
	}
	inline __m128 & operator ^= (__m128 & a, float b)
	{
		a = a ^ b;
		return a;
	}

	inline __m128 operator == (__m128 a, __m128 b)
	{
		return _mm_cmpeq_ps(a, b);
	}
	inline __m128 operator == (float a, __m128 b)
	{
		return _mm_cmpeq_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator == (__m128 a, float b)
	{
		return _mm_cmpeq_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator != (__m128 a, __m128 b)
	{
		return _mm_cmpneq_ps(a, b);
	}
	inline __m128 operator != (float a, __m128 b)
	{
		return _mm_cmpneq_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator != (__m128 a, float b)
	{
		return _mm_cmpneq_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator < (__m128 a, __m128 b)
	{
		return _mm_cmplt_ps(a, b);
	}
	inline __m128 operator < (float a, __m128 b)
	{
		return _mm_cmplt_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator < (__m128 a, float b)
	{
		return _mm_cmplt_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator > (__m128 a, __m128 b)
	{
		return _mm_cmpgt_ps(a, b);
	}
	inline __m128 operator > (float a, __m128 b)
	{
		return _mm_cmpgt_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator > (__m128 a, float b)
	{
		return _mm_cmpgt_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator <= (__m128 a, __m128 b)
	{
		return _mm_cmple_ps(a, b);
	}
	inline __m128 operator <= (float a, __m128 b)
	{
		return _mm_cmple_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator <= (__m128 a, float b)
	{
		return _mm_cmple_ps(a, _mm_set_ps(b, b, b, b));
	}

	inline __m128 operator >= (__m128 a, __m128 b)
	{
		return _mm_cmpge_ps(a, b);
	}
	inline __m128 operator >= (float a, __m128 b)
	{
		return _mm_cmpge_ps(_mm_set_ps(a, a, a, a), b);
	}
	inline __m128 operator >= (__m128 a, float b)
	{
		return _mm_cmpge_ps(a, _mm_set_ps(b, b, b, b));
	}



	// Overloaded operators for __m128i, so it can be used in vectors

	inline __m128i operator + (__m128i a, __m128i b)
	{
		return _mm_add_epi32(a, b);
	}
	inline __m128i operator + (int a, __m128i b)
	{
		return _mm_add_epi32(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator + (__m128i a, int b)
	{
		return _mm_add_epi32(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator - (__m128i a, __m128i b)
	{
		return _mm_sub_epi32(a, b);
	}
	inline __m128i operator - (int a, __m128i b)
	{
		return _mm_sub_epi32(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator - (__m128i a, int b)
	{
		return _mm_sub_epi32(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator - (__m128i a)
	{
		return _mm_sub_epi32(_mm_setzero_si128(), a);
	}

	inline __m128i operator * (__m128i a, __m128i b)
	{
		// Note: there's no 32-bit int multiply in SSE2 (there is in SSE4.1).
		// This sequence of shuffles and 64-bit muls emulates it, as seen here:
		// http://stackoverflow.com/questions/10500766/sse-multiplication-of-4-32-bit-integers
		__m128i tmp1 = _mm_mul_epu32(a,b); // mul 2,0
		__m128i tmp2 = _mm_mul_epu32( _mm_srli_si128(a,4), _mm_srli_si128(b,4)); // mul 3,1
		return _mm_unpacklo_epi32(
					_mm_shuffle_epi32(tmp1, _MM_SHUFFLE(0,0,2,0)),
					_mm_shuffle_epi32(tmp2, _MM_SHUFFLE(0,0,2,0)));
	}
	inline __m128i operator * (int a, __m128i b)
	{
		return _mm_set_epi32(a, a, a, a) * b;
	}
	inline __m128i operator * (__m128i a, int b)
	{
		return a * _mm_set_epi32(b, b, b, b);
	}

	// Note: there's no 32-bit integer divide in SSE2.  Could maybe emulate it, or
	// approximate it by converting to float and dividing.
#if 0
	inline __m128i operator / (__m128i a, __m128i b)
	{
		return ???
	}
	inline __m128i operator / (int a, __m128i b)
	{
		return _mm_set_epi32(a, a, a, a) / b;
	}
	inline __m128i operator / (__m128i a, int b)
	{
		return a / _mm_set_epi32(b, b, b, b);
	}
#endif

	inline __m128i operator & (__m128i a, __m128i b)
	{
		return _mm_and_si128(a, b);
	}
	inline __m128i operator & (int a, __m128i b)
	{
		return _mm_and_si128(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator & (__m128i a, int b)
	{
		return _mm_and_si128(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator | (__m128i a, __m128i b)
	{
		return _mm_or_si128(a, b);
	}
	inline __m128i operator | (int a, __m128i b)
	{
		return _mm_or_si128(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator | (__m128i a, int b)
	{
		return _mm_or_si128(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator ^ (__m128i a, __m128i b)
	{
		return _mm_xor_si128(a, b);
	}
	inline __m128i operator ^ (int a, __m128i b)
	{
		return _mm_xor_si128(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator ^ (__m128i a, int b)
	{
		return _mm_xor_si128(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator ! (__m128i a)
	{
		// Note: there's no bitwise-not, so we have to use "andnot" (= ~a & b) with b = true.
		return _mm_andnot_si128(a, _mm_set_epi32(~0, ~0, ~0, ~0));
	}

	inline __m128i operator ~ (__m128i a)
	{
		// Note: there's no bitwise-not, so we have to use "andnot" (= ~a & b) with b = true.
		return _mm_andnot_si128(a, _mm_set_epi32(~0, ~0, ~0, ~0));
	}

	inline __m128i & operator += (__m128i & a, __m128i b)
	{
		a = a + b;
		return a;
	}
	inline __m128i & operator += (__m128i & a, int b)
	{
		a = a + b;
		return a;
	}

	inline __m128i & operator -= (__m128i & a, __m128i b)
	{
		a = a - b;
		return a;
	}
	inline __m128i & operator -= (__m128i & a, int b)
	{
		a = a - b;
		return a;
	}

	inline __m128i & operator *= (__m128i & a, __m128i b)
	{
		a = a * b;
		return a;
	}
	inline __m128i & operator *= (__m128i & a, int b)
	{
		a = a * b;
		return a;
	}

	// No 32-bit integer divide
#if 0
	inline __m128i & operator /= (__m128i & a, __m128i b)
	{
		a = a / b;
		return a;
	}
	inline __m128i & operator /= (__m128i & a, int b)
	{
		a = a / b;
		return a;
	}
#endif

	inline __m128i & operator &= (__m128i & a, __m128i b)
	{
		a = a & b;
		return a;
	}
	inline __m128i & operator &= (__m128i & a, int b)
	{
		a = a & b;
		return a;
	}

	inline __m128i & operator |= (__m128i & a, __m128i b)
	{
		a = a | b;
		return a;
	}
	inline __m128i & operator |= (__m128i & a, int b)
	{
		a = a | b;
		return a;
	}

	inline __m128i & operator ^= (__m128i & a, __m128i b)
	{
		a = a ^ b;
		return a;
	}
	inline __m128i & operator ^= (__m128i & a, int b)
	{
		a = a ^ b;
		return a;
	}

	inline __m128i operator == (__m128i a, __m128i b)
	{
		return _mm_cmpeq_epi32(a, b);
	}
	inline __m128i operator == (int a, __m128i b)
	{
		return _mm_cmpeq_epi32(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator == (__m128i a, int b)
	{
		return _mm_cmpeq_epi32(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator != (__m128i a, __m128i b)
	{
		// Note: there's no _mm_cmpneq_epi32, and there's no bitwise-not...
		// so we have to use "andnot" (= ~a & b) with b = true.
		return _mm_andnot_si128(_mm_cmpeq_epi32(a, b), _mm_set_epi32(~0, ~0, ~0, ~0));
	}
	inline __m128i operator != (int a, __m128i b)
	{
		return _mm_set_epi32(a, a, a, a) != b;
	}
	inline __m128i operator != (__m128i a, int b)
	{
		return a != _mm_set_epi32(b, b, b, b);
	}

	inline __m128i operator < (__m128i a, __m128i b)
	{
		return _mm_cmplt_epi32(a, b);
	}
	inline __m128i operator < (int a, __m128i b)
	{
		return _mm_cmplt_epi32(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator < (__m128i a, int b)
	{
		return _mm_cmplt_epi32(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator > (__m128i a, __m128i b)
	{
		return _mm_cmpgt_epi32(a, b);
	}
	inline __m128i operator > (int a, __m128i b)
	{
		return _mm_cmpgt_epi32(_mm_set_epi32(a, a, a, a), b);
	}
	inline __m128i operator > (__m128i a, int b)
	{
		return _mm_cmpgt_epi32(a, _mm_set_epi32(b, b, b, b));
	}

	inline __m128i operator <= (__m128i a, __m128i b)
	{
		// Note: there's no _mm_cmple_epi32, and there's no bitwise-not...
		// so we have to use "andnot" (= ~a & b) with b = true.
		return _mm_andnot_si128(_mm_cmpgt_epi32(a, b), _mm_set_epi32(~0, ~0, ~0, ~0));
	}
	inline __m128i operator <= (int a, __m128i b)
	{
		return _mm_set_epi32(a, a, a, a) <= b;
	}
	inline __m128i operator <= (__m128i a, int b)
	{
		return a <= _mm_set_epi32(b, b, b, b);
	}

	inline __m128i operator >= (__m128i a, __m128i b)
	{
		// Note: there's no _mm_cmpge_epi32, and there's no bitwise-not...
		// so we have to use "andnot" (= ~a & b) with b = true.
		return _mm_andnot_si128(_mm_cmplt_epi32(a, b), _mm_set_epi32(~0, ~0, ~0, ~0));
	}
	inline __m128i operator >= (int a, __m128i b)
	{
		return _mm_set_epi32(a, a, a, a) >= b;
	}
	inline __m128i operator >= (__m128i a, int b)
	{
		return a >= _mm_set_epi32(b, b, b, b);
	}
#endif // !__linux__

	
	// Convert memory layouts to and from SIMD-friendly (AOSOA) layout

	// convertToSIMD takes a layout like this:
	//   xyz...xyz...xyz...xyz...
	// and makes it look like this:
	//   xxxxyyyyzzzz...xxxxyyyyzzzz...
	// In other words it converts an array of float3 to an array of float3_simd.
	// Components (xyz) are assumed to be 32 bits each (float or int).
    // Both input and output layouts can have an arbitrary stride, but the
	// components of one vector are assumed to be tightly packed.
	// numComponents is the count of components per vector (e.g. 3 for xyz, 4 for xyzw).
	// numVectors is the count of vectors (each xyz or xyzw).  If not a multiple of 4,
	// the last item in the output array will be only partially filled.

	void convertToSIMD(
			int numComponents,
			int numVectors,
			const void * pInput,
			int inputStrideBytes,
			void * pOutput,
			int outputStrideBytes);

	// convertFromSIMD does the inverse of convertToSIMD.

	void convertFromSIMD(
			int numComponents,
			int numVectors,
			const void * pInput,
			int inputStrideBytes,
			void * pOutput,
			int outputStrideBytes);



	// Concrete SIMD vectors, matrices, points, and affines,
	// for the most common types and dimensions

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

#define DEFINE_CONCRETE_VECTORS(type, name) \
			typedef vector<type, 2> name##2_simd; \
			typedef vector<type, 3> name##3_simd; \
			typedef vector<type, 4> name##4_simd; \
			typedef vector<type, 2> const & name##2_simd__arg; \
			typedef vector<type, 3> const & name##3_simd__arg; \
			typedef vector<type, 4> const & name##4_simd__arg;

	DEFINE_CONCRETE_VECTORS(__m128, float);
	DEFINE_CONCRETE_VECTORS(__m128i, int);

#undef DEFINE_CONCRETE_VECTORS

#define DEFINE_CONCRETE_MATRICES(type, name) \
			typedef matrix<type, 2, 2> name##2x2_simd; \
			typedef matrix<type, 3, 3> name##3x3_simd; \
			typedef matrix<type, 4, 4> name##4x4_simd; \
			typedef matrix<type, 2, 2> const & name##2x2_simd__arg; \
			typedef matrix<type, 3, 3> const & name##3x3_simd__arg; \
			typedef matrix<type, 4, 4> const & name##4x4_simd__arg;

	DEFINE_CONCRETE_MATRICES(__m128, float);
	DEFINE_CONCRETE_MATRICES(__m128i, int);

#undef DEFINE_CONCRETE_MATRICES

#define DEFINE_CONCRETE_AFFINES(type, name) \
			typedef affine<type, 2> name##2_simd; \
			typedef affine<type, 3> name##3_simd; \
			typedef affine<type, 2> const & name##2_simd__arg; \
			typedef affine<type, 3> const & name##3_simd__arg;

	DEFINE_CONCRETE_AFFINES(__m128, affine);
	DEFINE_CONCRETE_AFFINES(__m128i, iaffine);

#undef DEFINE_CONCRETE_AFFINES

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
