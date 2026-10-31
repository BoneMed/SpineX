// Step3_CobbSegment.cpp — fit endplate planes, compute coronal/sagittal tilts,
// and segment the spine into Cobb curves with apex localization.
#include "CobbAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include <vtkMath.h>

#ifndef COBB_PI
#define COBB_PI 3.14159265358979323846
#endif

namespace cobb {

namespace {

void eigh3sym(const double cov[3][3], double eval[3], double evec[3][3]) {
    double a[3][3]; std::memcpy(a, cov, sizeof(a));
    double* aRows[3] = { a[0], a[1], a[2] };
    double w[3];
    double v[3][3];
    double* vRows[3] = { v[0], v[1], v[2] };
    vtkMath::Jacobi(aRows, w, vRows);
    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int x, int y) { return w[x] < w[y]; });
    for (int k = 0; k < 3; ++k) {
        eval[k] = w[order[k]];
        for (int r = 0; r < 3; ++r) evec[r][k] = v[r][order[k]];
    }
}

// Return signed tilt in degrees of `normal` projected onto coronal (XZ) or
// sagittal (YZ) global plane.  Same convention as Python: atan2(horizontal, Z).
double signed_tilt_deg(const Vec3& nIn, const std::string& plane) {
    Vec3 n = normalize(nIn);
    double horizontal = (plane == "coronal") ? n[0] : n[1];
    double vertical   = n[2];
    return std::atan2(horizontal, vertical) * 180.0 / COBB_PI;
}

// Fit one normal+center to the triangles selected by `mask` (PCA on centers).
void fit_endplate_normal(const EndplateResult& item,
                         const std::vector<uint8_t>& mask,
                         const Vec3& systemAxisZ,
                         Vec3& outNormal, Vec3& outCenter, int& outCells)
{
    outCells = 0;
    if (mask.empty() || item.triangles.empty()) {
        outNormal = systemAxisZ; outCenter = item.system.centroid;
        return;
    }
    const int nTri = static_cast<int>(item.triangles.size() / 3);
    std::vector<Vec3> centers; centers.reserve(nTri);
    std::vector<double> areas;  areas.reserve(nTri);

    auto* mesh = item.bodyMesh.Get();
    if (!mesh) {
        outNormal = systemAxisZ; outCenter = item.system.centroid; return;
    }
    auto polys = mesh->GetPolys(); polys->InitTraversal();
    for (int t = 0; t < nTri; ++t) {
        if (!mask[t]) continue;
        int v0 = item.triangles[3*t], v1 = item.triangles[3*t+1], v2 = item.triangles[3*t+2];
        double p0[3], p1[3], p2[3];
        mesh->GetPoint(v0, p0); mesh->GetPoint(v1, p1); mesh->GetPoint(v2, p2);
        Vec3 c = { (p0[0]+p1[0]+p2[0])/3.0, (p0[1]+p1[1]+p2[1])/3.0, (p0[2]+p1[2]+p2[2])/3.0 };
        Vec3 e1 = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        Vec3 e2 = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        Vec3 cr = cross(e1, e2);
        double a = 0.5 * norm(cr);
        centers.push_back(c);
        areas.push_back(std::max(a, 1e-12));
    }
    outCells = static_cast<int>(centers.size());
    if (outCells == 0) {
        outNormal = systemAxisZ; outCenter = item.system.centroid; return;
    }

    double wSum = 0.0; Vec3 wc = {0,0,0};
    for (size_t i = 0; i < centers.size(); ++i) {
        wSum += areas[i];
        wc[0] += centers[i][0] * areas[i];
        wc[1] += centers[i][1] * areas[i];
        wc[2] += centers[i][2] * areas[i];
    }
    Vec3 mean = { wc[0]/wSum, wc[1]/wSum, wc[2]/wSum };

    // Weighted covariance.
    double cov[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
    for (size_t i = 0; i < centers.size(); ++i) {
        double dx = centers[i][0] - mean[0];
        double dy = centers[i][1] - mean[1];
        double dz = centers[i][2] - mean[2];
        double w = areas[i];
        cov[0][0] += w*dx*dx;  cov[0][1] += w*dx*dy;  cov[0][2] += w*dx*dz;
        cov[1][1] += w*dy*dy;  cov[1][2] += w*dy*dz;
        cov[2][2] += w*dz*dz;
    }
    cov[0][0] /= wSum; cov[0][1] /= wSum; cov[0][2] /= wSum;
    cov[1][1] /= wSum; cov[1][2] /= wSum; cov[2][2] /= wSum;
    cov[1][0] = cov[0][1]; cov[2][0] = cov[0][2]; cov[2][1] = cov[1][2];

    double eval[3], evec[3][3];
    eigh3sym(cov, eval, evec);
    Vec3 normal = { evec[0][0], evec[1][0], evec[2][0] };  // smallest eigenvalue = plane normal
    normal = normalize(normal);
    if (dot(normal, systemAxisZ) < 0) normal = normal * -1.0;

    outNormal = normal;
    outCenter = mean;
}

}  // anonymous namespace

