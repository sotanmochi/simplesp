﻿//--------------------------------------------------------------------------------
// Copyright (c) 2017-2019, sanko-shoko. All rights reserved.
//--------------------------------------------------------------------------------

#ifndef __SP_DEPTH_H__
#define __SP_DEPTH_H__

#include "spcore/spcore.h"

namespace sp{


    //--------------------------------------------------------------------------------
    //  bilateral filter
    //--------------------------------------------------------------------------------
    
    SP_CPUFUNC void bilateralFilterDepth(Mem<SP_REAL> &dst, const Mem<SP_REAL> &src, const SP_REAL asigma = 0.8, const SP_REAL bsigma = 10.0){
        SP_ASSERT(isValid(2, src));

        dst.resize(2, src.dsize);
        const Mem<SP_REAL> &tmp = (&dst != &src) ? src : clone(src);

        const int size = static_cast<int>(3.0 * asigma);
        const SP_REAL asigma2 = asigma * asigma;

        Mem2<SP_REAL> kernel(2 * size + 1, 2 * size + 1);
        for (int y = -size; y <= size; y++){
            for (int x = -size; x <= size; x++){
                const SP_REAL r = x * x + y * y;
                const SP_REAL v = 1.0 / (sqrt(2.0 * SP_PI) * asigma) * exp(-r / (2.0 * asigma2));
                kernel(x + size, y + size) = v;
            }
        }

        const SP_REAL tscale = 10.0;

        Mem1<SP_REAL> table(100);
        for (int i = 0; i < table.size(); i++){
            const SP_REAL r = i * i / (tscale * tscale);
            const SP_REAL v = exp(-r / 2.0);
            table[i] = v;
        }

        const Rect rect = getRect2(dst.dsize);

        for (int v = 0; v < dst.dsize[1]; v++){
            for (int u = 0; u < dst.dsize[0]; u++){

                const SP_REAL base = acs2(tmp, u, v);
                if (base == 0.0) continue;

                SP_REAL sum = 0.0, div = 0.0;
                for (int ky = -size; ky <= size; ky++){
                    for (int kx = -size; kx <= size; kx++){
                        if (inRect2(rect, u + kx, v + ky) == false) continue;

                        const SP_REAL &val = acs2(tmp, u + kx, v + ky);
                        if (val == 0.0) continue;

                        const SP_REAL a = kernel(kx + size, ky + size);
                        const SP_REAL b = table(round(tscale * fabs(val - base) / bsigma));

                        sum += a * b * val;
                        div += a * b;
                    }
                }

                acs2(dst, u, v) = sum / div;
            }
        }
    }


    //--------------------------------------------------------------------------------
    //  pyrdown
    //--------------------------------------------------------------------------------

    SP_CPUFUNC void pyrdownDepth(Mem<SP_REAL> &dst, const Mem<SP_REAL> &src){
        SP_ASSERT(isValid(2, src));

        const Mem<SP_REAL> &tmp = (&dst != &src) ? src : clone(src);

        const int dsize0 = (tmp.dsize[0] + 1) / 2;
        const int dsize1 = (tmp.dsize[1] + 1) / 2;

        const int dsize[2] = { dsize0, dsize1 };
        dst.resize(2, dsize);
        dst.zero();

        const int kernel[3][3] = { { 1, 2, 1 }, { 2, 4, 2 }, { 1, 2, 1 } };
        for (int v = 0; v < dst.dsize[1]; v++){
            for (int u = 0; u < dst.dsize[0]; u++){
                const SP_REAL su = 2 * u;
                const SP_REAL sv = 2 * v;
                if (acs2(tmp, su + 0, sv + 0) == 0.0) continue;

                SP_REAL sum = 0.0;
                SP_REAL div = 0.0;
                for (int ky = -1; ky <= 1; ky++){
                    for (int kx = -1; kx <= 1; kx++){
                        const SP_REAL val = acs2(tmp, su - kx, sv - ky);
                        if (val == 0.0) continue;

                        const SP_REAL k = kernel[kx + 1][ky + 1];
                        sum += k * val;
                        div += k;
                    }
                }

                if (div != 0.0){
                    acs2(dst, u, v) = sum / div;
                }
            }
        }
    }


    //--------------------------------------------------------------------------------
    //  vector
    //--------------------------------------------------------------------------------

    SP_CPUFUNC void cnvDepthToVec(Mem<Vec3> &dst, const CamParam &cam, const Mem<SP_REAL> &src) {
        SP_ASSERT(isValid(2, src));

        dst.resize(2, src.dsize);
        dst.zero();

        for (int v = 0; v < dst.dsize[1] - 1; v++) {
            for (int u = 0; u < dst.dsize[0] - 1; u++) {

                const SP_REAL val0 = acs2(src, u, v);
                if (val0 == 0.0) continue;

                const SP_REAL val1 = acs2(src, u + 1, v);
                const SP_REAL val2 = acs2(src, u, v + 1);
                if (val1 == 0.0 || val2 == 0.0) continue;

                const Vec2 npx0 = invCam(cam, getVec2(u, v));
                const Vec3 vec0 = getVec3(npx0.x, npx0.y, 1.0) * val0;

                acs2(dst, u, v) = vec0;
            }
        }
    }

    SP_CPUFUNC void cnvDepthToVecPN(Mem<VecPN3> &dst, const CamParam &cam, const Mem<SP_REAL> &src){
        SP_ASSERT(isValid(2, src));

        dst.resize(2, src.dsize);
        dst.zero();

        for (int v = 0; v < dst.dsize[1] - 1; v++){
            for (int u = 0; u < dst.dsize[0] - 1; u++){

                const SP_REAL val0 = acs2(src, u, v);
                if (val0 == 0.0) continue;

                const SP_REAL val1 = acs2(src, u + 1, v);
                const SP_REAL val2 = acs2(src, u, v + 1);
                if (val1 == 0.0 || val2 == 0.0) continue;

                const Vec2 npx0 = invCam(cam, getVec2(u, v));
                const Vec2 npx1 = invCam(cam, getVec2(u + 1, v));
                const Vec2 npx2 = invCam(cam, getVec2(u, v + 1));

                const Vec3 vec0 = getVec3(npx0.x, npx0.y, 1.0) * val0;
                const Vec3 vec1 = getVec3(npx1.x, npx1.y, 1.0) * val1;
                const Vec3 vec2 = getVec3(npx2.x, npx2.y, 1.0) * val2;

                const Vec3 nrm = unitVec(crsVec(vec2 - vec0, vec1 - vec0));

                acs2(dst, u, v) = getVecPN3(vec0, nrm);
            }
        }
    }

    SP_CPUFUNC void cnvVecToDepth(Mem<SP_REAL> &dst, const Mem<Vec3> &src) {
        SP_ASSERT(isValid(2, src));

        dst.resize(2, src.dsize);
        dst.zero();

        for (int i = 0; i < dst.size(); i++) {
            dst[i] = src[i].z;
        }
    }

    SP_CPUFUNC void cnvVecPNToDepth(Mem<SP_REAL> &dst, const Mem<VecPN3> &src) {
        SP_ASSERT(isValid(2, src));

        dst.resize(2, src.dsize);
        dst.zero();

        for (int i = 0; i < dst.size(); i++) {
            dst[i] = src[i].pos.z;
        }
    }



}
#endif