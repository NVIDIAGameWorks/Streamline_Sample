#include <donut/core/math/math.h>
#include <assert.h>

namespace donut::math
{
	// Color space conversions

	hsv RGBtoHSV(rgb_arg c)
	{
		float minComp = minComponent(c);
		float maxComp = maxComponent(c);
		float delta = maxComp - minComp;

		if (maxComp == 0.0f)
			return makehsv(0.0f);

		hsv result = { 0, delta / maxComp, maxComp };

		if (delta == 0.0f)
			return result;

		if (c.x == maxComp)
			result.x = (c.y - c.z) / delta;
		else if (c.y == maxComp)
			result.x = 2.0f + (c.z - c.x) / delta;
		else
			result.x = 4.0f + (c.x - c.y) / delta;

		if (result.x < 0.0f)
			result.x += 6.0f;

		result.x *= 60.0f;
		return result;
	}

	rgb HSVtoRGB(hsv_arg c)
	{
		if (c.y == 0.0f)
			return makergb(c.z);

		float h = modPositive(c.x, 360.0f) / 60.0f;
		int i = int(floor(h));
		assert(i >= 0 && i < 6);
		float f = h - i;
		float p = c.z * (1.0f - c.y);
		float q = c.z * (1.0f - c.y * f);
		float t = c.z * (1.0f - c.y * (1.0f - f));

		switch (i)
		{
		case 0: return makergb(c.z, t, p);
		case 1: return makergb(q, c.z, p);
		case 2: return makergb(p, c.z, t);
		case 3: return makergb(p, q, c.z);
		case 4: return makergb(t, p, c.z);
		case 5: return makergb(c.z, p, q);
		default:
			return makergb(0.0f);
		}
	}

	// White point for CIELAB conversion (in XYZ color space),
	// chosen to make RGB (1, 1, 1) come out to CIELAB (100, 0, 0).
	static const float3 xyzWhitePoint = { 0.9505f, 1.0f, 1.0887f };

	cielab RGBtoCIELAB(rgb_arg c)
	{
		// Convert RGB to XYZ color space
		static const float3x3 RGBtoXYZ =
		{
			0.4124f, 0.2126f, 0.0193f,
			0.3576f, 0.7152f, 0.1192f,
			0.1805f, 0.0722f, 0.9502f,
		};
		float3 xyz = c * RGBtoXYZ;

		// Convert to CIELAB space
		xyz /= xyzWhitePoint;
		float3 warp = select(
						xyz > 0.00885645f,
						pow(xyz, 1.0f/3.0f),
						7.787037f * xyz + 0.137931f);
		return makecielab(
					116.0f * warp.y - 16.0f,
					500.0f * (warp.x - warp.y),
					200.0f * (warp.y - warp.z));
	}

	rgb CIELABtoRGB(cielab_arg c)
	{
		// Convert CIELAB to XYZ
		float warpY = (c.x + 16.0f) / 116.0f;
		float warpX = warpY + c.y / 500.0f;
		float warpZ = warpY - c.z / 200.0f;
		float3 warp = { warpX, warpY, warpZ };
		float3 xyz = xyzWhitePoint * select(
										warp > 0.206897f,
										warp * warp * warp,
										(warp - 0.137931f) / 7.787037f);

		// Convert XYZ to RGB color space
		static const float3x3 XYZtoRGB =
		{
			3.2406f, -0.9689f, 0.0557f,
			-1.5372f, 1.8758f, -0.2040f,
			-0.4986f, 0.0415f, 1.0570f,
		};
		return xyz * XYZtoRGB;
	}
}
