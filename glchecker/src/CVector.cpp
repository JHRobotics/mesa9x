/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#include "CEngine.h"

const double CPI = acos(-1.0);
const double CTAU = acos(-1.0) * 2.0;

namespace CVector2D
{
	const CVector CNullvector(0.0f, 0.0f);
	/* Collision detection */
	bool CLine_Line(CLine line, CLine line2, CVector* intersect)
	{
		/*line.dir.x * u = line2.pos.x + line2.dir.x * v - line.pos.x
		u = (line2.pos.y + line2.dir.y * v - line.pos.y) / line.dir.y
		line.dir.x *  (line2.pos.y + line2.dir.y * v - line.pos.y) / line.dir.y = line2.pos.x + line2.dir.x * v - line.pos.x
		line.dir.x * line2.pos.y + line.dir.x * line2.dir.y * v - line.dir.x * line.pos.y  = line2.pos.x * line.dir.y + line2.dir.x * v * line.dir.y - line.pos.x * line.dir.y
		line.dir.x * line2.pos.y - line2.pos.x * line.dir.- line.dir.x * line.pos.y + line.pos.x * line.dir.y = line2.dir.x * v * line.dir.y - line.dir.x * line2.dir.y * v
		line.dir.x * line2.pos.y - line.dir.x * line.pos.y + line.pos.x * line.dir.y - line2.pos.x * line.dir.y = (line2.dir.x * line.dir.y - line.dir.x * line2.dir.y) * v
		(line.dir.x * (line2.pos.y - line.pos.y) + line.dir.y * (line.pos.x - line2.pos.x)) / (line2.dir.x * line.dir.y - line.dir.x * line2.dir.y) = v*/
		float denom = line2.dir.x * line.dir.y - line.dir.x * line2.dir.y, v, u;
		if (denom == 0.0f)
			return false;
		v = (line.dir.x * (line2.pos.y - line.pos.y) + line.dir.y * (line.pos.x - line2.pos.x)) / denom;
		if (v < 0.0f || v > 1.0f)
			return false;
		if (line.dir.y != 0.0f)
			u = (line2.pos.y + line2.dir.y * v - line.pos.y) / line.dir.y;
		else
			u = (line2.pos.x + line2.dir.x * v - line.pos.x) / line.dir.x;
		if (u < 0.0f || u > 1.0f)
			return false;
		if (intersect != NULL)
		{
			intersect->x = line2.pos.x + line2.dir.x * v;
			intersect->y = line2.pos.y + line2.dir.y * v;
		}
		return true;
	}

	bool CCircle_Circle(CCircle circle, CCircle circle2, CVector* response)
	{
		CVector tmp = circle2.pos - circle.pos;
		float mdist = circle.radius + circle2.radius, m = tmp.magnitude(), t = m - mdist;
		if (t >= 0.0f) // Touch or do not intersect
			return false;
		if (response != NULL)
		{
			if (m > 0.0f)
				(*response) = tmp * (t / mdist);
			else
				response->x = mdist;
		}
		return true;
	}

	bool CTriangle_Triangle(CTriangle triangle, CTriangle triangle2, CVector* response)
	{
		return true;
	}

	bool CRectangle_Rectangle(CRectangle rectangle, CRectangle rectangle2, CVector* response)
	{
		return true;
	}

	bool CLine_Circle(CLine line, CCircle circle, CVector* response)
	{
		CVector end = line.pos + line.dir, vect = circle.pos - line.pos, ref(0.0f, 0.0f);
		float v = vect.dot(line.dir) / line.dir.sqdist(ref);
		if (v <= 0.0f)
			vect = circle.pos - line.pos;
		else if (v >= 1.0f)
			vect = circle.pos - end;
		else
			vect = circle.pos - line.pos - line.dir * v;
		v = circle.radius - vect.magnitude();
		if (v <= 0.0f) // Touches or not intersecting
			return false;
		if (response != NULL)
		{
			if (v != circle.radius)
			{
				vect.normalize();
				(*response) = vect * v;
			}
			else
				response->x = circle.radius;
		}
		return true;
	}

	bool CLine_Rectangle(CLine line, CRectangle rectangle, CVector* intersect)
	{
		/* If line is inside
		float a, b, c, c2;
		a = -rectangle.l[1].dir.y;
		b = rectangle.l[1].dir.x;
		c = -a * rectangle.l[1].pos.x - b * rectangle.l[1].pos.y;
		c2 = a * rectangle.l[1].pos.x + b * rectangle.l[1].pos.y;
		if (a * line.pos.x + b * line.pos.y + c > 0 && -a * line.pos.x - b * line.pos.y + c2 > 0)
		{
			a = -rectangle.l[3].dir.y;
			b = rectangle.l[3].dir.x;
			c = -a * rectangle.l[3].pos.x - b * rectangle.l[3].pos.y;
			c2 = a * rectangle.l[3].pos.x + b * rectangle.l[3].pos.y;
			if (a * line.pos.x + b * line.pos.y + c > 0 && -a * line.pos.x - b * line.pos.y + c2 > 0)
				return true;
		}*/
		// Intersect sides
		unsigned char i;
		CLoops(i, 0, 4)
		{
			if (CLine_Line(rectangle.l[i], line, intersect))
				return true;
		}
		return false;
	}

