// CobbAnalysis.h — public API of the C++ Cobb pipeline.
#pragma once

#include "CobbTypes.h"

#include <string>
#include <vector>
#include <vtkSmartPointer.h>

class vtkImageData;

namespace cobb {

// Bundle of tuning knobs (defaults match the Python pipeline).
struct Params {
    // Step 2: body cut detection / extraction
    double bodyYBinMm        = 1.5;
    double bodyGridMm        = 1.5;
    double bodyCenterWidthMm = 6.0;
    int    bodyMinSlicePoints = 80;
    int    bodyBadPatience   = 2;
    int    bodyMinComponentArea = 8;
    // Step 2: endplate detection
    double anteriorQuantile  = 0.92;
    double growYQuantile     = 0.98;
    double posteriorMarginMm = 8.0;
    double topQuantile       = 0.76;
    double bottomQuantile    = 0.24;
    double maxNormalAngleDeg  = 62.0;
    double growNormalAngleDeg = 58.0;
    double growZMarginMm     = 5.0;
    double fillHoleGridMm    = 1.2;
    bool   fillHoles         = true;
    // Step 3: clinical thresholds (coronal scoliosis only used here)
    double coronalScoliosisThresholdDeg = 10.0;
    int    minSegmentIntervals = 1;
    double minSlopeDeg       = 1.0;
};

// Read an NRRD label volume and produce a vtkImageData with proper sform.
// Returns nullptr on failure (and prints to std::cerr).
vtkSmartPointer<vtkImageData> readNrrd(const std::string& path,
                                       Vec3& origin,
                                       std::array<Vec3, 3>& spaceDirections,
                                       std::array<int, 3>& dims);

// Step 1 — build per-vertebra local coordinate systems for the given labels.
std::vector<LCS> computeLCS(vtkImageData* labelMap,
                            const Vec3& origin,
                            const std::array<Vec3, 3>& spaceDirections,
                            const std::vector<int>& labels);

// Step 2 — detect superior/inferior endplate triangles for each vertebra.
std::vector<EndplateResult> detectEndplates(vtkImageData* labelMap,
                                            const Vec3& origin,
                                            const std::array<Vec3, 3>& spaceDirections,
                                            const std::vector<LCS>& systems,
                                            const Params& params);

// Step 3a — fit one plane to each detected endplate (PCA, weighted by triangle area).
std::vector<EndplateMeasure> measureEndplates(const std::vector<EndplateResult>& items);

// Step 3b — segment the spine in the given plane ("coronal" / "sagittal") and
// classify each segment with the basic clinical rules.
std::vector<CobbSegment> findCobbSegments(const std::vector<EndplateMeasure>& measures,
                                          const std::vector<int>& labels,
                                          const std::string& plane,
                                          const Params& params);

}  // namespace cobb
