// Morphology.h — minimal binary morphology helpers used by the Cobb pipeline.
// All grids are zero-padded as needed inside the routines; callers pass tightly
// bounded regions to keep memory small.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace cobb::morph {

// 2D row-major bool grid (mask), shape [nx][ny] flattened to nx*ny bytes.
struct Grid2D {
    int nx = 0, ny = 0;
    std::vector<uint8_t> data;  // 0 / 1
    bool& at(int x, int y);
    bool  at(int x, int y) const;
    Grid2D() = default;
    Grid2D(int nx, int ny) : nx(nx), ny(ny), data(static_cast<size_t>(nx)*ny, 0) {}
};

// 3D row-major bool grid, shape [nx][ny][nz].
struct Grid3D {
    int nx = 0, ny = 0, nz = 0;
    std::vector<uint8_t> data;
    Grid3D() = default;
    Grid3D(int nx, int ny, int nz) : nx(nx), ny(ny), nz(nz),
        data(static_cast<size_t>(nx)*ny*nz, 0) {}
    inline size_t idx(int x, int y, int z) const {
        return ((size_t)x*ny + y)*nz + z;
    }
};

// 2D dilation by a 3x3 square structuring element, n_iters times (in place).
void dilate2d(Grid2D& g, int iters);
// 2D erosion (3x3 SE) — boundary pixels treated as 0.
void erode2d(Grid2D& g, int iters);
// 2D binary closing = dilate + erode.
void closing2d(Grid2D& g, int iters);
// 2D binary opening = erode + dilate.
void opening2d(Grid2D& g, int iters);

// 2D 4-connected connected components. Returns label image and component count.
// Background = 0; returns labels 1..N.
std::vector<int> label2d(const Grid2D& g, int& nComponents);

// 2D binary fill holes: flood-fill background from boundary; non-flooded
// background pixels are treated as holes and filled.
void fill_holes_2d(Grid2D& g);

// 3D 6-connected components. Returns flat label volume (same size as g.data) and N.
std::vector<int> label3d(const Grid3D& g, int& nComponents);

// 3D binary propagation: grow `seed` (subset of `mask`) until it fills the
// connected component of `mask` reachable from any seed voxel.
// Result returned as a new Grid3D the same shape as `mask`.
Grid3D propagate3d(const Grid3D& mask, const Grid3D& seed);

}  // namespace cobb::morph
