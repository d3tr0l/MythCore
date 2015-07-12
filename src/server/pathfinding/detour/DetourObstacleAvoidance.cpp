/*
 * Copyright (C) 2008 - 2011 Trinity <http://www.trinitycore.org/>
 *
 * Copyright (C) 2010 - 2013 Myth Project <http://mythprojectnetwork.blogspot.com/>
 *
 * Copyright (C) 2012 SymphonyArt <http://symphonyart.com/>
 *
 * Myth Project's source is based on the Trinity Project source, you can find the
 * link to that easily in Trinity Copyrights. Myth Project is a private community.
 * To get access, you either have to donate or pass a developer test.
 * You may not share Myth Project's sources! For personal use only.
 */

#include "DetourObstacleAvoidance.h"
#include "DetourCommon.h"
#include "DetourAlloc.h"
#include "DetourAssert.h"
#include <string.h>
#include <math.h>
#include <float.h>
#include <new>

static int sweepCircleCircle(const float* c0, const float r0, const float* v,
                             const float* c1, const float r1,
                             float& tmin, float& tmax)
{
    static const float EPS = 0.0001f;
    float s[3];
    dtVsub(s,c1,c0);
    float r = r0+r1;
    float c = dtVdot2D(s,s) - r*r;
    float a = dtVdot2D(v,v);
    if(a < EPS) return 0;    // not moving

    // Overlap, calc time to exit.
    float b = dtVdot2D(v,s);
    float d = b*b - a*c;
    if(d < 0.0f) return 0; // no intersection.
    a = 1.0f / a;
    const float rd = dtSqrt(d);
    tmin = (b - rd) * a;
    tmax = (b + rd) * a;
    return 1;
}

static int isectRaySeg(const float* ap, const float* u,
                       const float* bp, const float* bq,
                       float& t)
{
    float v[3], w[3];
    dtVsub(v,bq,bp);
    dtVsub(w,ap,bp);
    float d = dtVperp2D(u,v);
    if(fabsf(d) < 1e-6f) return 0;
    d = 1.0f/d;
    t = dtVperp2D(v,w) * d;
    if(t < 0 || t > 1) return 0;
    float s = dtVperp2D(u,w) * d;
    if(s < 0 || s > 1) return 0;
    return 1;
}

dtObstacleAvoidanceDebugData* dtAllocObstacleAvoidanceDebugData()
{
    void* mem = dtAlloc(sizeof(dtObstacleAvoidanceDebugData), DT_ALLOC_PERM);
    if(!mem) return 0;
    return new(mem) dtObstacleAvoidanceDebugData;
}

void dtFreeObstacleAvoidanceDebugData(dtObstacleAvoidanceDebugData* ptr)
{
    if(!ptr) return;
    ptr->~dtObstacleAvoidanceDebugData();
    dtFree(ptr);
}

dtObstacleAvoidanceDebugData::dtObstacleAvoidanceDebugData() :
    m_nsamples(0),
    m_maxSamples(0),
    m_vel(0),
    m_ssize(0),
    m_pen(0),
    m_vpen(0),
    m_vcpen(0),
    m_spen(0),
    m_tpen(0)
{
}

dtObstacleAvoidanceDebugData::~dtObstacleAvoidanceDebugData()
{
    dtFree(m_vel);
    dtFree(m_ssize);
    dtFree(m_pen);
    dtFree(m_vpen);
    dtFree(m_vcpen);
    dtFree(m_spen);
    dtFree(m_tpen);
}

bool dtObstacleAvoidanceDebugData::init(const int maxSamples)
{
    dtAssert(maxSamples);
    m_maxSamples = maxSamples;

    m_vel = (float*)dtAlloc(sizeof(float)*3*m_maxSamples, DT_ALLOC_PERM);
    if(!m_vel)
        return false;
    m_pen = (float*)dtAlloc(sizeof(float)*m_maxSamples, DT_ALLOC_PERM);
    if(!m_pen)
        return false;
    m_ssize = (float*)dtAlloc(sizeof(float)*m_maxSamples, DT_ALLOC_PERM);
    if(!m_ssize)
        return false;
    m_vpen = (float*)dtAlloc(sizeof(float)*m_maxSamples, DT_ALLOC_PERM);
    if(!m_vpen)
        return false;
    m_vcpen = (float*)dtAlloc(sizeof(float)*m_maxSamples, DT_ALLOC_PERM);
    if(!m_vcpen)
        return false;
    m_spen = (float*)dtAlloc(sizeof(float)*m_maxSamples, DT_ALLOC_PERM);
    if(!m_spen)
        return false;
    m_tpen = (float*)dtAlloc(sizeof(float)*m_maxSamples, DT_ALLOC_PERM);
    if(!m_tpen)
        return false;

    return true;
}

