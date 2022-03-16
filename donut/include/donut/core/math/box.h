#pragma once

namespace donut::math
{
	// Generic axis-aligned bounding box (AABB) struct, in mins/maxs form
	// Note: min > max (on any axis) is an empty (null) box.  All empty boxes are the same.
	// min == max is a box containing only one point along that axis.

    template <typename T, int n>
    struct box
    {
        cassert(n > 1);
        static constexpr int numCorners = 1 << n;

        vector<T, n> m_mins, m_maxs;

        constexpr bool isempty() const
        {
            return any(m_mins > m_maxs);
        }

        constexpr bool contains(vector<T, n> const & a) const
        {
            return all(m_mins <= a) && all(a <= m_maxs);
        }

        constexpr bool contains(box<T, n> const & a) const
        {
            return a.isempty() || (all(m_mins <= a.m_mins) && all(a.m_maxs <= m_maxs));
        }

        constexpr bool intersects(box<T, n> const & a) const
        {
            return all(a.m_mins <= m_maxs) && all(m_mins <= a.m_maxs);
        }

        bool isfinite() const
        {
            return all(isfinite(m_mins)) && all(isfinite(m_maxs));
        }

        constexpr vector<T, n> clamp(vector<T, n> const & a) const
        {
            return dm::clamp(a, m_mins, m_maxs);
        }

        constexpr vector<T, n> center() const
        {
            return m_mins + (m_maxs - m_mins) / T(2);
        }

        constexpr vector<T, n> diagonal() const
        {
            return m_maxs - m_mins;
        }

        constexpr vector<T, n> getCorner(int iCorner) const
        {
            return select(bitvector<n>(iCorner), m_maxs, m_mins);
        }

        void getCorners(vector<T, n> * cornersOut) const
        {
            for (int i = 0, nc = numCorners; i < nc; ++i)
                cornersOut[i] = getCorner(i);
        }

        void getExtentsAlongAxis(vector<T, n> const & a, T & outMin, T & outMax) const
        {
            T dotCenter = dot(center(), a);
            T dotDiagonal = dot(diagonal(), abs(a));
            outMin = dotCenter - dotDiagonal;
            outMax = dotCenter + dotDiagonal;
        }

        T dotMin(vector<T, n> const & a) const
        {
            T dotMin, dotMax;
            getExtentsAlongAxis(a, dotMin, dotMax);
            return dotMin;
        }

        T dotMax(vector<T, n> const & a) const
        {
            T dotMin, dotMax;
            getExtentsAlongAxis(a, dotMin, dotMax);
            return dotMax;
        }

        box() { }
        constexpr box(const vector<T, n>& mins, const vector<T, n>& maxs)
            : m_mins(mins), m_maxs(maxs) { }

        box(int numPoints, const vector<T, n>* points)
        {
            if (numPoints == 0)
            {
                m_mins = vector<T, n>(std::numeric_limits<T>::max());
                m_maxs = vector<T, n>(std::numeric_limits<T>::lowest());
                return;
            }

            m_mins = points[0];
            m_maxs = points[0];

            for (int i = 1; i < numPoints; ++i)
            {
                m_mins = min(m_mins, points[i]);
                m_maxs = max(m_maxs, points[i]);
            }
        }

        template<typename U>
        constexpr box(const box<U, n>& b)
            : m_mins(b.m_mins), m_maxs(b.m_maxs) { }

        static constexpr box empty()
        {
            return box(
                vector<T, n>(std::numeric_limits<T>::max()),
                vector<T, n>(std::numeric_limits<T>::lowest()));
        }

        constexpr box translate(const vector<T, n>& v) const
        {
            return box(m_mins + v, m_maxs + v);
        }

        constexpr box grow(const vector<T, n>& v) const
        {
            return box(m_mins - v, m_maxs + v);
        }

        constexpr box grow(T v) const
        {
            return box(m_mins - v, m_maxs + v);
        }

        constexpr box round() const
        {
            return box(round(m_mins), round(m_maxs));
        }

        constexpr box operator & (const box& other) const
        {
            return box<T, n>(max(m_mins, other.m_mins), min(m_maxs, other.m_maxs));
        }

        box operator &= (const box& other)
        {
            *this = *this & other;
            return *this;
        }

        constexpr box operator | (const box& other) const
        {
            return box<T, n>(min(m_mins, other.m_mins), max(m_maxs, other.m_maxs));
        }

        box operator |= (const box& other)
        {
            *this = *this | other;
            return *this;
        }

        constexpr box operator | (const vector<T, n>& v) const
        {
            return box<T, n>(min(m_mins, v), max(m_maxs, v));
        }

        box operator |= (const vector<T, n>& v)
        {
            *this = *this | v;
            return *this;
        }

        box operator * (const affine<T, n>& transform) const
        {
            vector<T, n> corner = getCorner(0);
            corner = transform.transformPoint(corner);
            box<T, n> result = { corner, corner };
            for (int i = 1; i < numCorners; ++i)
            {
                corner = getCorner(i);
                corner = transform.transformPoint(corner);
                result.m_mins = min(result.m_mins, corner);
                result.m_maxs = max(result.m_maxs, corner);
            }
            return result;
        }

