#include "apfGeometry.h"
#include "apfMatrix.h"
#include <cmath>

namespace apf {

Line::Line(Vector3 const& o, Vector3 const& d):
  origin(o),
  direction(d.normalize())
{
}

Plane::Plane(Vector3 const& n, double r):
  normal(n.normalize()),
  radius(r * n.getLength())
{
}

Plane Plane::fromPoints(Vector3 const& a,
      Vector3 const& b, Vector3 const& c)
{
  Vector3 normal = apf::cross(a - c, b - c).normalize();
  double radius = c * normal;
  return Plane(normal, radius);
}

double Plane::distance(Vector3 const& a) const
{
  return normal * a - radius;
}

bool areClose(double a, double b, double tol)
{
  /* there are more advanced and accurate floating
     point comparison algorithms available.
     consider that if this code gets used a lot
     more than it is now.
     "<=" so that using zero tolerance is an exact
     comparison */
  return std::fabs(b - a) <= tol;
}

bool areClose(Vector3 const& a, Vector3 const& b,
    double tol)
{
  return areClose((b - a).getLength(), 0, tol);
}

bool areClose(Plane const& a, Plane const& b, double tol)
{
  return areClose(a.normal * a.radius, b.normal * b.radius, tol);
}

bool areClose(Vector3 const& a, Plane const& b, double tol)
{
  return areClose(b.normal * a, b.radius, tol);
}

bool areParallel(Vector3 const& a, Vector3 const& b, double tol)
{
  return areClose(std::fabs(a * b) / (a.getLength() * a.getLength()), 1, tol);
}

bool areOrthogonal(Vector3 const& a, Vector3 const& b, double tol)
{
  return areClose(a * b / (a.getLength() * a.getLength()), 0, tol);
}

bool areParallel(Plane const& a, Plane const& b, double tol)
{
  return areParallel(a.normal, b.normal, tol);
}

bool areParallel(Line const& a, Plane const& b, double tol)
{
  return areOrthogonal(a.direction, b.normal, tol);
}

bool areClose(Line const& a, Plane const& b, double tol)
{
  return areParallel(a, b, tol) && areClose(a.origin, b, tol);
}

Line intersect(Plane const& a, Plane const& b)
{
  /* we have to compute the line
       x = (origin) + (lambda)(direction)
     as an origin,direction pair by combining the first two planes.
     The direction is straightforward: */
  Vector3 direction = cross(a.normal, b.normal);
  /* the origin can be computed by noting that we want
     to satisfy these two equations:
       normal_0 . x = radius_0
       normal_1 . x = radius_1
     and add this arbitrary but stable equation:
       direction . x = 0
     This gives us an AX=B problem. The normals are required
     to be linearly independent unit vectors,
     so A should be invertible */
  Matrix3x3 A;
  A[0] = a.normal;
  A[1] = b.normal;
  A[2] = direction;
  Vector3 B(a.radius, b.radius, 0);
  Vector3 origin = invert(A) * B;
  return Line(origin, direction);
}

Vector3 intersect(Line const& a, Plane const& b)
{
  /* we need to satisfy two equations:
       x = (origin) + (lambda)(direction)     line equation
       (normal).(x) = radius                  plane equation
     this gives us:
       (normal).((origin) + (lambda)(direction)) = radius
       (normal).(origin) + (lambda)((normal).(direction)) = radius
       lambda = (radius - (normal).(origin)) / ((normal).(direction))
     this will only fail if normal.direction=0, which is
     a line parallel to a plane. */
  double denominator = b.normal * a.direction;
  double numerator = b.radius - (b.normal * a.origin);
  double lambda = numerator / denominator;
  return a.origin + (a.direction * lambda);
}

}