void dtObstacleAvoidanceDebugData::reset()
{
    m_nsamples = 0;
}

void dtObstacleAvoidanceDebugData::addSample(const float* vel, const float ssize, const float pen,
                                             const float vpen, const float vcpen, const float spen, const float tpen)
{
    if(m_nsamples >= m_maxSamples)
        return;
    dtAssert(m_vel);
    dtAssert(m_ssize);
    dtAssert(m_pen);
    dtAssert(m_vpen);
    dtAssert(m_vcpen);
    dtAssert(m_spen);
    dtAssert(m_tpen);
    dtVcopy(&m_vel[m_nsamples*3], vel);
    m_ssize[m_nsamples] = ssize;
    m_pen[m_nsamples] = pen;
    m_vpen[m_nsamples] = vpen;
    m_vcpen[m_nsamples] = vcpen;
    m_spen[m_nsamples] = spen;
    m_tpen[m_nsamples] = tpen;
    m_nsamples++;
}

static void normalizeArray(float* arr, const int n)
{
    // Normalize penaly range.
    float minPen = FLT_MAX;
    float maxPen = -FLT_MAX;
    for(int i = 0; i < n; ++i)
    {
        minPen = dtMin(minPen, arr[i]);
        maxPen = dtMax(maxPen, arr[i]);
    }
    const float penRange = maxPen-minPen;
    const float s = penRange > 0.001f ? (1.0f / penRange) : 1;
    for(int i = 0; i < n; ++i)
        arr[i] = dtClamp((arr[i]-minPen)*s, 0.0f, 1.0f);
}

void dtObstacleAvoidanceDebugData::normalizeSamples()
{
    normalizeArray(m_pen, m_nsamples);
    normalizeArray(m_vpen, m_nsamples);
    normalizeArray(m_vcpen, m_nsamples);
    normalizeArray(m_spen, m_nsamples);
    normalizeArray(m_tpen, m_nsamples);
}

dtObstacleAvoidanceQuery* dtAllocObstacleAvoidanceQuery()
{
    void* mem = dtAlloc(sizeof(dtObstacleAvoidanceQuery), DT_ALLOC_PERM);
    if(!mem) return 0;
    return new(mem) dtObstacleAvoidanceQuery;
}

void dtFreeObstacleAvoidanceQuery(dtObstacleAvoidanceQuery* ptr)
{
    if(!ptr) return;
    ptr->~dtObstacleAvoidanceQuery();
    dtFree(ptr);
}

dtObstacleAvoidanceQuery::dtObstacleAvoidanceQuery() :
    m_velBias(0.0f),
    m_weightDesVel(0.0f),
    m_weightCurVel(0.0f),
    m_weightSide(0.0f),
    m_weightToi(0.0f),
    m_horizTime(0.0f),
    m_maxCircles(0),
    m_circles(0),
    m_ncircles(0),
    m_maxSegments(0),
    m_segments(0),
    m_nsegments(0)
{
}

dtObstacleAvoidanceQuery::~dtObstacleAvoidanceQuery()
{
    dtFree(m_circles);
    dtFree(m_segments);
}

bool dtObstacleAvoidanceQuery::init(const int maxCircles, const int maxSegments)
{
    m_maxCircles = maxCircles;
    m_ncircles = 0;
    m_circles = (dtObstacleCircle*)dtAlloc(sizeof(dtObstacleCircle)*m_maxCircles, DT_ALLOC_PERM);
    if(!m_circles)
        return false;
    memset(m_circles, 0, sizeof(dtObstacleCircle)*m_maxCircles);

    m_maxSegments = maxSegments;
    m_nsegments = 0;
    m_segments = (dtObstacleSegment*)dtAlloc(sizeof(dtObstacleSegment)*m_maxSegments, DT_ALLOC_PERM);
    if(!m_segments)
        return false;
    memset(m_segments, 0, sizeof(dtObstacleSegment)*m_maxSegments);

    return true;
}

void dtObstacleAvoidanceQuery::reset()
{
    m_ncircles = 0;
    m_nsegments = 0;
}

