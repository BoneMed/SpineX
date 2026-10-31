// CobbCommon.cpp — shared utilities: Vec3 math, label naming, NRRD reading.
#include "CobbAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include <vtkImageData.h>
#include <vtkMatrix3x3.h>
#include <vtkNrrdReader.h>
#include <vtkPointData.h>
#include <vtkInformation.h>

namespace cobb {

double norm(const Vec3& v) {
    return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

Vec3 normalize(const Vec3& v) {
    double n = norm(v);
    if (n < 1e-12) return {0, 0, 0};
    return {v[0]/n, v[1]/n, v[2]/n};
}

std::string label_name(int label) {
    if (label >= 1 && label <= 7)  return "C" + std::to_string(label);
    if (label >= 8 && label <= 19) return "T" + std::to_string(label - 7);
    if (label >= 20 && label <= 25) return "L" + std::to_string(label - 19);
    if (label == 26) return "T13";
    if (label == 27) return "Sacrum";
    return "Label" + std::to_string(label);
}

vtkSmartPointer<vtkImageData> readNrrd(const std::string& path,
                                       Vec3& origin,
                                       std::array<Vec3, 3>& spaceDirections,
                                       std::array<int, 3>& dims)
{
    auto reader = vtkSmartPointer<vtkNrrdReader>::New();
    reader->SetFileName(path.c_str());
    reader->Update();

    auto img = reader->GetOutput();
    if (!img) {
        std::cerr << "[cobb] NRRD read failed: " << path << "\n";
        return nullptr;
    }

    int d[3];
    img->GetDimensions(d);
    dims = {d[0], d[1], d[2]};

    // VTK stores the sform components on the reader as DataSpacing + DataOrigin
    // when vtkNrrdReader is used in scalar mode. To recover the full 3x3
    // direction matrix we use vtkNrrdReader::GetDataDirection() if available,
    // otherwise fall back to spacing*identity.
    double org[3];
    img->GetOrigin(org);
    origin = {org[0], org[1], org[2]};

    // GetDirectionMatrix on vtkImageData (VTK >= 9.x) gives the 3x3.
    vtkMatrix3x3* dm = img->GetDirectionMatrix();
    double sp[3];
    img->GetSpacing(sp);
    if (dm) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                spaceDirections[j][i] = dm->GetElement(i, j) * sp[j];
            }
        }
    } else {
        spaceDirections[0] = {sp[0], 0, 0};
        spaceDirections[1] = {0, sp[1], 0};
        spaceDirections[2] = {0, 0, sp[2]};
    }

    std::cerr << "[cobb] NRRD: dims=" << dims[0] << "x" << dims[1] << "x" << dims[2]
              << " origin=" << origin[0] << "," << origin[1] << "," << origin[2]
              << " sp=" << sp[0] << "," << sp[1] << "," << sp[2] << "\n";
    return img;
}

}  // namespace cobb
