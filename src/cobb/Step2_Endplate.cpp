// Step2_Endplate.cpp — body extraction + superior/inferior endplate triangle masks.
//
// Mirrors step2_endplate_detection.py.  All operations are in mm in physical
// space, but morphology is done on local-axis-aligned 2D rasters built per
// vertebra so we can reuse simple grid algorithms.
#include "CobbAnalysis.h"
#include "Morphology.h"

#ifndef COBB_PI
#define COBB_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <queue>
#include <unordered_map>

#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkFlyingEdges3D.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyDataNormals.h>
#include <vtkCleanPolyData.h>

namespace cobb {

using morph::Grid2D;
using morph::Grid3D;

namespace {

// ---- Small helpers ----------------------------------------------------------

inline Vec3 ijk_to_phys(int i, int j, int k,
                        const Vec3& origin,
                        const std::array<Vec3, 3>& sd) {
    return {
        origin[0] + i * sd[0][0] + j * sd[1][0] + k * sd[2][0],
        origin[1] + i * sd[0][1] + j * sd[1][1] + k * sd[2][1],
        origin[2] + i * sd[0][2] + j * sd[1][2] + k * sd[2][2],
    };
}

// Inclusive quantile-style percentile.
double percentile(std::vector<double>& v, double q) {
    if (v.empty()) return 0.0;
    if (v.size() == 1) return v[0];
    q = std::clamp(q, 0.0, 1.0);
    size_t k = static_cast<size_t>(q * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

double median(std::vector<double>& v) { return percentile(v, 0.5); }

// Project physical points to local coords (relative to system).
inline Vec3 to_local(const Vec3& p, const LCS& s) {
    Vec3 d = p - s.centroid;
    return { dot(d, s.axisX), dot(d, s.axisY), dot(d, s.axisZ) };
}

// ---- Body Y-cut detection --------------------------------------------------

double find_body_y_cut(const std::vector<Vec3>& localPoints,
                       double yBinMm, double gridMm,
                       double centerHalfWidthMm,
                       int minSlicePoints, int badPatience)
{
    if (localPoints.size() < 50) return 0.0;
    std::vector<double> ys, xs, zs;
    ys.reserve(localPoints.size());
    xs.reserve(localPoints.size());
    zs.reserve(localPoints.size());
    for (const auto& p : localPoints) { ys.push_back(p[1]); xs.push_back(p[0]); zs.push_back(p[2]); }

    auto ymin_iter = std::min_element(ys.begin(), ys.end());
    auto ymax_iter = std::max_element(ys.begin(), ys.end());
    double yMin = *ymin_iter, yMax = *ymax_iter;
    if (yMax - yMin < 4 * yBinMm) {
        std::vector<double> ycp = ys;
        return percentile(ycp, 0.6);
    }

    // x_mid from anterior 35% slab
    std::vector<double> ycp = ys;
    std::vector<double> ya = ycp;
    double yAntCut = percentile(ya, 0.35);
    std::vector<double> xAnt;
    for (size_t i = 0; i < ys.size(); ++i) if (ys[i] <= yAntCut) xAnt.push_back(xs[i]);
    double xMid = xAnt.empty() ? 0.0 : median(xAnt);

    double xLo = *std::min_element(xs.begin(), xs.end()) - gridMm;
    double zLo = *std::min_element(zs.begin(), zs.end()) - gridMm;
    int nx = static_cast<int>(std::ceil((*std::max_element(xs.begin(), xs.end()) - xLo) / gridMm)) + 2;
    int nz = static_cast<int>(std::ceil((*std::max_element(zs.begin(), zs.end()) - zLo) / gridMm)) + 2;
    if (nx < 3) nx = 3;
    if (nz < 3) nz = 3;

    std::vector<int> centerCols;
    for (int c = 0; c < nx; ++c) {
        double xc = xLo + c * gridMm;
        if (std::abs(xc - xMid) <= centerHalfWidthMm) centerCols.push_back(c);
    }
    if (centerCols.empty()) {
        int c = std::clamp(static_cast<int>(std::round((xMid - xLo) / gridMm)), 0, nx - 1);
        centerCols.push_back(c);
    }

    int nEdges = static_cast<int>(std::ceil((yMax - yMin) / yBinMm)) + 1;
    bool started = false;
    int badRun = 0;
    double lastGoodEnd = percentile(ycp, 0.6);
    double firstBadStart = -1e30;

    for (int e = 0; e < nEdges - 1; ++e) {
        double y0 = yMin + e * yBinMm;
        double y1 = y0 + yBinMm;
        std::vector<size_t> idx;
        for (size_t i = 0; i < ys.size(); ++i) if (ys[i] >= y0 && ys[i] < y1) idx.push_back(i);
        if (static_cast<int>(idx.size()) < minSlicePoints) {
            if (started) {
                ++badRun;
                if (firstBadStart < -1e29) firstBadStart = y0;
                if (badRun >= badPatience) {
                    return firstBadStart;
                }
            }
            continue;
        }

        Grid2D occ(nx, nz);
        for (size_t i : idx) {
            int gx = std::clamp(static_cast<int>(std::round((xs[i] - xLo) / gridMm)), 0, nx-1);
            int gz = std::clamp(static_cast<int>(std::round((zs[i] - zLo) / gridMm)), 0, nz-1);
            occ.data[(size_t)gx*nz + gz] = 1;
        }
        morph::closing2d(occ, 1);
        morph::fill_holes_2d(occ);
        int nComp = 0;
        auto lbl = morph::label2d(occ, nComp);

        bool isGood = false;
        for (int c = 1; c <= nComp && !isGood; ++c) {
            int area = 0;
            bool touchMid = false;
            for (int gx = 0; gx < nx; ++gx) {
                for (int gz = 0; gz < nz; ++gz) {
                    if (lbl[(size_t)gx*nz + gz] == c) {
                        ++area;
                        for (int cm : centerCols) if (gx == cm) { touchMid = true; break; }
                    }
                }
            }
            if (area >= 8 && touchMid) isGood = true;
        }

        if (isGood) {
            started = true;
            badRun = 0;
            lastGoodEnd = y1;
            firstBadStart = -1e30;
        } else if (started) {
            ++badRun;
            if (firstBadStart < -1e29) firstBadStart = y0;
            if (badRun >= badPatience) {
                return firstBadStart;
            }
        }
    }

    double fallback = percentile(ycp, 0.6);
    return std::min(lastGoodEnd, fallback);
}

// ---- Body extraction (per-slab XZ central component + 3D largest CC) -------

std::vector<std::array<int,3>> split_body_xz(const std::vector<std::array<int,3>>& coordsIjk,
                                              const std::vector<Vec3>& localPoints,
                                              double bodyYCut, double yBinMm, double gridMm,
                                              double centerHalfWidthMm, int minComponentArea,
                                              const std::array<int, 3>& volumeDims)
{
    if (coordsIjk.empty()) return {};

    std::vector<size_t> allowedIdx;
    for (size_t i = 0; i < localPoints.size(); ++i) if (localPoints[i][1] <= bodyYCut) allowedIdx.push_back(i);
    if (allowedIdx.empty()) return coordsIjk;

    // anterior seed for x_mid
    std::vector<double> ys; ys.reserve(allowedIdx.size());
    for (size_t i : allowedIdx) ys.push_back(localPoints[i][1]);
    std::vector<double> ysCpy = ys;
    double yAnt = percentile(ysCpy, 0.35);
    std::vector<double> xAnt;
    for (size_t i = 0; i < localPoints.size(); ++i)
        if (localPoints[i][1] <= yAnt) xAnt.push_back(localPoints[i][0]);
    if (xAnt.empty()) {
        for (size_t i : allowedIdx) xAnt.push_back(localPoints[i][0]);
    }
    double xMid = xAnt.empty() ? 0.0 : median(xAnt);

    double xLo = 1e30, zLo = 1e30, xHi = -1e30, zHi = -1e30, yMin = 1e30;
    for (size_t i : allowedIdx) {
        const Vec3& p = localPoints[i];
        if (p[0] < xLo) xLo = p[0]; if (p[0] > xHi) xHi = p[0];
        if (p[2] < zLo) zLo = p[2]; if (p[2] > zHi) zHi = p[2];
        if (p[1] < yMin) yMin = p[1];
    }
    xLo -= gridMm; zLo -= gridMm;
    int nx = static_cast<int>(std::ceil((xHi - xLo) / gridMm)) + 3;
    int nz = static_cast<int>(std::ceil((zHi - zLo) / gridMm)) + 3;
    if (nx < 3) nx = 3; if (nz < 3) nz = 3;

    std::vector<int> centerCols;
    for (int c = 0; c < nx; ++c) {
        double xc = xLo + c * gridMm;
        if (std::abs(xc - xMid) <= centerHalfWidthMm) centerCols.push_back(c);
    }
    if (centerCols.empty()) {
        int c = std::clamp(static_cast<int>(std::round((xMid - xLo) / gridMm)), 0, nx-1);
        centerCols.push_back(c);
    }

    std::vector<uint8_t> keep(coordsIjk.size(), 0);

    int nEdges = static_cast<int>(std::ceil((bodyYCut - yMin) / yBinMm)) + 1;
    for (int e = 0; e < nEdges - 1; ++e) {
        double y0 = yMin + e * yBinMm;
        double y1 = std::min(y0 + yBinMm, bodyYCut + 1e-6);

        std::vector<size_t> ids;
        for (size_t i = 0; i < localPoints.size(); ++i) {
            double y = localPoints[i][1];
            if (y >= y0 && y < y1 && y <= bodyYCut) ids.push_back(i);
        }
        if (ids.empty()) continue;

        Grid2D occ(nx, nz);
        std::vector<int> gxArr(ids.size()), gzArr(ids.size());
        for (size_t k = 0; k < ids.size(); ++k) {
            const Vec3& p = localPoints[ids[k]];
            gxArr[k] = std::clamp(static_cast<int>(std::round((p[0] - xLo) / gridMm)), 0, nx-1);
            gzArr[k] = std::clamp(static_cast<int>(std::round((p[2] - zLo) / gridMm)), 0, nz-1);
            occ.data[(size_t)gxArr[k]*nz + gzArr[k]] = 1;
        }
        morph::closing2d(occ, 1);
        morph::fill_holes_2d(occ);
        int nComp = 0;
        auto lbl = morph::label2d(occ, nComp);

        std::vector<int> compSize(nComp + 1, 0);
        std::vector<uint8_t> compTouchesMid(nComp + 1, 0);
        for (int gx = 0; gx < nx; ++gx) {
            for (int gz = 0; gz < nz; ++gz) {
                int c = lbl[(size_t)gx*nz + gz];
                if (c == 0) continue;
                ++compSize[c];
            }
        }
        for (int cm : centerCols) {
            for (int gz = 0; gz < nz; ++gz) {
                int c = lbl[(size_t)cm*nz + gz];
                if (c) compTouchesMid[c] = 1;
            }
        }
        std::vector<uint8_t> keepComp(nComp + 1, 0);
        for (int c = 1; c <= nComp; ++c) {
            if (compSize[c] >= minComponentArea && compTouchesMid[c]) keepComp[c] = 1;
        }
        bool any = false;
        for (int c = 1; c <= nComp; ++c) if (keepComp[c]) { any = true; break; }
        if (!any) {
            // Fallback: keep largest
            int bestC = 1, bestN = 0;
            for (int c = 1; c <= nComp; ++c) if (compSize[c] > bestN) { bestN = compSize[c]; bestC = c; }
            keepComp[bestC] = 1;
        }
        for (size_t k = 0; k < ids.size(); ++k) {
            int c = lbl[(size_t)gxArr[k]*nz + gzArr[k]];
            if (c >= 1 && keepComp[c]) keep[ids[k]] = 1;
        }
    }

    std::vector<std::array<int,3>> filtered;
    filtered.reserve(coordsIjk.size() / 2);
    for (size_t i = 0; i < coordsIjk.size(); ++i) if (keep[i]) filtered.push_back(coordsIjk[i]);
    if (filtered.empty()) {
        // fall back to allowed
        for (size_t i : allowedIdx) filtered.push_back(coordsIjk[i]);
        return filtered;
    }

    // Largest 3D connected component on filtered.
    int loI = filtered[0][0], hiI = loI, loJ = filtered[0][1], hiJ = loJ, loK = filtered[0][2], hiK = loK;
    for (const auto& p : filtered) {
        loI = std::min(loI, p[0]); hiI = std::max(hiI, p[0]);
        loJ = std::min(loJ, p[1]); hiJ = std::max(hiJ, p[1]);
        loK = std::min(loK, p[2]); hiK = std::max(hiK, p[2]);
    }
    loI = std::max(0, loI - 2); loJ = std::max(0, loJ - 2); loK = std::max(0, loK - 2);
    hiI = std::min(volumeDims[0], hiI + 3);
    hiJ = std::min(volumeDims[1], hiJ + 3);
    hiK = std::min(volumeDims[2], hiK + 3);
    int nI = hiI - loI, nJ = hiJ - loJ, nK = hiK - loK;
    Grid3D vol(nI, nJ, nK);
    for (const auto& p : filtered) {
        vol.data[vol.idx(p[0]-loI, p[1]-loJ, p[2]-loK)] = 1;
    }
    int n3 = 0;
    auto lbl3 = morph::label3d(vol, n3);
    if (n3 > 1) {
        std::vector<int> sizes(n3 + 1, 0);
        for (size_t k = 0; k < lbl3.size(); ++k) if (lbl3[k]) ++sizes[lbl3[k]];
        int best = 1, bestN = 0;
        for (int c = 1; c <= n3; ++c) if (sizes[c] > bestN) { bestN = sizes[c]; best = c; }
        std::vector<std::array<int,3>> out;
        for (int x = 0; x < nI; ++x)
            for (int y = 0; y < nJ; ++y)
                for (int z = 0; z < nK; ++z)
                    if (lbl3[vol.idx(x,y,z)] == best)
                        out.push_back({x+loI, y+loJ, z+loK});
        return out;
    }
    return filtered;
}

// ---- Marching cubes on a binary voxel set ---------------------------------

vtkSmartPointer<vtkPolyData> coords_to_mesh(const std::vector<std::array<int,3>>& coords,
                                            const Vec3& origin,
                                            const std::array<Vec3, 3>& sd,
                                            const std::array<int, 3>& volDims,
                                            int pad = 2)
{
    auto out = vtkSmartPointer<vtkPolyData>::New();
    if (coords.empty()) return out;
    int loI = coords[0][0], hiI = loI, loJ = coords[0][1], hiJ = loJ, loK = coords[0][2], hiK = loK;
    for (const auto& p : coords) {
        loI = std::min(loI, p[0]); hiI = std::max(hiI, p[0]);
        loJ = std::min(loJ, p[1]); hiJ = std::max(hiJ, p[1]);
        loK = std::min(loK, p[2]); hiK = std::max(hiK, p[2]);
    }
    loI = std::max(0, loI - pad); loJ = std::max(0, loJ - pad); loK = std::max(0, loK - pad);
    hiI = std::min(volDims[0], hiI + pad + 1);
    hiJ = std::min(volDims[1], hiJ + pad + 1);
    hiK = std::min(volDims[2], hiK + pad + 1);
    int nI = hiI - loI, nJ = hiJ - loJ, nK = hiK - loK;

    auto img = vtkSmartPointer<vtkImageData>::New();
    img->SetDimensions(nI, nJ, nK);
    img->SetSpacing(1.0, 1.0, 1.0);
    img->SetOrigin(0.0, 0.0, 0.0);
    img->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
    auto* arr = img->GetPointData()->GetScalars();
    std::memset(arr->GetVoidPointer(0), 0, static_cast<size_t>(nI)*nJ*nK);
    for (const auto& p : coords) {
        int i = p[0] - loI, j = p[1] - loJ, k = p[2] - loK;
        vtkIdType idx = i + (j + k * nJ) * nI;
        static_cast<unsigned char*>(arr->GetVoidPointer(0))[idx] = 1;
    }

    auto mc = vtkSmartPointer<vtkFlyingEdges3D>::New();
    mc->SetInputData(img);
    mc->SetValue(0, 0.5);
    mc->ComputeNormalsOff();
    mc->Update();

    auto raw = mc->GetOutput();

    // Transform points from cropped voxel space → physical mm.
    auto pts = vtkSmartPointer<vtkPoints>::New();
    pts->SetNumberOfPoints(raw->GetNumberOfPoints());
    for (vtkIdType i = 0; i < raw->GetNumberOfPoints(); ++i) {
        double q[3]; raw->GetPoint(i, q);
        // q is in cropped voxel coords; convert to original index
        double oi = q[0] + loI;
        double oj = q[1] + loJ;
        double ok = q[2] + loK;
        Vec3 p = {
            origin[0] + oi * sd[0][0] + oj * sd[1][0] + ok * sd[2][0],
            origin[1] + oi * sd[0][1] + oj * sd[1][1] + ok * sd[2][1],
            origin[2] + oi * sd[0][2] + oj * sd[1][2] + ok * sd[2][2],
        };
        pts->SetPoint(i, p[0], p[1], p[2]);
    }
    out->SetPoints(pts);
    out->SetPolys(raw->GetPolys());

    // If sd has negative determinant (mirrored space), flip triangle winding.
    Vec3 a = sd[0], b = sd[1], c = sd[2];
    Vec3 cr = cross(a, b);
    double det = dot(cr, c);
    if (det < 0) {
        auto polys = vtkSmartPointer<vtkCellArray>::New();
        out->GetPolys()->InitTraversal();
        vtkIdType npts; const vtkIdType* idsP;
        while (out->GetPolys()->GetNextCell(npts, idsP)) {
            vtkIdType swapped[3] = { idsP[0], idsP[2], idsP[1] };
            polys->InsertNextCell(3, swapped);
        }
        out->SetPolys(polys);
    }

    auto cleaner = vtkSmartPointer<vtkCleanPolyData>::New();
    cleaner->SetInputData(out);
    cleaner->Update();
    out->ShallowCopy(cleaner->GetOutput());

    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputData(out);
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->Update();
    out->ShallowCopy(normals->GetOutput());
    return out;
}

// ---- Per-triangle geometry in LCS ------------------------------------------

void face_geometry(vtkPolyData* mesh, const LCS& sys,
                   std::vector<int>& triangles,
                   std::vector<Vec3>& localCenters,
                   std::vector<Vec3>& localNormals)
{
    triangles.clear(); localCenters.clear(); localNormals.clear();
    if (!mesh) return;
    auto polys = mesh->GetPolys();
    if (!polys) return;
    polys->InitTraversal();
    vtkIdType npts; const vtkIdType* ids;
    while (polys->GetNextCell(npts, ids)) {
        if (npts != 3) continue;
        triangles.push_back(static_cast<int>(ids[0]));
        triangles.push_back(static_cast<int>(ids[1]));
        triangles.push_back(static_cast<int>(ids[2]));

        double p0[3], p1[3], p2[3];
        mesh->GetPoint(ids[0], p0);
        mesh->GetPoint(ids[1], p1);
        mesh->GetPoint(ids[2], p2);
        Vec3 c = { (p0[0]+p1[0]+p2[0])/3.0, (p0[1]+p1[1]+p2[1])/3.0, (p0[2]+p1[2]+p2[2])/3.0 };
        Vec3 e1 = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        Vec3 e2 = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        Vec3 n = cross(e1, e2);
        double nn = norm(n);
        if (nn < 1e-12) n = {0,0,1}; else n = n * (1.0 / nn);

        Vec3 d = c - sys.centroid;
        Vec3 lc = { dot(d, sys.axisX), dot(d, sys.axisY), dot(d, sys.axisZ) };
        Vec3 ln = { dot(n, sys.axisX), dot(n, sys.axisY), dot(n, sys.axisZ) };
        localCenters.push_back(lc);
        localNormals.push_back(ln);
    }
}

// ---- Triangle-edge connectivity --------------------------------------------

std::vector<std::vector<int>> triangle_neighbors(const std::vector<int>& triangles) {
    int nTri = static_cast<int>(triangles.size() / 3);
    std::unordered_map<long long, std::vector<int>> edgeMap;
    edgeMap.reserve(nTri * 3);
    auto edgeKey = [](int a, int b) -> long long {
        if (a > b) std::swap(a, b);
        return (static_cast<long long>(a) << 32) | static_cast<unsigned long long>(b);
    };
    for (int t = 0; t < nTri; ++t) {
        int v0 = triangles[3*t], v1 = triangles[3*t+1], v2 = triangles[3*t+2];
        edgeMap[edgeKey(v0, v1)].push_back(t);
        edgeMap[edgeKey(v1, v2)].push_back(t);
        edgeMap[edgeKey(v2, v0)].push_back(t);
    }
    std::vector<std::vector<int>> adj(nTri);
    for (auto& kv : edgeMap) {
        auto& cells = kv.second;
        for (size_t a = 0; a < cells.size(); ++a) {
            for (size_t b = 0; b < cells.size(); ++b) {
                if (a == b) continue;
                adj[cells[a]].push_back(cells[b]);
            }
        }
    }
    return adj;
}

std::vector<uint8_t> largest_cc_mask(const std::vector<std::vector<int>>& adj,
                                     const std::vector<uint8_t>& mask) {
    int n = static_cast<int>(mask.size());
    std::vector<uint8_t> visited(n, 0);
    std::vector<int> bestComp;
    for (int s = 0; s < n; ++s) {
        if (!mask[s] || visited[s]) continue;
        std::vector<int> comp;
        std::queue<int> q;
        q.push(s); visited[s] = 1;
        while (!q.empty()) {
            int c = q.front(); q.pop();
            comp.push_back(c);
            for (int nb : adj[c]) {
                if (mask[nb] && !visited[nb]) { visited[nb] = 1; q.push(nb); }
            }
        }
        if (comp.size() > bestComp.size()) bestComp = std::move(comp);
    }
    std::vector<uint8_t> out(n, 0);
    for (int t : bestComp) out[t] = 1;
    return out;
}

std::vector<uint8_t> grow_from_seed(const std::vector<std::vector<int>>& adj,
                                    const std::vector<uint8_t>& seed,
                                    const std::vector<uint8_t>& allowed) {
    int n = static_cast<int>(seed.size());
    std::vector<uint8_t> out(n, 0);
    std::queue<int> q;
    for (int t = 0; t < n; ++t) if (seed[t] && allowed[t]) { out[t] = 1; q.push(t); }
    while (!q.empty()) {
        int c = q.front(); q.pop();
        for (int nb : adj[c]) {
            if (allowed[nb] && !out[nb]) { out[nb] = 1; q.push(nb); }
        }
    }
    return out;
}

// ---- Lateral X continuity prune --------------------------------------------

std::vector<uint8_t> prune_x_continuity(const std::vector<uint8_t>& mask,
                                        const std::vector<Vec3>& localCenters,
                                        double xBinMm, int maxGapBins) {
    int n = static_cast<int>(mask.size());
    std::vector<int> ids;
    for (int t = 0; t < n; ++t) if (mask[t]) ids.push_back(t);
    if ((int)ids.size() < 20) return mask;

    double xMin = 1e30, xMax = -1e30;
    for (int t : ids) { double x = localCenters[t][0]; xMin = std::min(xMin, x); xMax = std::max(xMax, x); }
    int nBins = static_cast<int>(std::ceil((xMax - xMin) / xBinMm)) + 1;
    if (nBins < 5) return mask;
    std::vector<int> hist(nBins, 0);
    for (int t : ids) {
        int b = std::clamp(static_cast<int>((localCenters[t][0] - xMin) / xBinMm), 0, nBins - 1);
        ++hist[b];
    }
    std::vector<uint8_t> occ(nBins, 0);
    for (int b = 0; b < nBins; ++b) occ[b] = hist[b] > 0;

    // Connected runs (1D).
    std::vector<int> runId(nBins, 0);
    int nRuns = 0;
    for (int b = 0; b < nBins; ++b) {
        if (!occ[b]) continue;
        if (b == 0 || !occ[b-1]) ++nRuns;
        runId[b] = nRuns;
    }
    if (nRuns <= 1) return mask;

    std::vector<int> runSize(nRuns + 1, 0);
    for (int b = 0; b < nBins; ++b) if (runId[b]) ++runSize[runId[b]];
    int best = 1, bestN = 0;
    for (int r = 1; r <= nRuns; ++r) if (runSize[r] > bestN) { bestN = runSize[r]; best = r; }

    std::vector<uint8_t> keepBin(nBins, 0);
    for (int b = 0; b < nBins; ++b) if (runId[b] == best) keepBin[b] = 1;
    // closing by maxGapBins
    for (int g = 0; g < maxGapBins; ++g) {
        std::vector<uint8_t> tmp = keepBin;
        for (int b = 1; b < nBins-1; ++b)
            if (!keepBin[b] && keepBin[b-1] && keepBin[b+1]) tmp[b] = 1;
        keepBin.swap(tmp);
    }
    std::vector<uint8_t> out(n, 0);
    for (int t : ids) {
        int b = std::clamp(static_cast<int>((localCenters[t][0] - xMin) / xBinMm), 0, nBins - 1);
        if (keepBin[b]) out[t] = 1;
    }
    return out;
}

// ---- Hole fill on local-XY rasterized footprint ----------------------------

void fill_internal_holes(std::vector<uint8_t>& mask,
                         const std::vector<uint8_t>& candidate,
                         const std::vector<Vec3>& localCenters,
                         double gridMm,
                         int& addedOut)
{
    addedOut = 0;
    int n = static_cast<int>(mask.size());
    std::vector<int> sel;
    for (int t = 0; t < n; ++t) if (mask[t]) sel.push_back(t);
    if ((int)sel.size() < 20) return;

    double xMin = 1e30, yMin = 1e30, xMax = -1e30, yMax = -1e30;
    for (int t : sel) {
        xMin = std::min(xMin, localCenters[t][0]); xMax = std::max(xMax, localCenters[t][0]);
        yMin = std::min(yMin, localCenters[t][1]); yMax = std::max(yMax, localCenters[t][1]);
    }
    xMin -= gridMm; yMin -= gridMm; xMax += gridMm; yMax += gridMm;
    int nx = static_cast<int>(std::ceil((xMax - xMin) / gridMm)) + 1;
    int ny = static_cast<int>(std::ceil((yMax - yMin) / gridMm)) + 1;
    if (nx < 3 || ny < 3 || nx > 1000 || ny > 1000) return;

    Grid2D occ(nx, ny);
    auto toGrid = [&](double x, double y, int& gx, int& gy) {
        gx = std::clamp(static_cast<int>(std::round((x - xMin) / gridMm)), 0, nx - 1);
        gy = std::clamp(static_cast<int>(std::round((y - yMin) / gridMm)), 0, ny - 1);
    };
    for (int t : sel) {
        int gx, gy; toGrid(localCenters[t][0], localCenters[t][1], gx, gy);
        occ.data[(size_t)gx*ny + gy] = 1;
    }
    Grid2D filled = occ;
    morph::fill_holes_2d(filled);
    Grid2D holes(nx, ny);
    for (size_t i = 0; i < occ.data.size(); ++i)
        holes.data[i] = (filled.data[i] && !occ.data[i]) ? 1 : 0;
    bool anyHole = false;
    for (auto v : holes.data) if (v) { anyHole = true; break; }
    if (!anyHole) return;

    std::vector<int> cand;
    for (int t = 0; t < n; ++t) if (candidate[t] && !mask[t]) cand.push_back(t);
    int added = 0;
    for (int t : cand) {
        double x = localCenters[t][0], y = localCenters[t][1];
        if (x < xMin || x > xMax || y < yMin || y > yMax) continue;
        int gx, gy; toGrid(x, y, gx, gy);
        if (holes.data[(size_t)gx*ny + gy]) {
            mask[t] = 1; ++added;
        }
    }
    addedOut = added;
}

}  // anonymous namespace

// ---- Public Step 2 entry ---------------------------------------------------

std::vector<EndplateResult> detectEndplates(vtkImageData* labelMap,
                                            const Vec3& origin,
                                            const std::array<Vec3, 3>& sd,
                                            const std::vector<LCS>& systems,
                                            const Params& params)
{
    int dims[3]; labelMap->GetDimensions(dims);
    std::array<int,3> volDims = {dims[0], dims[1], dims[2]};
    auto* arr = labelMap->GetPointData()->GetScalars();

    // Group voxel coords per label in one pass.
    std::map<int, std::vector<std::array<int,3>>> coordsByLabel;
    for (const auto& s : systems) coordsByLabel[s.label].reserve(2048);
    vtkIdType idx = 0;
    for (int k = 0; k < dims[2]; ++k) {
        for (int j = 0; j < dims[1]; ++j) {
            for (int i = 0; i < dims[0]; ++i, ++idx) {
                int v = static_cast<int>(arr->GetTuple1(idx));
                auto it = coordsByLabel.find(v);
                if (it == coordsByLabel.end()) continue;
                it->second.push_back({i, j, k});
            }
        }
    }

    std::vector<EndplateResult> out;
    out.reserve(systems.size());

    for (const auto& sys : systems) {
        auto& coords = coordsByLabel[sys.label];
        if (coords.empty()) continue;

        // Local coords of all label voxels
        std::vector<Vec3> localPts; localPts.reserve(coords.size());
        for (const auto& c : coords) {
            Vec3 p = ijk_to_phys(c[0], c[1], c[2], origin, sd);
            localPts.push_back(to_local(p, sys));
        }

        // Step 2a: body Y-cut
        double yCut = find_body_y_cut(localPts,
            params.bodyYBinMm, params.bodyGridMm, params.bodyCenterWidthMm,
            params.bodyMinSlicePoints, params.bodyBadPatience);

        // Step 2b: split body voxels
        auto bodyCoords = split_body_xz(coords, localPts, yCut,
            params.bodyYBinMm, params.bodyGridMm, params.bodyCenterWidthMm,
            params.bodyMinComponentArea, volDims);

        // Marching cubes
        auto bodyMesh = coords_to_mesh(bodyCoords, origin, sd, volDims);
        auto fullMesh = coords_to_mesh(coords,     origin, sd, volDims);

        // Triangle geometry in LCS
        std::vector<int> triangles;
        std::vector<Vec3> localCenters, localNormals;
        face_geometry(bodyMesh, sys, triangles, localCenters, localNormals);

        int nTri = static_cast<int>(triangles.size() / 3);
        std::vector<uint8_t> top(nTri, 0), bottom(nTri, 0);
        if (nTri == 0) {
            EndplateResult r;
            r.label = sys.label; r.system = sys; r.bodyMesh = bodyMesh; r.fullMesh = fullMesh;
            r.triangles = triangles;
            r.localCenters = localCenters; r.localNormals = localNormals;
            r.topMask = top; r.bottomMask = bottom;
            out.push_back(r);
            continue;
        }

        // Anterior seed cut + grow Y
        std::vector<double> ys; ys.reserve(nTri);
        for (const auto& c : localCenters) ys.push_back(c[1]);
        double ySeedCut = percentile(ys, params.anteriorQuantile);
        double yGrowCut = percentile(ys, params.growYQuantile);
        double yHardCut = std::min(yGrowCut, ySeedCut + params.posteriorMarginMm);

        // Z thresholds from anterior region
        std::vector<double> zsAnt;
        for (int t = 0; t < nTri; ++t) if (localCenters[t][1] <= ySeedCut) zsAnt.push_back(localCenters[t][2]);
        double zTopCut = zsAnt.empty() ? 0.0 : percentile(zsAnt, params.topQuantile);
        double zBottomCut = zsAnt.empty() ? 0.0 : percentile(zsAnt, params.bottomQuantile);

        double normalCut    = std::cos(params.maxNormalAngleDeg  * COBB_PI / 180.0);
        double growNormalCut = std::cos(params.growNormalAngleDeg * COBB_PI / 180.0);

        std::vector<uint8_t> antSeed(nTri, 0), antGrow(nTri, 0);
        for (int t = 0; t < nTri; ++t) {
            antSeed[t] = (localCenters[t][1] <= ySeedCut) ? 1 : 0;
            antGrow[t] = (localCenters[t][1] <= yHardCut) ? 1 : 0;
        }

        std::vector<uint8_t> topRaw(nTri, 0), bottomRaw(nTri, 0);
        for (int t = 0; t < nTri; ++t) {
            if (antSeed[t]) {
                if (localNormals[t][2] >= normalCut && localCenters[t][2] >= zTopCut)
                    topRaw[t] = 1;
                if (localNormals[t][2] <= -normalCut && localCenters[t][2] <= zBottomCut)
                    bottomRaw[t] = 1;
            }
        }
        // Fallback if directional empty
        auto sumU = [](const std::vector<uint8_t>& v) {
            int s = 0; for (auto x : v) s += x; return s;
        };
        if (sumU(topRaw) == 0) {
            for (int t = 0; t < nTri; ++t) if (antSeed[t] && std::abs(localNormals[t][2]) >= normalCut && localCenters[t][2] >= zTopCut) topRaw[t] = 1;
        }
        if (sumU(bottomRaw) == 0) {
            for (int t = 0; t < nTri; ++t) if (antSeed[t] && std::abs(localNormals[t][2]) >= normalCut && localCenters[t][2] <= zBottomCut) bottomRaw[t] = 1;
        }

        auto adj = triangle_neighbors(triangles);
        auto topSeed = largest_cc_mask(adj, topRaw);
        auto bottomSeed = largest_cc_mask(adj, bottomRaw);

        // Seed Z medians
        std::vector<double> zsTop, zsBot;
        for (int t = 0; t < nTri; ++t) {
            if (topSeed[t]) zsTop.push_back(localCenters[t][2]);
            if (bottomSeed[t]) zsBot.push_back(localCenters[t][2]);
        }
        double topSeedZ = zsTop.empty() ? zTopCut : median(zsTop);
        double bottomSeedZ = zsBot.empty() ? zBottomCut : median(zsBot);

        // Allowed regions for grow
        std::vector<uint8_t> topAllowed(nTri, 0), bottomAllowed(nTri, 0);
        for (int t = 0; t < nTri; ++t) {
            double z = localCenters[t][2];
            double nz = localNormals[t][2];
            if (antGrow[t] && z >= topSeedZ - params.growZMarginMm && z <= topSeedZ + params.growZMarginMm
                && nz >= growNormalCut) topAllowed[t] = 1;
            if (antGrow[t] && z >= bottomSeedZ - params.growZMarginMm && z <= bottomSeedZ + params.growZMarginMm
                && nz <= -growNormalCut) bottomAllowed[t] = 1;
        }

        auto topGrown = grow_from_seed(adj, topSeed, topAllowed);
        auto botGrown = grow_from_seed(adj, bottomSeed, bottomAllowed);

        top = (sumU(topGrown) > sumU(topSeed)) ? topGrown : topSeed;
        bottom = (sumU(botGrown) > sumU(bottomSeed)) ? botGrown : bottomSeed;
        for (int t = 0; t < nTri; ++t) {
            if (top[t] && localNormals[t][2] < growNormalCut) top[t] = 0;
            if (bottom[t] && localNormals[t][2] > -growNormalCut) bottom[t] = 0;
        }

        top = prune_x_continuity(top, localCenters, 2.0, 1);
        bottom = prune_x_continuity(bottom, localCenters, 2.0, 1);

        if (params.fillHoles) {
            int addedT = 0, addedB = 0;
            fill_internal_holes(top, topAllowed, localCenters, params.fillHoleGridMm, addedT);
            fill_internal_holes(bottom, bottomAllowed, localCenters, params.fillHoleGridMm, addedB);
        }

        EndplateResult r;
        r.label = sys.label;
        r.system = sys;
        r.bodyMesh = bodyMesh;
        r.fullMesh = fullMesh;
        r.triangles = std::move(triangles);
        r.localCenters = std::move(localCenters);
        r.localNormals = std::move(localNormals);
        r.topMask = std::move(top);
        r.bottomMask = std::move(bottom);
        out.push_back(std::move(r));

        std::cerr << "[cobb] " << sys.name << " (label=" << sys.label
                  << "): body=" << bodyCoords.size() << "/" << coords.size() << " vox, "
                  << "tri=" << nTri
                  << ", top=" << sumU(out.back().topMask)
                  << ", bottom=" << sumU(out.back().bottomMask)
                  << ", yCut=" << yCut << "\n";
    }
    return out;
}

}  // namespace cobb