void dtObstacleAvoidanceQuery::addCircle(const float* pos, const float rad,
                                         const float* vel, const float* dvel)
{
    if(m_ncircles >= m_maxCircles)
        return;

    dtObstacleCircle* cir = &m_circles[m_ncircles++];
    dtVcopy(cir->p, pos);
    cir->rad = rad;
    dtVcopy(cir->vel, vel);
    dtVcopy(cir->dvel, dvel);
}

void dtObstacleAvoidanceQuery::addSegment(const float* p, const float* q)
{
    if(m_nsegments > m_maxSegments)
        return;

    dtObstacleSegment* seg = &m_segments[m_nsegments++];
    dtVcopy(seg->p, p);
    dtVcopy(seg->q, q);
}

void dtObstacleAvoidanceQuery::prepare(const float* pos, const float* dvel)
{
    // Prepare obstacles
    for(int i = 0; i < m_ncircles; ++i)
    {
        dtObstacleCircle* cir = &m_circles[i];

        // Side
        const float* pa = pos;
        const float* pb = cir->p;

        const float orig[3] = {0, 0};
        float dv[3];
        dtVsub(cir->dp,pb,pa);
        dtVnormalize(cir->dp);
        dtVsub(dv, cir->dvel, dvel);

        const float a = dtTriArea2D(orig, cir->dp,dv);
        if(a < 0.01f)
        {
            cir->np[0] = -cir->dp[2];
            cir->np[2] = cir->dp[0];
        }
        else
        {
            cir->np[0] = cir->dp[2];
            cir->np[2] = -cir->dp[0];
        }
    }

    for(int i = 0; i < m_nsegments; ++i)
    {
        dtObstacleSegment* seg = &m_segments[i];

        // Precalc if the agent is really close to the segment.
        const float r = 0.01f;
        float t;
        seg->touch = dtDistancePtSegSqr2D(pos, seg->p, seg->q, t) < dtSqr(r);
    }
}

float dtObstacleAvoidanceQuery::processSample(const float* vcand, const float cs,
                                              const float* pos, const float rad,
                                              const float vmax, const float* vel, const float* dvel,
                                              dtObstacleAvoidanceDebugData* debug)
{
    // Find min time of impact and exit amongst all obstacles.
    float tmin = m_horizTime;
    float side = 0;
    int nside = 0;

    for(int i = 0; i < m_ncircles; ++i)
    {
        const dtObstacleCircle* cir = &m_circles[i];

        // RVO
        float vab[3];
        dtVscale(vab, vcand, 2);
        dtVsub(vab, vab, vel);
        dtVsub(vab, vab, cir->vel);

        // Side
        side += dtClamp(dtMin(dtVdot2D(cir->dp,vab)*0.5f+0.5f, dtVdot2D(cir->np,vab)*2), 0.0f, 1.0f);
        nside++;

        float htmin = 0, htmax = 0;
        if(!sweepCircleCircle(pos,rad, vab, cir->p,cir->rad, htmin, htmax))
            continue;

        // Handle overlapping obstacles.
        if(htmin < 0.0f && htmax > 0.0f)
        {
            // Avoid more when overlapped.
            htmin = -htmin * 0.5f;
        }

        if(htmin >= 0.0f)
        {
            // The closest obstacle is somewhere ahead of us, keep track of nearest obstacle.
            if(htmin < tmin)
                tmin = htmin;
        }
    }

    for(int i = 0; i < m_nsegments; ++i)
    {
        const dtObstacleSegment* seg = &m_segments[i];
        float htmin = 0;

        if(seg->touch)
        {
            // Special case when the agent is very close to the segment.
            float sdir[3], snorm[3];
            dtVsub(sdir, seg->q, seg->p);
            snorm[0] = -sdir[2];
            snorm[2] = sdir[0];
            // If the velocity is pointing towards the segment, no collision.
            if(dtVdot2D(snorm, vcand) < 0.0f)
                continue;
            // Else immediate collision.
            htmin = 0.0f;
        }
        else
        {
            if(!isectRaySeg(pos, vcand, seg->p, seg->q, htmin))
                continue;
        }

        // Avoid less when facing walls.
        htmin *= 2.0f;

        // The closest obstacle is somewhere ahead of us, keep track of nearest obstacle.
        if(htmin < tmin)
            tmin = htmin;
    }

    // Normalize side bias, to prevent it dominating too much.
    if(nside)
        side /= nside;

    const float ivmax = 1.0f / vmax;
    const float vpen = m_weightDesVel * (dtVdist2D(vcand, dvel) * ivmax);
    const float vcpen = m_weightCurVel * (dtVdist2D(vcand, vel) * ivmax);
    const float spen = m_weightSide * side;
    const float tpen = m_weightToi * (1.0f/(0.1f+tmin / m_horizTime));

    const float penalty = vpen + vcpen + spen + tpen;

    // Store different penalties for debug viewing
    if(debug)
        debug->addSample(vcand, cs, penalty, vpen, vcpen, spen, tpen);

    return penalty;
}

