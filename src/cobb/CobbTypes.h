// CobbTypes.h — common types for the Cobb pipeline.
#pragma once

#include <array>
#include <string>
#include <vector>

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>

namespace cobb {

using Vec3 = std::array<double, 3>;

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a[0]+b[0], a[1]+b[1], a[2]+b[2]}; }
inline Vec3 operator*(const Vec3& a, double s)      { return {a[0]*s, a[1]*s, a[2]*s}; }
inline double dot(const Vec3& a, const Vec3& b)     { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline Vec3   cross(const Vec3& a, const Vec3& b)   { return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; }
double        norm(const Vec3& v);
Vec3          normalize(const Vec3& v);

// Per-vertebra local coordinate system (Step 1).
struct LCS {
    int label = 0;
    std::string name;
    int voxelCount = 0;
    Vec3 centroid{0,0,0};
    Vec3 axisX{1,0,0};   // lateral
    Vec3 axisY{0,1,0};   // posterior (+Y is posterior)
    Vec3 axisZ{0,0,1};   // superior  (+Z is up)
    double extentX = 0, extentY = 0, extentZ = 0;
};

// Per-vertebra endplate detection result (Step 2).
struct EndplateResult {
    int label = 0;
    LCS system;
    vtkSmartPointer<vtkPolyData> bodyMesh;     // marching cubes on body voxels
    vtkSmartPointer<vtkPolyData> fullMesh;     // marching cubes on whole vertebra (for context render)
    std::vector<int> triangles;                // 3*nTri ints, indices into bodyMesh->GetPoints()
    std::vector<Vec3> localCenters;            // triangle center in LCS, size = nTri
    std::vector<Vec3> localNormals;            // triangle normal in LCS, size = nTri
    std::vector<unsigned char> topMask;        // size = nTri
    std::vector<unsigned char> bottomMask;     // size = nTri
};

// Per-vertebra fitted plane + tilts (Step 3 input).
struct EndplateMeasure {
    int label = 0;
    std::string name;
    Vec3 centroid{0,0,0};
    int topCells = 0, bottomCells = 0;
    Vec3 topCenter{0,0,0}, bottomCenter{0,0,0};
    Vec3 topNormal{0,0,1}, bottomNormal{0,0,1};
    Vec3 midNormal{0,0,1};
    double topCoronalTilt = 0, bottomCoronalTilt = 0, midCoronalTilt = 0;     // deg, signed
    double topSagittalTilt = 0, bottomSagittalTilt = 0, midSagittalTilt = 0;  // deg, signed
};

// One Cobb segment in either coronal or sagittal plane (Step 3 output).
struct CobbSegment {
    int startIndex = 0, endIndex = 0;
    int startLabel = 0, endLabel = 0;
    std::string startName, endName;
    double signedAngleDeg = 0;
    double cobbDeg = 0;
    int vertebraCount = 0;
    int apexLabel = 0;
    std::string apexName;
    double apexDistanceMm = 0;
    std::string curveType;     // thoracic / lumbar / thoracolumbar / unknown (coronal only)
    std::string clinicalStatus; // valid_curve / normal / abnormal_low / abnormal_high
};

// VerSe label naming (1..7 = C1-C7, 8..19 = T1-T12, 20..24 = L1-L5, etc).
std::string label_name(int label);

}  // namespace cobb
