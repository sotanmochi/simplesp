﻿//--------------------------------------------------------------------------------
// Copyright (c) 2017-2018, sanko-shoko. All rights reserved.
//--------------------------------------------------------------------------------

#ifndef __SP_TRACK3D_H__
#define __SP_TRACK3D_H__

#include "spcore/spcore.h"
#include "spapp/spdata/spmodel.h"
#include "spapp/spdata/spbmp.h"
#include "spapp/spalgo/sprandomforest.h"

#if SP_USE_OMP
#include <omp.h>
#endif

namespace sp{

    class TrackRF {
    public:
        const int POINT_NUM = 20;
        const int SAMPLE_NUM = 1500;

        const double RAND_TRN = 10.0;
        const double RAND_ROT = 10.0 * SP_PI / 180.0;

        struct GeoNode{
            // geodesic pose
            Pose pose;

            // sample points
            Mem1<Vec3> pnts;

            // train 6 tree
            RandomForestReg rf;
        };

        Mem1<GeoNode> m_nodes;

    public:

        Vec3 getDirect(const Pose &pose) {
            return unitVec((invRot(pose.rot) * pose.trn)) * -1.0;
        }

        bool valid() {
            return (m_nodes.size() > 0) ? true : false;
        }

        void train(const Mem1<Mesh3> &model, const int div = 3) {
            srand(0);

            const CamParam cam = getCamParam(300, 300);
            const double distance = getModelDistance(model, cam);

            const int gnum = getGeodesicMeshNum(div);
            m_nodes.resize(gnum);

#if SP_USE_OMP
#pragma omp parallel for
#endif
            for (int i = 0; i < gnum; i++) {
#if SP_USE_OMP
                if (omp_get_thread_num() == 0) {
                    printf("\rtrain [%s] ", progressBar(i, gnum / omp_get_num_threads()));
                }
                fflush(stdout);
#elif
                printf("\rtrain [%s] ", progressBar(i, gnum));
                fflush(stdout);
#endif

                const Pose pose = getGeodesicPose(div, i, distance);

                Mem2<double> depth;
                renderDepth(depth, cam, pose, model);

                makeTree(m_nodes[i], cam, pose, depth, i);
            }
        }

        void makeTree(GeoNode &gnode, const CamParam &cam, const Pose &pose, const Mem2<double> &depth, const int seed) {
            gnode.pose = pose;

            genSamplePnts(gnode.pnts, cam, pose, depth, seed);

            Mem1<Mem<double> > Xs;
            Mem1<double> Ys[6];

            genDataset(Xs, Ys, gnode.pnts, cam, pose, depth, seed);
            
            for (int s = 0; s < 6; s++) {
                gnode.rf.train(Xs, Ys[s], SAMPLE_NUM);
            }
        }

        bool execute(Pose &pose, const CamParam &cam, const Mem2<double> &depth) {

            const double maxAngle = 35.0 * SP_PI / 180.0;
 
            Mem1<Mat> vals;
            Mem1<Mat> devs;
            Mem1<GeoNode*> refs;

            for (int i = 0; i < m_nodes.size(); i++) {
                GeoNode &node = m_nodes[i];

                const Vec3 ref = getDirect(node.pose);
                const Vec3 vec = getDirect(pose);

                const double angle = acos(dotVec(vec, ref));
                if (fabs(angle) < maxAngle) {

                    Mem<double> data = Mem1<double>(POINT_NUM);
                    for (int p = 0; p < POINT_NUM; p++) {
                        const Vec3 pos = pose * node.pnts[p];
                        const Vec2 npx = prjVec(pos);
                        const Vec2 pix = mulCam(cam, npx);

                        double d = depth(round(pix.x), round(pix.y));
                        d = (d > 0.0) ? d : pose.trn.z;

                        const Vec3 v = getVec(npx.x, npx.y, 1.0) * d;
                        data[p] = dotVec(ref, invPose(pose) * v - node.pnts[p]);
                    }

                    Mat val = zeroMat(6, 1);
                    Mat dev = zeroMat(6, 1);

                    Mem1<const RandomForestReg::Node*> &rfnode = node.rf.execute2(data);
                    for (int s = 0; s < 6; s++) {
                        val[s] = rfnode[s]->val;
                        dev[s] = rfnode[s]->dev;
                    }
                    vals.push(val);
                    devs.push(dev);
                    refs.push(&node);
                }
            }

            {
                struct Tmp {
                    double v;
                    double eval;
                    bool operator > (const Tmp t) const { return this->eval > t.eval; }
                    bool operator < (const Tmp t) const { return this->eval < t.eval; }
                };
                
                Mat sum = zeroMat(6, 1);
                for (int p = 0; p < 6; p++) {
                    Mem1<Tmp> tmps;
                    for (int i = 0; i < vals.size(); i++) {
                        Mat m = zeroMat(6, 1);
                        m[p] = vals[i][p];

                        Tmp tmp;
                        tmp.v = vals[i][p];
                        //tmp.eval = devs[i][p];
                        tmp.eval = evalPose(*refs[i], cam, pose * invPose(getPose(m)), depth);
                        tmps.push(tmp);
                    }
                    sort(tmps);
                    const int num = round(tmps.size() * 0.2);
                    for (int i = 0; i < num; i++) {
                        sum[p] += tmps[i].v;
                    }
                    sum[p] /= num;
                }
                pose = pose * invPose(getPose(sum));
                
            }
            return true;
        }

