/*
Copyright 2008-2010 Travis Desell, Dave Przybylo, Nathan Cole, Matthew
Arsenault, Boleslaw Szymanski, Heidi Newberg, Carlos Varela, Malik
Magdon-Ismail and Rensselaer Polytechnic Institute.

This file is part of Milkway@Home.

Milkyway@Home is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Milkyway@Home is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Milkyway@Home.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "coordinates.h"

typedef struct
{
    real ra;
    real dec;
} RADec;

/* Convert sun-centered lbr (degrees) into galactic xyz coordinates. */
mwvector lbr2xyz(const ASTRONOMY_PARAMETERS* ap, const mwvector lbr)
{
    real zp, d;
/* TODO: Use radians to begin with */

    real lsin, lcos;
    real bsin, bcos;

    mwvector xyz;

    mw_sincos(d2r(B(lbr)), &bsin, &bcos);
    mw_sincos(d2r(L(lbr)), &lsin, &lcos);

    Z(xyz) = R(lbr) * bsin;
    zp = R(lbr) * bcos;
    d = mw_sqrt(sqr(ap->sun_r0) + sqr(zp) - 2.0 * ap->sun_r0 * zp * lcos);
    X(xyz) = (sqr(zp) - sqr(ap->sun_r0) - sqr(d)) / (2.0 * ap->sun_r0);
    Y(xyz) = zp * lsin;
    W(xyz) = 0.0;
    return xyz;
}

/* Convert galactic xyz into sun-centered lbr coordinates. */
static mwvector xyz2lbr(const ASTRONOMY_PARAMETERS* ap, const mwvector xyz)
{
    mwvector lbr;
    real tmp, xsun;

    xsun = X(xyz) + ap->sun_r0;
    tmp = sqr(xsun) + sqr(Y(xyz));

    L(lbr) = mw_atan2(Y(xyz), xsun);
    B(lbr) = mw_atan2(Z(xyz), mw_sqrt(tmp));
    R(lbr) = mw_sqrt(tmp + sqr(Z(xyz)));

    L(lbr) = r2d(L(lbr));
    B(lbr) = r2d(B(lbr));

    if (L(lbr) < 0.0)
        L(lbr) += 360.0;

    return lbr;
}

static inline real calc_g(mwvector lbg)
{
    return 5.0 * (mw_log(100.0 * R(lbg)) / mw_log(10.0)) + absm;
}

/* CHECKME: Isn't this supposed to be log10? */
static mwvector xyz2lbg(const ASTRONOMY_PARAMETERS* ap, mwvector point, real offset)
{
    mwvector lbg = xyz2lbr(ap, point);
    real g = calc_g(lbg) - offset;

    R(lbg) = g;

    return lbg;
}

/* wrapper that converts a point into magnitude-space pseudo-xyz */
mwvector xyz_mag(const ASTRONOMY_PARAMETERS* ap, mwvector point, real offset)
{
    mwvector logPoint, lbg;

    lbg = xyz2lbg(ap, point, offset);
    logPoint = lbr2xyz(ap, lbg);

    return logPoint;
}

static LB cartesianToSpherical(mwvector v)
{
    LB lb;
    real r = mw_hypot(X(v), Y(v));

    lb.l = (r != 0.0)    ? mw_atan2(Y(v), X(v)) : 0.0;
    lb.b = (Z(v) != 0.0) ? mw_atan2(Z(v), r)    : 0.0;

    return lb;
}

static mwvector raDecToCartesian(RADec raDec)
{
    real cosb;
    mwvector v;

    cosb = mw_cos(raDec.dec);
    X(v) = mw_cos(raDec.ra) * cosb;
    Y(v) = mw_sin(raDec.ra) * cosb;
    Z(v) = mw_sin(raDec.dec);

    return v;
}

static const mwmatrix rmat =
{
    mw_vec( -0.054875539726, -0.873437108010, -0.483834985808 ),
    mw_vec(  0.494109453312, -0.444829589425,  0.746982251810 ),
    mw_vec( -0.867666135858, -0.198076386122,  0.455983795705 )
};