	bool CLine_Triangle(CLine line, CTriangle triangle, CVector* response)
	{
		return true;
	}

	bool CCircle_Rectangle(CCircle circle, CRectangle rectangle, CVector* response)
	{
		/*CVector a = circle.pos - rectangle.v1, b = rectangle.v2 - rectangle.v1, c = rectangle.v4 - rectangle.v1;
		float dotb = a.dot(b), dotc = a.dot(c);
		if (dotb >= 0 && dotb <= b.dot(b) && dotc >= 0 && dotc <= c.dot(c)) // Inside rectangle
			return true;*/
		// Intersect lines
		bool inter = false;
		unsigned char i;
		if (response == NULL)
		{
			CLoops(i, 0, 4)
			{
				if (CLine_Circle(rectangle.l[i], circle, NULL))
				{
					inter = true;
					break;
				}
			}
		}
		else
		{
			CVector mov;
			(*response) = CNullvector;
			CLoops(i, 0, 4)
			{
				if (CLine_Circle(rectangle.l[i], circle, &mov))
				{
					inter = true;
					(*response) += mov;
				}
			}
		}
		return inter;
	}

	bool CCircle_Triangle(CCircle circle, CTriangle triangle, CVector* response)
	{
		return true;
	}
}

namespace CVector3D
{
	const CVector CNullvector(0.0f, 0.0f, 0.0f);
	/* Collision detection */
	inline CVector CSphere_Triangle(CTriangle& triangle, CSphere& sphere)
	{
		float c = triangle.normal.dot(sphere.pos) - triangle.pconst;
		if (fabs(c) < sphere.radius)
		{
			if (c > 0)
				c -= sphere.radius;
			else
				c += sphere.radius;
			// Inside triangle?
			CVector t = sphere.pos - triangle.normal * (sphere.radius - c),
				ab = triangle.b - triangle.a,
				ac = triangle.c - triangle.a,
				at = t - triangle.a;
			float d1 = ab.dot(ab), d2 = ab.dot(ac), d3 = ab.dot(at), d4 = ac.dot(ac), d5 = ac.dot(at), inv = 1 / (d1 * d4 - d2 * d2);
			float u = (d4 * d3 - d2 * d5) * inv, v = (d1 * d5 - d2 * d3) * inv;
			if (u >= 0 && v >= 0 && u + v < 1)
				return triangle.normal * c;
			else
			{
				// Colliding with edges?
				t = (sphere.pos - triangle.a);
				v = t.dot(ab) / ab.dot(ab);
				at = triangle.a + ab * v;
				if ((u = at.distance(sphere.pos) - sphere.radius) < 0 && v >= 0 && v <= 1)
				{
					t = sphere.pos - at;
					t.normalize();
					t *= u;
					return t;
				}
				else
				{
					t = (sphere.pos - triangle.a);
					v = t.dot(ac) / ac.dot(ac);
					at = triangle.a + ac * v;
					if ((u = at.distance(sphere.pos) - sphere.radius) < 0 && v >= 0 && v <= 1)
					{
						t = sphere.pos - at;
						t.normalize();
						t *= u;
						return t;
					}
					else
					{
						ab = triangle.c - triangle.b;
						t = (sphere.pos - triangle.b);
						v = t.dot(ab) / ab.dot(ab);
						at = triangle.b + ab * v;
						if ((u = at.distance(sphere.pos) - sphere.radius) < 0 && v >= 0 && v <= 1)
						{
							t = sphere.pos - at;
							t.normalize();
							t *= u;
							return t;
						}
						else
						{
							// Colliding with points?
							if ((u = sphere.pos.distance(triangle.a) - sphere.radius) < 0)
							{
								t = sphere.pos - triangle.a;
								t.normalize();
								t *= u;
								return t;
							}
							else if ((u =sphere.pos.distance(triangle.b) - sphere.radius) < 0)
							{
								t = sphere.pos - triangle.b;
								t.normalize();
								t *= u;
								return t;
							}
							else if ((u =sphere.pos.distance(triangle.c) - sphere.radius) < 0)
							{
								t = sphere.pos - triangle.c;
								t.normalize();
								t *= u;
								return t;
							}
						}
					}
				}
			}
		}
		return CNullvector;
	}
}