        box operator *= (const affine<T, n>& transform) const
        {
            *this = *this * transform;
            return *this;
        }

        constexpr bool operator == (box<T, n> const & other) const
        {
            return all(m_mins == other.m_mins) && all(m_maxs == other.m_maxs);
        }

        constexpr bool operator != (box<T, n> const & other) const
        {
            return any(m_mins != other.m_mins) || any(m_maxs != other.m_maxs);
        }
    };

	
	// Concrete boxes, and their maker functions,
	// for the most common types and dimensions

#define DEFINE_CONCRETE_BOXES(type, name, ptype) \
			typedef box<type, 2> name##2; \
			typedef box<type, 3> name##3; \
			typedef box<type, 2> const & name##2_arg; \
			typedef box<type, 3> const & name##3_arg; \
			[[deprecated]] inline name##2 make##name##2(type minx, type miny, type maxx, type maxy) \
				{ return name##2(type##2(minx, miny), type##2(maxx, maxy)); } \
			[[deprecated]] inline name##2 make##name##2Empty() \
				{ return name##2::empty(); } \
			[[deprecated]] inline name##2 make##name##2(ptype##2_arg mins, ptype##2_arg maxs) \
				{ return name##2(mins, maxs); } \
			[[deprecated]] inline name##2 make##name##2(int numPoints, ptype##2 const * points) \
				{ return name##2(numPoints, points); } \
			[[deprecated]] inline name##3 make##name##3(type minx, type miny, type minz, type maxx, type maxy, type maxz) \
				{ return name##3(type##3(minx, miny, minz), type##3(maxx, maxy, maxz)); } \
			[[deprecated]] inline name##3 make##name##3Empty() \
				{ return name##3::empty(); } \
			[[deprecated]] inline name##3 make##name##3(ptype##3_arg mins, ptype##3_arg maxs) \
				{ return name##3(mins, maxs); } \
			[[deprecated]] inline name##3 make##name##3(int numPoints, ptype##3 const * points) \
				{ return name##3(numPoints, points); } \

	DEFINE_CONCRETE_BOXES(float, box, float);
	DEFINE_CONCRETE_BOXES(int, ibox, int);

#undef DEFINE_CONCRETE_BOXES


	// Other math functions

	template <typename T, int n>
    [[deprecated]] box<T, n> boxUnion(box<T, n> const & a, vector<T, n> const & b)
	{
        return a | b;
	}

	template <typename T, int n>
    [[deprecated]] box<T, n> boxUnion(box<T, n> const & a, box<T, n> const & b)
	{
        return a | b;
	}

	template <typename T, int n>
    [[deprecated]] box<T, n> boxIntersection(box<T, n> const & a, box<T, n> const & b)
	{
        return a & b;
	}

	template <typename T, int n>
    [[deprecated]] box<T, n> boxTranslate(box<T, n> const & a, vector<T, n> const & b)
	{
        return a.translate(b);
	}

	template <typename T, int n>
    [[deprecated]] box<T, n> boxGrow(box<T, n> const & a, vector<T, n> const & b)
	{
        return a.grow(b);
	}

	template <typename T, int n>
    [[deprecated]] box<T, n> boxGrow(box<T, n> const & a, T b)
	{
        return a.grow(b);
	}

	template <typename T, int n>
    [[deprecated]] box<T, n> boxTransform(box<T, n> const & a, affine<T, n> const & aff)
	{
        return a * aff;
	}

	template <typename T, int n>
	T distance(box<T, n> const & a, vector<T, n> const & b)
	{
		return distance(a.clamp(b), b);
	}

	template <typename T, int n>
	T distance(vector<T, n> const & a, box<T, n> const & b)
	{
		return distance(a, b.clamp(a));
	}

	template <typename T, int n>
	T distanceSquared(box<T, n> const & a, vector<T, n> const & b)
	{
		return distanceSquared(a.clamp(b), b);
	}

	template <typename T, int n>
	T distanceSquared(vector<T, n> const & a, box<T, n> const & b)
	{
		return distanceSquared(a, b.clamp(a));
	}

	// !!! this doesn't match the behavior of isnear() for vectors and matrices -
	// returns a single result rather than a componentwise result
	template <typename T, int n>
	bool isnear(box<T, n> const & a, box<T, n> const & b, float epsilon = dm::epsilon)
	{
		return all(isnear(a.m_mins, b.m_mins, epsilon)) &&
			   all(isnear(a.m_maxs, b.m_maxs, epsilon));
	}

	// !!! this doesn't match the behavior of isfinite() for vectors and matrices -
	// returns a single result rather than a componentwise result
	template <typename T, int n>
	bool isfinite(box<T, n> const & a)
	{
        return a.isfinite();
	}

	template <typename T, int n>
    [[deprecated]] box<int, n> round(box<T, n> const & a)
	{
        return a.round();
	}
}
