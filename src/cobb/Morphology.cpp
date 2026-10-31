#include "Morphology.h"

#include <algorithm>
#include <queue>

namespace cobb::morph {

bool& Grid2D::at(int x, int y)        { return reinterpret_cast<bool&>(data[(size_t)x*ny + y]); }
bool  Grid2D::at(int x, int y) const  { return data[(size_t)x*ny + y] != 0; }

void dilate2d(Grid2D& g, int iters) {
    if (iters <= 0) return;
    std::vector<uint8_t> tmp(g.data.size(), 0);
    for (int it = 0; it < iters; ++it) {
        std::fill(tmp.begin(), tmp.end(), 0);
        for (int x = 0; x < g.nx; ++x) {
            for (int y = 0; y < g.ny; ++y) {
                if (!g.at(x, y)) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xn = x + dx;
                    if (xn < 0 || xn >= g.nx) continue;
                    for (int dy = -1; dy <= 1; ++dy) {
                        int yn = y + dy;
                        if (yn < 0 || yn >= g.ny) continue;
                        tmp[(size_t)xn*g.ny + yn] = 1;
                    }
                }
            }
        }
        g.data.swap(tmp);
    }
}

void erode2d(Grid2D& g, int iters) {
    if (iters <= 0) return;
    std::vector<uint8_t> tmp(g.data.size(), 0);
    for (int it = 0; it < iters; ++it) {
        std::fill(tmp.begin(), tmp.end(), 0);
        for (int x = 0; x < g.nx; ++x) {
            for (int y = 0; y < g.ny; ++y) {
                bool keep = true;
                for (int dx = -1; dx <= 1 && keep; ++dx) {
                    int xn = x + dx;
                    if (xn < 0 || xn >= g.nx) { keep = false; break; }
                    for (int dy = -1; dy <= 1; ++dy) {
                        int yn = y + dy;
                        if (yn < 0 || yn >= g.ny || !g.at(xn, yn)) { keep = false; break; }
                    }
                }
                tmp[(size_t)x*g.ny + y] = keep ? 1 : 0;
            }
        }
        g.data.swap(tmp);
    }
}

void closing2d(Grid2D& g, int iters) { dilate2d(g, iters); erode2d(g, iters); }
void opening2d(Grid2D& g, int iters) { erode2d(g, iters); dilate2d(g, iters); }

std::vector<int> label2d(const Grid2D& g, int& nComponents) {
    std::vector<int> lab(g.data.size(), 0);
    int next = 0;
    static const int DX[4] = {-1, 1, 0, 0};
    static const int DY[4] = { 0, 0,-1, 1};
    for (int x = 0; x < g.nx; ++x) {
        for (int y = 0; y < g.ny; ++y) {
            size_t k = (size_t)x*g.ny + y;
            if (!g.data[k] || lab[k]) continue;
            ++next;
            std::queue<std::pair<int,int>> q;
            q.emplace(x, y); lab[k] = next;
            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                for (int d = 0; d < 4; ++d) {
                    int nx = cx + DX[d], ny = cy + DY[d];
                    if (nx < 0 || nx >= g.nx || ny < 0 || ny >= g.ny) continue;
                    size_t kk = (size_t)nx*g.ny + ny;
                    if (g.data[kk] && !lab[kk]) {
                        lab[kk] = next;
                        q.emplace(nx, ny);
                    }
                }
            }
        }
    }
    nComponents = next;
    return lab;
}

void fill_holes_2d(Grid2D& g) {
    // Flood-fill background from all boundary 0-pixels; whatever remains 0 in the
    // interior is a hole — flip it to 1.
    std::vector<uint8_t> visited(g.data.size(), 0);
    std::queue<std::pair<int,int>> q;
    auto pushBg = [&](int x, int y) {
        if (x < 0 || x >= g.nx || y < 0 || y >= g.ny) return;
        size_t k = (size_t)x*g.ny + y;
        if (visited[k] || g.data[k]) return;
        visited[k] = 1;
        q.emplace(x, y);
    };
    for (int x = 0; x < g.nx; ++x) { pushBg(x, 0); pushBg(x, g.ny-1); }
    for (int y = 0; y < g.ny; ++y) { pushBg(0, y); pushBg(g.nx-1, y); }

    static const int DX[4] = {-1, 1, 0, 0};
    static const int DY[4] = { 0, 0,-1, 1};
    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        for (int d = 0; d < 4; ++d) pushBg(cx + DX[d], cy + DY[d]);
    }
    for (size_t k = 0; k < g.data.size(); ++k) {
        if (!g.data[k] && !visited[k]) g.data[k] = 1;
    }
}

std::vector<int> label3d(const Grid3D& g, int& nComponents) {
    std::vector<int> lab(g.data.size(), 0);
    int next = 0;
    static const int DX[6] = {-1,1, 0,0, 0,0};
    static const int DY[6] = { 0,0,-1,1, 0,0};
    static const int DZ[6] = { 0,0, 0,0,-1,1};
    for (int x = 0; x < g.nx; ++x) {
        for (int y = 0; y < g.ny; ++y) {
            for (int z = 0; z < g.nz; ++z) {
                size_t k = g.idx(x,y,z);
                if (!g.data[k] || lab[k]) continue;
                ++next;
                std::queue<std::array<int,3>> q;
                q.push({x,y,z}); lab[k] = next;
                while (!q.empty()) {
                    auto p = q.front(); q.pop();
                    for (int d = 0; d < 6; ++d) {
                        int nx = p[0]+DX[d], ny = p[1]+DY[d], nz = p[2]+DZ[d];
                        if (nx<0||nx>=g.nx||ny<0||ny>=g.ny||nz<0||nz>=g.nz) continue;
                        size_t kk = g.idx(nx,ny,nz);
                        if (g.data[kk] && !lab[kk]) {
                            lab[kk] = next;
                            q.push({nx,ny,nz});
                        }
                    }
                }
            }
        }
    }
    nComponents = next;
    return lab;
}

Grid3D propagate3d(const Grid3D& mask, const Grid3D& seed) {
    Grid3D out(mask.nx, mask.ny, mask.nz);
    if (mask.nx != seed.nx || mask.ny != seed.ny || mask.nz != seed.nz) return out;

    std::queue<std::array<int,3>> q;
    for (int x = 0; x < mask.nx; ++x) {
        for (int y = 0; y < mask.ny; ++y) {
            for (int z = 0; z < mask.nz; ++z) {
                size_t k = mask.idx(x,y,z);
                if (seed.data[k] && mask.data[k]) {
                    out.data[k] = 1;
                    q.push({x,y,z});
                }
            }
        }
    }
    static const int DX[6] = {-1,1, 0,0, 0,0};
    static const int DY[6] = { 0,0,-1,1, 0,0};
    static const int DZ[6] = { 0,0, 0,0,-1,1};
    while (!q.empty()) {
        auto p = q.front(); q.pop();
        for (int d = 0; d < 6; ++d) {
            int nx = p[0]+DX[d], ny = p[1]+DY[d], nz = p[2]+DZ[d];
            if (nx<0||nx>=mask.nx||ny<0||ny>=mask.ny||nz<0||nz>=mask.nz) continue;
            size_t kk = mask.idx(nx,ny,nz);
            if (mask.data[kk] && !out.data[kk]) {
                out.data[kk] = 1;
                q.push({nx,ny,nz});
            }
        }
    }
    return out;
}

}  // namespace cobb::morph