/* convert galactic coordinates l,b (radians) into cartesian x,y,z */
static inline mwvector lbToXyz(LB lb)
{
    mwvector xyz;

    X(xyz) = cos(lb.l) * cos(lb.b);
    Y(xyz) = sin(lb.l) * cos(lb.b);
    Z(xyz) = sin(lb.b);

    return xyz;
}

static RADec surveyToRADec(real slong, real slat)
{
    RADec raDec;
    real x1, y1, z1;

    const real etaPole = surveyCenterDec_rad;

    /* Rotation */
    x1        = -mw_sin(slong);
    y1        = mw_cos(slat + etaPole) * mw_cos(slong);
    z1        = mw_sin(slat + etaPole) * mw_cos(slong);
    raDec.ra  = mw_atan2(y1, x1) + NODE_GC_COORDS_RAD;
    raDec.dec = mw_asin(z1);

    return raDec;
}

static LB surveyToLB(real slong, real slat)
{
    mwvector v1, v2;
    RADec raDec;

    /* To equatorial */
    raDec = surveyToRADec(slong, slat);

    /* To galactic coordinates */
    v1 = raDecToCartesian(raDec);

    /* Equatorial to Galactic */
    v2 = mw_mulmv(rmat, v1);

    return cartesianToSpherical(v2);
}

/* Get normal vector of data slice from stripe number */
mwvector stripe_normal(int wedge)
{
    real eta;
    LB lb;

    eta = atEtaFromStripeNumber_rad(wedge);
    lb = surveyToLB(0.0, M_PI_2 + eta);
    return lbToXyz(lb);
}


/* Convert GC coordinates (mu, nu) into l and b for the given wedge. */
HOT CONST_F
LB gc2lb(const int wedge, const real mu, const real nu)
{
    LB lb;
    real sinmunode, cosmunode;
    real sinnu, cosnu;
    real munode;
    real sininc, cosinc;
    real sinra, cosra;

    real x12, y2, y1, z1;
    real ra, dec;
    real wedge_eta, wedge_incl;
    real cosdec;
    mwvector v1, v2;
    real r;

    /* Rotation */
    mw_sincos(d2r(nu), &sinnu, &cosnu);

    munode = mu - NODE_GC_COORDS;
    mw_sincos(d2r(munode), &sinmunode, &cosmunode);

    x12 = cosmunode * cosnu;  /* x1 = x2 */
    y2 = sinmunode * cosnu;
    /* z2 = sin(nu) */

    wedge_eta = atEtaFromStripeNumber_rad(wedge);

    /* Get inclination for the given wedge. */
    wedge_incl = wedge_eta + d2r(surveyCenterDec);

    mw_sincos(wedge_incl, &sininc, &cosinc);

    y1 = y2 * cosinc - sinnu * sininc;
    z1 = y2 * sininc + sinnu * cosinc;

    ra = mw_atan2(y1, x12) + NODE_GC_COORDS_RAD;
    dec = mw_asin(z1);


    /* Spherical to Cartesian */
    mw_sincos(ra, &sinra, &cosra);

    cosdec = mw_cos(dec);
    SET_VECTOR(v1,
               cosra * cosdec,
               sinra * cosdec,
               z1         /* mw_sin(asin(z1)) == z1 */
              );

    /* Equatorial to Galactic */

    /* Matrix rmat * vector v1 -> vector vb */
    v2 = mw_mulmv(rmat, v1);

    /* Cartesian to spherical */
    r = mw_hypot(X(v2), Y(v2));

    LB_L(lb) = ( r != 0.0 ) ? mw_atan2( Y(v2), X(v2) ) : 0.0;
    LB_B(lb) = ( Z(v2) != 0.0 ) ? mw_atan2( Z(v2), r ) : 0.0;

    LB_L(lb) = r2d(LB_L(lb));
    LB_B(lb) = r2d(LB_B(lb));

    return lb;
}

