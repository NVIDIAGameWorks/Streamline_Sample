#pragma once

namespace donut::math
{
	// Create typedefs of float vectors to represent common color spaces.
	// These are intentionally *not* type-safe, to reduce friction.
	// Note: alpha is always linear, even in sRGB spaces.

#define DEFINE_COLOR_SPACE(name) \
			typedef float3 name; \
			typedef float3_arg name##_arg; \
			typedef float4 name##a; \
			typedef float4_arg name##a_arg; \
			inline constexpr name make##name(float x, float y, float z) \
				{ return name { x, y, z }; } \
			inline constexpr name make##name(float a) \
				{ return name { a, a, a }; } \
			inline constexpr name make##name(const float * a) \
				{ return name { a[0], a[1], a[2] }; } \
			inline constexpr name##a make##name##a(float x, float y, float z, float w) \
				{ return name##a { x, y, z, w }; } \
			inline constexpr name##a make##name##a(name##_arg xyz, float w) \
				{ return name##a { xyz.x, xyz.y, xyz.z, w }; } \
			inline constexpr name##a make##name##a(float a) \
				{ return name##a { a, a, a, a }; } \
			inline constexpr name##a make##name##a(const float * a) \
				{ return name##a { a[0], a[1], a[2], a[3] }; }

	DEFINE_COLOR_SPACE(rgb);		// Linear RGB space
	DEFINE_COLOR_SPACE(srgb);		// sRGB space
	DEFINE_COLOR_SPACE(hsv);		// HSV based on linear RGB space
	DEFINE_COLOR_SPACE(shsv);		// HSV based on sRGB space
	DEFINE_COLOR_SPACE(ycocg);		// YCoCg based on linear RGB space
	DEFINE_COLOR_SPACE(sycocg);		// YCoCg based on sRGB space
	DEFINE_COLOR_SPACE(cielab);		// CIELAB space

#undef DEFINE_COLOR_SPACE

    namespace colors
    {
        constexpr rgb black = makergb(0.f);
        constexpr rgb white = makergb(1.f);
        constexpr rgb red = makergb(1.f, 0.f, 0.f);
        constexpr rgb green = makergb(0.f, 1.f, 0.f);
        constexpr rgb blue = makergb(0.f, 0.f, 1.f);
    }

	// Rec. 709 luma coefficients for linear RGB space
	static const rgb lumaCoefficients = { 0.2126f, 0.7152f, 0.0722f };
	inline float luminance(rgb_arg c)
		{ return dot(c, lumaCoefficients); }
	inline float luminance(rgba_arg c)
		{ return dot(float3(c), lumaCoefficients); }

	// Composition operator for linear RGB space (premultiplied alpha)
	inline rgba over_premul(rgba_arg a, rgba_arg b)
		{ return makergba(float3(a) + (1.0f-a.w) * float3(b), 1.0f - ((1.0f-a.w) * (1.0f-b.w))); }

	// Composition operator for linear RGB space (non-premultiplied alpha)
	inline rgba over_nonpremul(rgba_arg a, rgba_arg b)
		{ return makergba(lerp(float3(b), float3(a), a.w), 1.0f - (1.0f-a.w) * (1.0f-b.w)); }
	inline rgb over_nonpremul(rgba_arg a, rgb_arg b)
		{ return lerp(b, float3(a), a.w); }



	// SRGB/linear color space conversions
	inline float toLinear(float c)
		{ return (c <= 0.04045f) ? c/12.92f : ::pow((c + 0.055f)/1.055f, 2.4f); }
	inline float toSRGB(float c)
		{ return (c <= 0.0031308f) ? c*12.92f : 1.055f*::pow(c, 1.0f/2.4f) - 0.055f; }
	inline rgb toLinear(srgb_arg c)
		{ return select(c <= 0.04045f, c/12.92f, pow((c + 0.055f)/1.055f, 2.4f)); }
	inline srgb toSRGB(rgb_arg c)
		{ return select(c <= 0.0031308f, c*12.92f, 1.055f*pow(c, 1.0f/2.4f) - 0.055f); }
	inline rgba toLinear(srgba_arg c)
		{ return makergba(toLinear(float3(c)), c.w); }
	inline srgba toSRGB(rgba_arg c)
		{ return makergba(toSRGB(float3(c)), c.w); }

	// RGB/HSV conversions
	hsv RGBtoHSV(rgb_arg c);
	rgb HSVtoRGB(hsv_arg c);
	inline hsva RGBtoHSV(rgba_arg c)
		{ return makehsva(RGBtoHSV(float3(c)), c.w); }
	inline rgba HSVtoRGB(hsva_arg c)
		{ return makergba(HSVtoRGB(float3(c)), c.w); }

	// RGB/YCoCg conversions
	inline ycocg RGBtoYCoCg(rgb_arg c)
		{ return makeycocg(0.25f*(c.x+2.0f*c.y+c.z), c.x-c.z, c.y - 0.5f*(c.x+c.z)); }
	inline rgb YCoCgtoRGB(ycocg_arg c)
		{ return makergb(c.x+0.5f*(c.y-c.z), c.x+0.5f*c.z, c.x-0.5f*(c.y+c.z)); }
	inline ycocga RGBtoYCoCg(rgba_arg c)
		{ return makeycocga(RGBtoYCoCg(float3(c)), c.w); }
	inline rgba YCoCgtoRGB(ycocga_arg c)
		{ return makergba(YCoCgtoRGB(float3(c)), c.w); }

	// RGB/CIELAB conversions
	cielab RGBtoCIELAB(rgb_arg c);
	rgb CIELABtoRGB(cielab_arg c);
	inline cielaba RGBtoCIELAB(rgba_arg c)
		{ return makecielaba(RGBtoCIELAB(float3(c)), c.w); }
	inline rgba CIELABtoRGB(cielaba_arg c)
		{ return makergba(CIELABtoRGB(float3(c)), c.w); }
}