        bool execute(Pose &pose, const CamParam &cam, const Mem2<double> &depth, const int itmax) {
            bool ret = true;
            for (int it = 0; ret && it < itmax; it++) {
                ret = execute(pose, cam, depth);
            }
            return ret;
        }

    private:

        void genSamplePnts(Mem1<Vec3> &pnts, const CamParam &cam, const Pose &pose, const Mem2<double> &depth, const int seed) {
            srand(seed);

            struct Tmp {
                Vec3 pos;
                double eval;

                bool operator > (const Tmp &pd) const { return eval > pd.eval; }
                bool operator < (const Tmp &pd) const { return eval < pd.eval; }
            };

            Mem1<Tmp> tmps;
            const double angle = randValUnif() * SP_PI;
            const Vec2 nl = getVec(cos(angle), sin(angle));

            for (int v = 0; v < cam.dsize[1]; v++) {
                for (int u = 0; u < cam.dsize[0]; u++) {
                    const double d = depth(u, v);
                    if (d > 0.0) {
                        const Vec2 npx = invCam(cam, getVec(u, v));

                        Tmp tmp;
                        tmp.pos = getVec(npx.x, npx.y, 1.0) * d;
                        tmp.eval = dotVec(nl, getVec(u, v));
                        tmps.push(tmp);
                    }
                }
            }

            sort(tmps);
            pnts.clear();

            const double rate = 0.3 * randValUnif() + 0.4; // (0.1, 0.7)
            for (int p = 0; p < POINT_NUM; p++) {
                const int i = rand() % round(rate * tmps.size());
                const Vec3 vec = invPose(pose) * tmps[i].pos;
                pnts.push(vec);
            }
        }

        void genDataset(Mem1<Mem<double> > &Xs, Mem1<double> *Ys, const Mem1<Vec3> &pnts, const CamParam &cam, const Pose &pose, const Mem2<double> &depth, const int seed) {
            srand(seed);

            const Vec3 Nv = getDirect(pose);

            for (int i = 0; i < SAMPLE_NUM; i++) {
                const Pose delta = randPoseUnif(RAND_ROT, RAND_TRN);
                const Pose tpose = pose * delta;
                Mem<double> data = Mem1<double>(POINT_NUM);

                for (int p = 0; p < POINT_NUM; p++) {
                    const Vec3 pos = tpose * pnts[p];
                    const Vec2 npx = prjVec(pos);
                    const Vec2 pix = mulCam(cam, npx);

                    const double d = depth(round(pix.x), round(pix.y));

                    if (d > 0.0) {
                        const Vec3 vec = getVec(npx.x, npx.y, 1.0) * d;
                        const Vec3 vec1 = invPose(tpose) * vec;
                        const Vec3 vec2 = pnts[p];
                        const Vec3 dif = vec1 - vec2;
                        data[p] = dotVec(Nv, invPose(tpose) * vec - pnts[p]);
                    }
                    else {
                        data[p] = tpose.trn.z;
                    }

                }

                Xs.push(data);

                const Mat m = getMat(delta, 6, 1);
                for (int s = 0; s < 6; s++) {
                    Ys[s].push(m[s]);
                }
            }
        }

        double evalPose(const GeoNode &node, const CamParam &cam, const Pose &pose, const Mem2<double> &depth) {

            Mem1<double> data(POINT_NUM);
            for (int p = 0; p < POINT_NUM; p++) {
                const Vec3 pos = pose * node.pnts[p];
                const Vec2 npx = prjVec(pos);
                const Vec2 pix = mulCam(cam, npx);

                const double d = depth(round(pix.x), round(pix.y));
                if (d > 0.0) {
                    data[p] = fabs(pos.z - d);
                }
                else {
                    data[p] = SP_INFINITY;
                }
            }
            return medianVal(data);
        }

    };

}
#endif