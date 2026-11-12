// coop_math.cpp -- coop:: section 1: the math adapter between the engine's
// double-precision UE5 types and the plain structs the wire code uses.
//
// Everything here is a verbatim port of the Lua in
// mod\CoopMod\Scripts\main.lua (READ-ONLY reference):
//   170        dist(a, b)                       -> Dist
//   391-395    normDeg(a)                       -> NormDeg
//   870-876    rotQuat(r)                       -> RotQuat
//   877-882    qmul(a, b)                       -> QuatMul
//   883-889    qrot(q, v)                       -> QuatRot
//   900-905    compose(p, c)                    -> Compose
//
// Two conventions are load-bearing and are NOT the usual textbook ones:
//
//  * FRotator::Quaternion is applied with d = pi/360 (half-angles), and the signs
//    are Unreal's, so `RotQuat` is not the generic XYZ euler-to-quat formula.
//  * `a * b` applies b first: Compose(parent, child) = parent * child, which is what
//    makes a component's world transform parent-relative.
//
// NormDeg must wrap with LUA's `%` (floor-mod, always non-negative) and then fold
// into (-180, 180].  std::fmod / std::remainder keep the sign of the input, so
// either one silently turns -190 into -190 instead of 170 -- and the puppet yaw
// easing then takes the long way round on every turn across the seam.
#include "ms2_ue.h"
#include "coop_state.h"

#include <cmath>

namespace coop {

// ===========================================================================
// layout conversion (UE5: FVector/FRotator/FQuat/FTransform are all double)
// ===========================================================================

Vec3 ToVec3(const ue::FVector& v) {
    Vec3 out;
    out.X = v.X;
    out.Y = v.Y;
    out.Z = v.Z;
    return out;
}

ue::FVector FromVec3(const Vec3& v) {
    ue::FVector out;
    out.X = v.X;
    out.Y = v.Y;
    out.Z = v.Z;
    return out;
}

Rot ToRot(const ue::FRotator& r) {
    Rot out;
    out.Pitch = r.Pitch;
    out.Yaw = r.Yaw;
    out.Roll = r.Roll;
    return out;
}

ue::FRotator FromRot(const Rot& r) {
    ue::FRotator out;
    out.Pitch = r.Pitch;
    out.Yaw = r.Yaw;
    out.Roll = r.Roll;
    return out;
}

Quat ToQuat(const ue::FQuat& q) {
    Quat out;
    out.X = q.X;
    out.Y = q.Y;
    out.Z = q.Z;
    out.W = q.W;
    return out;
}

ue::FQuat FromQuat(const Quat& q) {
    ue::FQuat out;
    out.X = q.X;
    out.Y = q.Y;
    out.Z = q.Z;
    out.W = q.W;
    return out;
}

Xform ToXform(const ue::FTransform& t) {
    Xform out;
    out.T = ToVec3(t.Translation);
    out.R = ToQuat(t.Rotation);
    out.S.X = t.Scale3D.X;
    out.S.Y = t.Scale3D.Y;
    out.S.Z = t.Scale3D.Z;
    return out;
}

ue::FTransform FromXform(const Xform& t) {
    ue::FTransform out;
    out.Rotation = FromQuat(t.R);
    out.Translation = FromVec3(t.T);
    out.Scale3D = FromVec3(Vec3{t.S.X, t.S.Y, t.S.Z});
    return out;
}

// ===========================================================================
// main.lua:170  dist(a, b)
// ===========================================================================
double Dist(const Vec3& a, const Vec3& b) {
    const double dx = a.X - b.X;
    const double dy = a.Y - b.Y;
    const double dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ===========================================================================
// main.lua:391-395
//
//   local function normDeg(a)
//       a = a % 360
//       if a > 180 then a = a - 360 end
//       return a
//   end
//
// Lua's % on floats is a - floor(a/b)*b, so the result is in [0, 360) even for a
// negative input.  Then the fold gives (-180, 180].
// ===========================================================================
double NormDeg(double a) {
    if (!(a == a)) return 0.0;                       // NaN in -> 0, never NaN out
    if (a > 1.0e15 || a < -1.0e15) return 0.0;       // do not let floor() overflow
    double r = a - std::floor(a / 360.0) * 360.0;     // Lua `%`: always [0, 360)
    if (r < 0.0) r += 360.0;                         // guard the fp edge at multiples
    if (r >= 360.0) r -= 360.0;
    if (r > 180.0) r -= 360.0;
    return r;
}

// ===========================================================================
// main.lua:870-876 -- FRotator::Quaternion (degrees, d = pi/360)
// ===========================================================================
Quat RotQuat(const Rot& r) {
    const double d = 3.14159265358979323846 / 360.0;
    const double sp = std::sin(r.Pitch * d), cp = std::cos(r.Pitch * d);
    const double sy = std::sin(r.Yaw * d), cy = std::cos(r.Yaw * d);
    const double sr = std::sin(r.Roll * d), cr = std::cos(r.Roll * d);

    Quat q;
    q.X = cr * sp * sy - sr * cp * cy;
    q.Y = -cr * sp * cy - sr * cp * sy;
    q.Z = cr * cp * sy - sr * sp * cy;
    q.W = cr * cp * cy + sr * sp * sy;
    return q;
}

// ===========================================================================
// main.lua:877-882 -- a * b applies b first
// ===========================================================================
Quat QuatMul(const Quat& a, const Quat& b) {
    Quat q;
    q.X = a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y;
    q.Y = a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X;
    q.Z = a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W;
    q.W = a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z;
    return q;
}

// ===========================================================================
// main.lua:883-889 -- rotate v by q (the two-cross-product form)
// ===========================================================================
Vec3 QuatRot(const Quat& q, const Vec3& v) {
    const double tx = 2.0 * (q.Y * v.Z - q.Z * v.Y);
    const double ty = 2.0 * (q.Z * v.X - q.X * v.Z);
    const double tz = 2.0 * (q.X * v.Y - q.Y * v.X);
    Vec3 out;
    out.X = v.X + q.W * tx + (q.Y * tz - q.Z * ty);
    out.Y = v.Y + q.W * ty + (q.Z * tx - q.X * tz);
    out.Z = v.Z + q.W * tz + (q.X * ty - q.Y * tx);
    return out;
}

// ===========================================================================
// main.lua:900-905
//
//   local sl = { x = p.s.x * c.l.x, ... }      -- the child's offset, scaled
//   local rl = qrot(p.q, sl)                    -- rotated into the parent
//   l = p.l + rl, q = qmul(p.q, c.q), s = p.s * c.s
// ===========================================================================
Xform Compose(const Xform& parent, const Xform& child) {
    const Vec3 scaled{parent.S.X * child.T.X, parent.S.Y * child.T.Y, parent.S.Z * child.T.Z};
    const Vec3 rotated = QuatRot(parent.R, scaled);

    Xform out;
    out.T.X = parent.T.X + rotated.X;
    out.T.Y = parent.T.Y + rotated.Y;
    out.T.Z = parent.T.Z + rotated.Z;
    out.R = QuatMul(parent.R, child.R);
    out.S.X = parent.S.X * child.S.X;
    out.S.Y = parent.S.Y * child.S.Y;
    out.S.Z = parent.S.Z * child.S.Z;
    return out;
}

}  // namespace coop