std::vector<EndplateMeasure> measureEndplates(const std::vector<EndplateResult>& items)
{
    std::vector<EndplateMeasure> out; out.reserve(items.size());
    for (const auto& it : items) {
        EndplateMeasure m;
        m.label = it.label;
        m.name  = it.system.name;
        m.centroid = it.system.centroid;

        Vec3 tn{0,0,1}, bn{0,0,1}, tc=it.system.centroid, bc=it.system.centroid;
        int tcells = 0, bcells = 0;
        fit_endplate_normal(it, it.topMask, it.system.axisZ, tn, tc, tcells);
        fit_endplate_normal(it, it.bottomMask, it.system.axisZ, bn, bc, bcells);

        m.topNormal = tn; m.bottomNormal = bn;
        m.topCenter = tc; m.bottomCenter = bc;
        m.topCells = tcells; m.bottomCells = bcells;

        Vec3 mid = { tn[0] + bn[0], tn[1] + bn[1], tn[2] + bn[2] };
        if (norm(mid) < 1e-6) m.midNormal = it.system.axisZ;
        else                  m.midNormal = normalize(mid);

        m.topCoronalTilt    = signed_tilt_deg(tn, "coronal");
        m.bottomCoronalTilt = signed_tilt_deg(bn, "coronal");
        m.midCoronalTilt    = signed_tilt_deg(m.midNormal, "coronal");
        m.topSagittalTilt    = signed_tilt_deg(tn, "sagittal");
        m.bottomSagittalTilt = signed_tilt_deg(bn, "sagittal");
        m.midSagittalTilt    = signed_tilt_deg(m.midNormal, "sagittal");
        out.push_back(m);
    }
    return out;
}

// ---------------------------------------------------------------------------
namespace {

int sign_db(double x, double eps) {
    if (x > eps) return 1;
    if (x < -eps) return -1;
    return 0;
}

double seg_cobb(const std::vector<double>& topT, const std::vector<double>& botT, int a, int b) {
    return std::abs(botT[b] - topT[a]);
}

CobbSegment make_seg(const std::vector<int>& labels,
                     const std::vector<double>& topT,
                     const std::vector<double>& botT, int a, int b) {
    CobbSegment s;
    s.startIndex = a; s.endIndex = b;
    s.startLabel = labels[a]; s.endLabel = labels[b];
    s.startName = label_name(labels[a]);
    s.endName   = label_name(labels[b]);
    s.signedAngleDeg = botT[b] - topT[a];
    s.cobbDeg = std::abs(s.signedAngleDeg);
    s.vertebraCount = b - a + 1;
    return s;
}

double point_line_distance_2d(double px, double py,
                              double ax, double ay, double bx, double by) {
    double abx = bx - ax, aby = by - ay;
    double denom = std::sqrt(abx*abx + aby*aby);
    if (denom < 1e-8) return std::sqrt((px-ax)*(px-ax) + (py-ay)*(py-ay));
    double crossv = abx * (py - ay) - aby * (px - ax);
    return std::abs(crossv) / denom;
}

void apex_for_segment(CobbSegment& s,
                      const std::vector<int>& labels,
                      const std::vector<EndplateMeasure>& measures) {
    int uev = s.startLabel, lev = s.endLabel;
    auto findCentroid = [&](int label) {
        for (const auto& m : measures) if (m.label == label) return m.centroid;
        return Vec3{0,0,0};
    };
    Vec3 cu = findCentroid(uev);
    Vec3 cl = findCentroid(lev);
    int bestLabel = uev;
    double bestDist = -1.0;
    for (int idx = s.startIndex; idx <= s.endIndex; ++idx) {
        Vec3 c = findCentroid(labels[idx]);
        double d = point_line_distance_2d(c[0], c[2], cu[0], cu[2], cl[0], cl[2]);
        if (d > bestDist) { bestDist = d; bestLabel = labels[idx]; }
    }
    s.apexLabel = bestLabel;
    s.apexName  = label_name(bestLabel);
    s.apexDistanceMm = bestDist;
}

std::string classify_curve(int apex, int uev, int lev) {
    if (apex >= 9 && apex <= 17)  return "thoracic";
    if (apex >= 21 && apex <= 24) return "lumbar";
    if (apex >= 18 && apex <= 20) {
        if (apex == 20 && lev >= 22) return "lumbar";
        if (uev <= 18 && lev <= 21) return "thoracolumbar";
        if (uev <= 18 && lev >= 21) return "thoracolumbar";
        int upper = std::abs(apex - uev);
        int lower = std::abs(lev - apex);
        return (upper > lower) ? "thoracolumbar" : "lumbar";
    }
    return "unknown";
}

void clean_boundaries(std::vector<int>& boundaries,
                      const std::vector<double>& topT,
                      const std::vector<double>& botT,
                      int minIntervals, double minCobb)
{
    while ((int)boundaries.size() > 2) {
        struct Bad { int intervals; double cobb; int k; };
        std::vector<Bad> bad;
        for (size_t k = 0; k + 1 < boundaries.size(); ++k) {
            int a = boundaries[k], b = boundaries[k+1];
            int intervals = b - a;
            double cobb = seg_cobb(topT, botT, a, b);
            if (intervals < minIntervals || cobb < minCobb)
                bad.push_back({intervals, cobb, (int)k});
        }
        if (bad.empty()) break;
        auto worst = std::min_element(bad.begin(), bad.end(),
            [](const Bad& a, const Bad& b) {
                if (a.intervals != b.intervals) return a.intervals < b.intervals;
                return a.cobb < b.cobb;
            });
        int k = worst->k;
        if (k == 0) boundaries.erase(boundaries.begin() + 1);
        else if (k == (int)boundaries.size() - 2) boundaries.erase(boundaries.end() - 2);
        else {
            double prev = seg_cobb(topT, botT, boundaries[k-1], boundaries[k]);
            double next = seg_cobb(topT, botT, boundaries[k+1], boundaries[k+2]);
            if (prev <= next) boundaries.erase(boundaries.begin() + k);
            else boundaries.erase(boundaries.begin() + k + 1);
        }
    }
}

}  // anonymous namespace

