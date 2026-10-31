// Step1_LCS.cpp — per-vertebra local coordinate system from a label volume.
//
// Convention (mirrors the Python pipeline):
//   +Z = superior (oriented to point up along the chain centroid)
//   +Y = posterior
//   +X = lateral (cross(Y, Z))
#include "CobbAnalysis.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numeric>

#include <vtkImageData.h>
#include <vtkMath.h>
#include <vtkPointData.h>

namespace cobb {

namespace {
constexpr Vec3 GLOBAL_POSTERIOR = {0.0, 1.0, 0.0};
constexpr Vec3 GLOBAL_SUPERIOR  = {0.0, 0.0, 1.0};

// (i,j,k) → mm using origin + sum_d (coord_d * spaceDirections[d])
inline Vec3 ijk_to_phys(int i, int j, int k,
                        const Vec3& origin,
                        const std::array<Vec3, 3>& sd) {
    return {
        origin[0] + i * sd[0][0] + j * sd[1][0] + k * sd[2][0],
        origin[1] + i * sd[0][1] + j * sd[1][1] + k * sd[2][1],
        origin[2] + i * sd[0][2] + j * sd[1][2] + k * sd[2][2],
    };
}

// Symmetric 3x3 eigendecomposition via vtkMath::Jacobi.
// Eigenvalues sorted ascending; eigenvectors are columns of `vec`.
void eigh3(const double cov[3][3], double eval[3], double evec[3][3]) {
    double a[3][3];
    std::memcpy(a, cov, sizeof(a));
    double* aRows[3] = { a[0], a[1], a[2] };
    double w[3];
    double v[3][3];
    double* vRows[3] = { v[0], v[1], v[2] };
    vtkMath::Jacobi(aRows, w, vRows);  // returns eigvals descending, eigvecs as cols of v
    // Re-sort ascending so eigvec[:,0] = smallest variance direction (≈ Z hint fallback).
    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int a, int b) { return w[a] < w[b]; });
    for (int k = 0; k < 3; ++k) {
        eval[k] = w[order[k]];
        for (int r = 0; r < 3; ++r) evec[r][k] = v[r][order[k]];
    }
}
}