void dtObstacleAvoidanceQuery::sampleVelocityGrid(const float* pos, const float rad, const float vmax,
                                                  const float* vel, const float* dvel,
                                                  float* nvel, const int gsize,
                                                  dtObstacleAvoidanceDebugData* debug)
{
    prepare(pos, dvel);

    dtVset(nvel, 0, 0, 0);

    if(debug)
        debug->reset();

    const float cvx = dvel[0] * m_velBias;
    const float cvz = dvel[2] * m_velBias;
    const float cs = vmax * 2 * (1 - m_velBias) / (float)(gsize-1);
    const float half = (gsize-1)*cs*0.5f;

    float minPenalty = FLT_MAX;

    for(int y = 0; y < gsize; ++y)
    {
        for(int x = 0; x < gsize; ++x)
        {
            float vcand[3];
            vcand[0] = cvx + x*cs - half;
            vcand[1] = 0;
            vcand[2] = cvz + y*cs - half;

            if(dtSqr(vcand[0])+dtSqr(vcand[2]) > dtSqr(vmax+cs/2)) continue;

            const float penalty = processSample(vcand, cs, pos,rad,vmax,vel,dvel, debug);
            if(penalty < minPenalty)
            {
                minPenalty = penalty;
                dtVcopy(nvel, vcand);
            }
        }
    }
}

static const float DT_PI = 3.14159265f;

void dtObstacleAvoidanceQuery::sampleVelocityAdaptive(const float* pos, const float rad, const float vmax,
                                                      const float* vel, const float* dvel, float* nvel,
                                                      const int ndivs, const int nrings, const int depth,
                                                      dtObstacleAvoidanceDebugData* debug)
{
    prepare(pos, dvel);

    dtVset(nvel, 0, 0, 0);

    if(debug)
        debug->reset();

    // Build sampling pattern aligned to desired velocity.
    static const int MAX_PATTERN_DIVS = 32;
    static const int MAX_PATTERN_RINGS = 4;
    float pat[(MAX_PATTERN_DIVS*MAX_PATTERN_RINGS+1)*2];
    int npat = 0;

    const int nd = dtClamp(ndivs, 1, MAX_PATTERN_DIVS);
    const int nr = dtClamp(nrings, 1, MAX_PATTERN_RINGS);
    const float da = (1.0f/nd) * DT_PI*2;
    const float dang = atan2f(dvel[2], dvel[0]);

    // Always add sample at zero
    pat[npat*2+0] = 0;
    pat[npat*2+1] = 0;
    npat++;

    for(int j = 0; j < nr; ++j)
    {
        const float rad = (float)(nr-j)/(float)nr;
        float a = dang + (j&1)*0.5f*da;
        for(int i = 0; i < nd; ++i)
        {
            pat[npat*2+0] = cosf(a)*rad;
            pat[npat*2+1] = sinf(a)*rad;
            npat++;
            a += da;
        }
    }

    // Start sampling.
    float cr = vmax * (1.0f-m_velBias);
    float res[3];
    dtVset(res, dvel[0] * m_velBias, 0, dvel[2] * m_velBias);

    for(int k = 0; k < depth; ++k)
    {
        float minPenalty = FLT_MAX;
        float bvel[3];
        dtVset(bvel, 0, 0, 0);

        for(int i = 0; i < npat; ++i)
        {
            float vcand[3];
            vcand[0] = res[0] + pat[i*2+0]*cr;
            vcand[1] = 0;
            vcand[2] = res[2] + pat[i*2+1]*cr;

            if(dtSqr(vcand[0])+dtSqr(vcand[2]) > dtSqr(vmax+0.001f)) continue;

            const float penalty = processSample(vcand,cr/10, pos,rad,vmax,vel,dvel, debug);
            if(penalty < minPenalty)
            {
                minPenalty = penalty;
                dtVcopy(bvel, vcand);
            }
        }

        dtVcopy(res, bvel);

        cr *= 0.5f;
    }

    dtVcopy(nvel, res);
}