std::vector<CobbSegment> findCobbSegments(const std::vector<EndplateMeasure>& measures,
                                          const std::vector<int>& labels,
                                          const std::string& plane,
                                          const Params& params)
{
    if (measures.size() < 2) return {};

    std::vector<double> midT(measures.size()), topT(measures.size()), botT(measures.size());
    for (size_t i = 0; i < measures.size(); ++i) {
        if (plane == "coronal") {
            topT[i] = measures[i].topCoronalTilt;
            botT[i] = measures[i].bottomCoronalTilt;
            midT[i] = measures[i].midCoronalTilt;
        } else {
            topT[i] = measures[i].topSagittalTilt;
            botT[i] = measures[i].bottomSagittalTilt;
            midT[i] = measures[i].midSagittalTilt;
        }
    }

    // Slope sign segmentation
    int n = static_cast<int>(midT.size());
    std::vector<int> signs(n - 1, 0);
    for (int i = 0; i < n - 1; ++i)
        signs[i] = sign_db(midT[i+1] - midT[i], params.minSlopeDeg);
    // Fill zeros from neighbors
    for (int i = 0; i < (int)signs.size(); ++i) {
        if (signs[i] != 0) continue;
        int prev = 0, nxt = 0;
        for (int j = i - 1; j >= 0; --j) if (signs[j]) { prev = signs[j]; break; }
        for (int j = i + 1; j < (int)signs.size(); ++j) if (signs[j]) { nxt = signs[j]; break; }
        signs[i] = prev != 0 ? prev : nxt;
    }
    std::vector<int> bnd; bnd.push_back(0);
    int prev = signs.empty() ? 0 : signs[0];
    for (int i = 1; i < (int)signs.size(); ++i) {
        if (prev != 0 && signs[i] != 0 && signs[i] != prev) bnd.push_back(i);
        if (signs[i] != 0) prev = signs[i];
    }
    bnd.push_back(n - 1);
    clean_boundaries(bnd, topT, botT, params.minSegmentIntervals, 0.0);

    std::vector<CobbSegment> segments;
    for (size_t k = 0; k + 1 < bnd.size(); ++k) {
        if (bnd[k+1] <= bnd[k]) continue;
        segments.push_back(make_seg(labels, topT, botT, bnd[k], bnd[k+1]));
    }
    if (segments.empty())
        segments.push_back(make_seg(labels, topT, botT, 0, n - 1));

    // Apex + classify for coronal; clinical filter
    std::vector<CobbSegment> annotated;
    annotated.reserve(segments.size());
    for (auto seg : segments) {
        if (plane == "coronal") {
            apex_for_segment(seg, labels, measures);
            seg.curveType = classify_curve(seg.apexLabel, seg.startLabel, seg.endLabel);
            bool valid = (seg.cobbDeg >= params.coronalScoliosisThresholdDeg && seg.vertebraCount >= 3);
            seg.clinicalStatus = valid ? "valid_curve" : "invalid_curve";
            if (valid) annotated.push_back(seg);
        } else {
            // Sagittal: don't filter, just keep
            seg.clinicalStatus = "computed";
            annotated.push_back(seg);
        }
    }
    return annotated;
}

}  // namespace cobb
