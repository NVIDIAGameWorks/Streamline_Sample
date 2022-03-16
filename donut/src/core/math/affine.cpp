#include <donut/core/math/math.h>

namespace donut::math
{
	// Affine transformation implementations

	affine2 rotation(float radians)
	{
		float sinTheta = sinf(radians);
		float cosTheta = cosf(radians);
		return affine2(
					cosTheta, sinTheta,
					-sinTheta, cosTheta,
					0, 0);
	}

	affine3 rotation(float3_arg axis, float radians)
	{
		// Note: assumes axis is normalized
		float sinTheta = sinf(radians);
		float cosTheta = cosf(radians);

		// Build matrix that does cross product by axis (on the right)
		float3x3 crossProductMat = float3x3(
										0, axis.z, -axis.y,
										-axis.z, 0, axis.x,
										axis.y, -axis.x, 0);

		// Matrix form of Rodrigues' rotation formula
		float3x3 mat = diagonal<float, 3>(cosTheta) +
						crossProductMat * sinTheta +
						outerProduct(axis, axis) * (1.0f - cosTheta);

		return affine3(mat, 0.0f);
	}

    affine3 rotation(float3_arg euler)
	{
		float sinX = sinf(euler.x);
		float cosX = cosf(euler.x);
		float sinY = sinf(euler.y);
		float cosY = cosf(euler.y);
		float sinZ = sinf(euler.z);
		float cosZ = cosf(euler.z);

		float3x3 matX = float3x3(
							1,  0,    0,
							0,  cosX, sinX,
							0, -sinX, cosX);
		float3x3 matY = float3x3(
							cosY, 0, -sinY,
							0,    1,  0,
							sinY, 0,  cosY);
		float3x3 matZ = float3x3(
							 cosZ, sinZ, 0,
							-sinZ, cosZ, 0,
							 0,    0,    1);

		return affine3(matX * matY * matZ, 0.0f);
    }

    affine3 yawPitchRoll(radians_f yaw, radians_f pitch, radians_f roll)
    {
        // adapted from glm

        float tmp_sh = sinf(yaw);
        float tmp_ch = cosf(yaw);
        float tmp_sp = sinf(pitch);
        float tmp_cp = cosf(pitch);
        float tmp_sb = sinf(roll);
        float tmp_cb = cosf(roll);


        affine3 result;
        result.m_linear[0][0] = tmp_ch * tmp_cb + tmp_sh * tmp_sp * tmp_sb;
        result.m_linear[0][1] = tmp_sb * tmp_cp;
        result.m_linear[0][2] = -tmp_sh * tmp_cb + tmp_ch * tmp_sp * tmp_sb;
        result.m_linear[0][3] = 0.f;
        result.m_linear[1][0] = -tmp_ch * tmp_sb + tmp_sh * tmp_sp * tmp_cb;
        result.m_linear[1][1] = tmp_cb * tmp_cp;
        result.m_linear[1][2] = tmp_sb * tmp_sh + tmp_ch * tmp_sp * tmp_cb;
        result.m_linear[1][3] = 0.f;
        result.m_linear[2][0] = tmp_sh * tmp_cp;
        result.m_linear[2][1] = -tmp_sp;
        result.m_linear[2][2] = tmp_ch * tmp_cp;
        result.m_linear[2][3] = 0.f;
        result.m_translation = 0.f;
        return result;
	}

	affine2 lookat(float2_arg look)
	{
		float2 lookNormalized = normalize(look);
		return affine2(lookNormalized, orthogonal(lookNormalized), 0.0f);
	}

	affine3 lookatX(float3_arg look)
	{
		float3 lookNormalized = normalize(look);
		float3 left = orthogonal(lookNormalized);
		float3 up = cross(lookNormalized, left);
		return affine3(lookNormalized, left, up, 0.0f);
	}

	affine3 lookatX(float3_arg look, float3_arg up)
	{
		float3 lookNormalized = normalize(look);
		float3 left = normalize(cross(up, lookNormalized));
		float3 trueUp = cross(lookNormalized, left);
		return affine3(lookNormalized, left, trueUp, 0.0f);
	}

	affine3 lookatZ(float3_arg look)
	{
		float3 lookNormalized = normalize(look);
		float3 left = orthogonal(lookNormalized);
		float3 up = cross(lookNormalized, left);
		return affine3(-left, up, -lookNormalized, 0.0f);
	}

	affine3 lookatZ(float3_arg look, float3_arg up)
	{
		float3 lookNormalized = normalize(look);
		float3 left = normalize(cross(up, lookNormalized));
		float3 trueUp = cross(lookNormalized, left);
		return affine3(-left, trueUp, -lookNormalized, 0.0f);
	}
}