std::vector<LCS> computeLCS(vtkImageData* labelMap,
                            const Vec3& origin,
                            const std::array<Vec3, 3>& sd,
                            const std::vector<int>& labels)
{
    int dims[3];
    labelMap->GetDimensions(dims);
    auto* arr = labelMap->GetPointData()->GetScalars();
    if (!arr) {
        std::cerr << "[cobb] computeLCS: label volume has no scalar array\n";
        return {};
    }

    // Pass 1: collect physical points per label.
    std::map<int, std::vector<Vec3>> pointsByLabel;
    for (int label : labels) pointsByLabel[label].reserve(1024);

    // Iterate over voxels. NRRD/VTK uses (i,j,k) with i fastest.
    vtkIdType idx = 0;
    for (int k = 0; k < dims[2]; ++k) {
        for (int j = 0; j < dims[1]; ++j) {
            for (int i = 0; i < dims[0]; ++i, ++idx) {
                int v = static_cast<int>(arr->GetTuple1(idx));
                auto it = pointsByLabel.find(v);
                if (it == pointsByLabel.end()) continue;
                it->second.push_back(ijk_to_phys(i, j, k, origin, sd));
            }
        }
    }

    // Centroids first (used to build chain Z-axis hints).
    std::map<int, Vec3> centroidByLabel;
    for (int label : labels) {
        const auto& pts = pointsByLabel[label];
        if (pts.empty()) continue;
        Vec3 c{0,0,0};
        for (const auto& p : pts) { c[0]+=p[0]; c[1]+=p[1]; c[2]+=p[2]; }
        double n = static_cast<double>(pts.size());
        centroidByLabel[label] = {c[0]/n, c[1]/n, c[2]/n};
    }

    // Build chain Z-hints (oriented superior).
    std::map<int, Vec3> zHintByLabel;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (labels.size() < 2) break;
        int label = labels[i];
        const Vec3* a = nullptr;
        const Vec3* b = nullptr;
        if (i == 0) {
            a = &centroidByLabel[label];
            b = &centroidByLabel[labels[i+1]];
            // direction = self - next  (so that prev_centroid - self = up)
            Vec3 dir = *a - *b;
            Vec3 z = normalize(dir);
            if (dot(z, GLOBAL_SUPERIOR) < 0) z = z * -1.0;
            zHintByLabel[label] = z;
        } else if (i == labels.size() - 1) {
            a = &centroidByLabel[labels[i-1]];
            b = &centroidByLabel[label];
            Vec3 dir = *a - *b;
            Vec3 z = normalize(dir);
            if (dot(z, GLOBAL_SUPERIOR) < 0) z = z * -1.0;
            zHintByLabel[label] = z;
        } else {
            a = &centroidByLabel[labels[i-1]];
            b = &centroidByLabel[labels[i+1]];
            Vec3 dir = *a - *b;
            Vec3 z = normalize(dir);
            if (dot(z, GLOBAL_SUPERIOR) < 0) z = z * -1.0;
            zHintByLabel[label] = z;
        }
    }

    // Pass 2: build LCS for each vertebra.
    std::vector<LCS> systems;
    systems.reserve(labels.size());
    for (int label : labels) {
        auto& pts = pointsByLabel[label];
        if (pts.empty()) {
            std::cerr << "[cobb] WARNING: label " << label << " has no voxels\n";
            continue;
        }
        const Vec3& centroid = centroidByLabel[label];

        // Covariance.
        double cov[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
        for (const auto& p : pts) {
            double dx = p[0] - centroid[0];
            double dy = p[1] - centroid[1];
            double dz = p[2] - centroid[2];
            cov[0][0] += dx*dx;  cov[0][1] += dx*dy;  cov[0][2] += dx*dz;
            cov[1][1] += dy*dy;  cov[1][2] += dy*dz;
            cov[2][2] += dz*dz;
        }
        double n = static_cast<double>(pts.size());
        cov[0][0] /= n; cov[0][1] /= n; cov[0][2] /= n;
        cov[1][1] /= n; cov[1][2] /= n; cov[2][2] /= n;
        cov[1][0] = cov[0][1]; cov[2][0] = cov[0][2]; cov[2][1] = cov[1][2];

        double eval[3], evec[3][3];
        eigh3(cov, eval, evec);  // ascending eigenvalues

        Vec3 axisZ;
        auto zit = zHintByLabel.find(label);
        if (zit != zHintByLabel.end()) {
            axisZ = zit->second;
        } else {
            // Single-vertebra fallback: use eigenvector of smallest variance
            // (which for a flat vertebra is roughly the superior axis).
            axisZ = {evec[0][0], evec[1][0], evec[2][0]};
            if (dot(axisZ, GLOBAL_SUPERIOR) < 0) axisZ = axisZ * -1.0;
        }
        axisZ = normalize(axisZ);

        // Y axis: project global posterior onto plane perpendicular to Z.
        Vec3 axisY = GLOBAL_POSTERIOR;
        double s = dot(axisY, axisZ);
        axisY = {axisY[0] - s*axisZ[0], axisY[1] - s*axisZ[1], axisY[2] - s*axisZ[2]};
        if (norm(axisY) < 1e-8) {
            // Fallback: middle eigenvector
            axisY = {evec[0][1], evec[1][1], evec[2][1]};
        }
        axisY = normalize(axisY);
        if (dot(axisY, GLOBAL_POSTERIOR) < 0) axisY = axisY * -1.0;

        Vec3 axisX = normalize(cross(axisY, axisZ));
        axisY = normalize(cross(axisZ, axisX));

        // Compute extent along local axes (range in each axis).
        double minX = 1e30, maxX = -1e30;
        double minY = 1e30, maxY = -1e30;
        double minZ = 1e30, maxZ = -1e30;
        for (const auto& p : pts) {
            Vec3 d = p - centroid;
            double lx = dot(d, axisX);
            double ly = dot(d, axisY);
            double lz = dot(d, axisZ);
            if (lx < minX) minX = lx; if (lx > maxX) maxX = lx;
            if (ly < minY) minY = ly; if (ly > maxY) maxY = ly;
            if (lz < minZ) minZ = lz; if (lz > maxZ) maxZ = lz;
        }

        LCS sys;
        sys.label = label;
        sys.name  = label_name(label);
        sys.voxelCount = static_cast<int>(pts.size());
        sys.centroid = centroid;
        sys.axisX = axisX;
        sys.axisY = axisY;
        sys.axisZ = axisZ;
        sys.extentX = maxX - minX;
        sys.extentY = maxY - minY;
        sys.extentZ = maxZ - minZ;
        systems.push_back(sys);
    }

    return systems;
}

}  // namespace cobb
