#include <donut/core/math/math.h>
#include <assert.h>

namespace donut::math
{
	// Convert memory layouts to and from SIMD-friendly (AOSOA) layout

	void convertToSIMD(
			int numComponents,
			int numVectors,
			const void * pInput,
			int inputStrideBytes,
			void * pOutput,
			int outputStrideBytes)
	{
		static const int simdWidth = 4;

		assert(numComponents > 0);
        assert(pInput);
        assert(inputStrideBytes >= sizeof(float) * numComponents);
        assert(pOutput);
        assert((size_t)pOutput % (simdWidth * sizeof(float)) == 0);
        assert(outputStrideBytes >= simdWidth * sizeof(float) * numComponents);

		// Do the part that's a multiple of simdWidth
		for (; numVectors >= simdWidth; numVectors -= simdWidth)
		{
			for (int i = 0; i < simdWidth; ++i)
			{
				for (int j = 0; j < numComponents; ++j)
					((float *)pOutput)[simdWidth*j + i] = ((float *)pInput)[j];

				pInput = advanceBytes(pInput, inputStrideBytes);
			}

			pOutput = advanceBytes(pOutput, outputStrideBytes);
		}

		// Do any part left over
		for (int i = 0; i < numVectors; ++i)
		{
			for (int j = 0; j < numComponents; ++j)
				((float *)pOutput)[simdWidth*j + i] = ((float *)pInput)[j];

			pInput = advanceBytes(pInput, inputStrideBytes);
		}
	}

	void convertFromSIMD(
			int numComponents,
			int numVectors,
			const void * pInput,
			int inputStrideBytes,
			void * pOutput,
			int outputStrideBytes)
	{
		static const int simdWidth = 4;

        assert(numComponents > 0);
        assert(pInput);
        assert((size_t)pInput % (simdWidth * sizeof(float)) == 0);
        assert(inputStrideBytes >= simdWidth * sizeof(float) * numComponents);
        assert(pOutput);
        assert(outputStrideBytes >= sizeof(float) * numComponents);

		// Do the part that's a multiple of simdWidth
		for (; numVectors >= simdWidth; numVectors -= simdWidth)
		{
			for (int i = 0; i < simdWidth; ++i)
			{
				for (int j = 0; j < numComponents; ++j)
					((float *)pOutput)[j] = ((float *)pInput)[simdWidth*j + i];

				pOutput = advanceBytes(pOutput, outputStrideBytes);
			}

			pInput = advanceBytes(pInput, inputStrideBytes);
		}

		// Do any part left over
		for (int i = 0; i < numVectors; ++i)
		{
			for (int j = 0; j < numComponents; ++j)
				((float *)pOutput)[j] = ((float *)pInput)[simdWidth*j + i];

			pOutput = advanceBytes(pOutput, outputStrideBytes);
		}
	}
}
