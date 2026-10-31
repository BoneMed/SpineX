// tools/cobb_test/main.cpp — hybrid Cobb test harness.
//
//   1. Spawns python/cobb/run_cobb.py as a subprocess (the algorithm uses
//      scipy/skimage which we do not reimplement in C++).
//   2. Reads back per-vertebra .vtp meshes + meta.json + segments_<plane>.json.
//   3. Renders the final Cobb-angle figure in C++ with VTK only.
//
// Usage:
//   cobb_test <segmentation.nrrd> [labels=12-24] [plane=coronal]
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkArrowSource.h>
#include <vtkPlaneSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkParametricSpline.h>
#include <vtkParametricFunctionSource.h>
#include <vtkCamera.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using nlohmann::json;

// -------------------- helpers ----------------------------------------------

namespace {

struct Vec3 { double x = 0, y = 0, z = 0; };

Vec3 vec3_from_json(const json& j) {
    Vec3 v;
    if (j.is_array() && j.size() >= 3) {
        v.x = j.at(0).get<double>();
        v.y = j.at(1).get<double>();
        v.z = j.at(2).get<double>();
    }
    return v;
}

Vec3 v_neg(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
Vec3 v_sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double v_norm(const Vec3& a) { return std::sqrt(a.x*a.x + a.y*a.y + a.z*a.z); }
Vec3 v_normalize(const Vec3& a) { double n = v_norm(a); return n < 1e-12 ? Vec3{0,0,1} : Vec3{a.x/n, a.y/n, a.z/n}; }
Vec3 v_cross(const Vec3& a, const Vec3& b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
double v_dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

// Hex "#rrggbb" → [0..1] RGB.
void hex_rgb(const std::string& hex, double rgb[3]) {
    int r = std::stoi(hex.substr(1, 2), nullptr, 16);
    int g = std::stoi(hex.substr(3, 2), nullptr, 16);
    int b = std::stoi(hex.substr(5, 2), nullptr, 16);
    rgb[0] = r / 255.0; rgb[1] = g / 255.0; rgb[2] = b / 255.0;
}

const char* SEGMENT_COLORS[8] = {
    "#d7263d", "#1b998b", "#f46036", "#2e294e",
    "#8e5cf7", "#3a86ff", "#ffbe0b", "#00b4d8",
};
const char* TOP_COLOR    = "#ff8c00";
const char* BOTTOM_COLOR = "#00b3b3";
const char* SHARED_COLOR = "#0057ff";
const char* APEX_COLOR   = "#ffd400";

vtkSmartPointer<vtkPolyData> load_vtp(const std::string& path) {
    auto rd = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    rd->SetFileName(path.c_str());
    rd->Update();
    auto out = vtkSmartPointer<vtkPolyData>::New();
    out->ShallowCopy(rd->GetOutput());
    return out;
}

vtkSmartPointer<vtkActor> make_actor(vtkPolyData* poly, const std::string& hex,
                                     double opacity = 1.0, bool edges = false) {
    auto m = vtkSmartPointer<vtkPolyDataMapper>::New();
    m->SetInputData(poly);
    m->ScalarVisibilityOff();
    auto a = vtkSmartPointer<vtkActor>::New();
    a->SetMapper(m);
    double rgb[3]; hex_rgb(hex, rgb);
    a->GetProperty()->SetColor(rgb);
    a->GetProperty()->SetOpacity(opacity);
    if (edges) {
        a->GetProperty()->EdgeVisibilityOn();
        a->GetProperty()->SetEdgeColor(0, 0, 0);
        a->GetProperty()->SetLineWidth(1.0);
    }
    return a;
}

vtkSmartPointer<vtkActor> make_arrow(const Vec3& start, const Vec3& dir, double length) {
    auto src = vtkSmartPointer<vtkArrowSource>::New();
    src->SetTipLength(0.28);
    src->SetTipRadius(0.09);
    src->SetShaftRadius(0.03);
    src->Update();
    Vec3 d = v_normalize(dir);
    auto t = vtkSmartPointer<vtkTransform>::New();
    t->Translate(start.x, start.y, start.z);
    Vec3 x{1, 0, 0};
    Vec3 axis = v_cross(x, d);
    double dotxd = std::clamp(v_dot(x, d), -1.0, 1.0);
    double ang = std::acos(dotxd) * 180.0 / 3.14159265358979323846;
    if (v_norm(axis) > 1e-6) {
        Vec3 an = v_normalize(axis);
        t->RotateWXYZ(ang, an.x, an.y, an.z);
    } else if (dotxd < 0) {
        t->RotateWXYZ(180.0, 0, 1, 0);
    }
    t->Scale(length, length, length);
    auto tf = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    tf->SetInputConnection(src->GetOutputPort());
    tf->SetTransform(t);
    tf->Update();
    return make_actor(tf->GetOutput(), "#1a1a1a", 0.95);
}

vtkSmartPointer<vtkActor> make_plane(const Vec3& center, const Vec3& normal,
                                     const Vec3& preferredU, double half = 9.0) {
    Vec3 n = v_normalize(normal);
    Vec3 u = preferredU;
    double s = v_dot(u, n);
    u = {u.x - s*n.x, u.y - s*n.y, u.z - s*n.z};
    if (v_norm(u) < 1e-6) {
        Vec3 fallback = {0, 1, 0};
        s = v_dot(fallback, n);
        u = {fallback.x - s*n.x, fallback.y - s*n.y, fallback.z - s*n.z};
    }
    u = v_normalize(u);
    Vec3 v = v_normalize(v_cross(n, u));
    auto src = vtkSmartPointer<vtkPlaneSource>::New();
    Vec3 o = {center.x - u.x*half - v.x*half,
              center.y - u.y*half - v.y*half,
              center.z - u.z*half - v.z*half};
    Vec3 p1 = {o.x + u.x*2*half, o.y + u.y*2*half, o.z + u.z*2*half};
    Vec3 p2 = {o.x + v.x*2*half, o.y + v.y*2*half, o.z + v.z*2*half};
    src->SetOrigin(o.x, o.y, o.z);
    src->SetPoint1(p1.x, p1.y, p1.z);
    src->SetPoint2(p2.x, p2.y, p2.z);
    src->Update();
    return make_actor(src->GetOutput(), "#888888", 0.32, true);
}

vtkSmartPointer<vtkActor> make_spine_curve(const std::vector<Vec3>& centroids) {
    auto pts = vtkSmartPointer<vtkPoints>::New();
    for (const auto& c : centroids) pts->InsertNextPoint(c.x, c.y, c.z);
    auto sp = vtkSmartPointer<vtkParametricSpline>::New();
    sp->SetPoints(pts);
    auto fs = vtkSmartPointer<vtkParametricFunctionSource>::New();
    fs->SetParametricFunction(sp);
    fs->SetUResolution((int)centroids.size() * 24);
    fs->SetScalarModeToNone();
    fs->Update();
    auto a = make_actor(fs->GetOutput(), "#000000", 1.0);
    a->GetProperty()->SetLineWidth(3.0);
    return a;
}

std::string find_python() {
    if (const char* env = std::getenv("SPINEX_PYTHON")) return env;
    // Default: BoneMed conda env (used in CMake too).
    return "C:/Users/Administrator/anaconda3/envs/BoneMed/python.exe";
}

std::string find_run_cobb_py() {
    // tools/cobb_test/main.cpp lives next to ../../python/cobb/run_cobb.py at
    // build time; at run time look for the script alongside the executable.
    fs::path exeDir;
    char buf[1024];
#ifdef _WIN32
    GetModuleFileNameA(nullptr, buf, sizeof(buf));
    exeDir = fs::path(buf).parent_path();
#else
    (void)buf;
#endif
    // First try: <exeDir>/python/cobb/run_cobb.py (CMake POST_BUILD copies this).
    fs::path local = exeDir / "python" / "cobb" / "run_cobb.py";
    if (fs::exists(local)) return local.string();
    // Fallback: SpineX-cpp source tree.
    fs::path src = "H:/pythonproject/PHD_PROGRAM/SpineX-cpp/python/cobb/run_cobb.py";
    return src.string();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: cobb_test <segmentation.nrrd> [labels=12-24] [plane=coronal]\n";
        return 1;
    }
    std::string segPath = argv[1];
    std::string labelSpec = (argc > 2) ? argv[2] : "12-24";
    std::string plane     = (argc > 3) ? argv[3] : "coronal";

    // ---- 1. Run python pipeline as subprocess ---------------------------
    fs::path tmpDir = fs::temp_directory_path() / "spinex_cobb";
    fs::create_directories(tmpDir);
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    std::string py = find_python();
    std::string scr = find_run_cobb_py();
    std::cerr << "[cobb_test] python : " << py << "\n";
    std::cerr << "[cobb_test] script : " << scr << "\n";
    std::cerr << "[cobb_test] outdir : " << tmpDir.string() << "\n";

    std::ostringstream cmd;
    cmd << "\"\"" << py << "\" \"" << scr << "\""
        << " --seg \"" << segPath << "\""
        << " --labels " << labelSpec
        << " --plane " << plane
        << " --out \"" << tmpDir.string() << "\""
        << "\"";
    std::cerr << "[cobb_test] $ " << cmd.str() << "\n";
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        std::cerr << "[cobb_test] python returned " << rc << "\n";
        return 2;
    }

    // ---- 2. Load metadata + meshes -------------------------------------
    fs::path metaPath = tmpDir / "meta.json";
    fs::path segPathJson = tmpDir / ("segments_" + plane + ".json");
    if (!fs::exists(metaPath) || !fs::exists(segPathJson)) {
        std::cerr << "[cobb_test] missing meta/segments json\n";
        return 3;
    }

    json meta, segPayload;
    {
        std::ifstream f(metaPath); f >> meta;
        std::ifstream g(segPathJson); g >> segPayload;
    }

    // ---- 3. Build render scene -----------------------------------------
    auto renderer = vtkSmartPointer<vtkRenderer>::New();
    renderer->SetBackground(1, 1, 1);

    std::map<int, int> labelToSeg;
    std::set<int> sharedLabels, apexLabels;
    auto& segs = segPayload.at("segments");
    auto& labelsArr = meta.at("labels");
    std::vector<int> labels;
    for (const auto& v : labelsArr) labels.push_back(v.get<int>());
    auto labelIndex = [&](int label) -> int {
        for (size_t i = 0; i < labels.size(); ++i) if (labels[i] == label) return (int)i;
        return -1;
    };

    for (size_t si = 0; si < segs.size(); ++si) {
        const auto& s = segs[si];
        int a = labelIndex(s.at("start_label").get<int>());
        int b = labelIndex(s.at("end_label").get<int>());
        if (a >= 0 && b >= 0)
            for (int idx = a; idx <= b; ++idx) labelToSeg[labels[idx]] = (int)si;
        apexLabels.insert(s.at("apex_label").get<int>());
    }
    for (size_t si = 0; si + 1 < segs.size(); ++si) {
        if (segs[si].at("end_label") == segs[si+1].at("start_label"))
            sharedLabels.insert(segs[si].at("end_label").get<int>());
    }

    std::vector<Vec3> centroids;
    centroids.reserve(meta.at("vertebrae").size());

    for (const auto& v : meta.at("vertebrae")) {
        int label = v.at("label").get<int>();
        Vec3 centroid    = vec3_from_json(v.at("centroid"));
        Vec3 axisX       = vec3_from_json(v.at("axis_x"));
        Vec3 topCenter   = vec3_from_json(v.at("top_center"));
        Vec3 bottomCenter= vec3_from_json(v.at("bottom_center"));
        Vec3 topNormal   = vec3_from_json(v.at("top_normal"));
        Vec3 bottomNormal= vec3_from_json(v.at("bottom_normal"));
        std::string fullPath   = v.at("full_mesh_path").get<std::string>();
        std::string topPath    = v.at("top_mesh_path").get<std::string>();
        std::string bottomPath = v.at("bottom_mesh_path").get<std::string>();

        // Choose color
        std::string color = "#bbbbbb";
        double opacity = 0.35;
        if (sharedLabels.count(label))     { color = SHARED_COLOR;  opacity = 0.62; }
        else if (apexLabels.count(label))  { color = APEX_COLOR;    opacity = 0.78; }
        else {
            auto it = labelToSeg.find(label);
            if (it != labelToSeg.end()) { color = SEGMENT_COLORS[it->second % 8]; opacity = 0.40; }
        }

        auto fullMesh = load_vtp(fullPath);
        if (fullMesh && fullMesh->GetNumberOfPoints() > 0)
            renderer->AddActor(make_actor(fullMesh, color, opacity));

        auto topMesh = load_vtp(topPath);
        if (topMesh && topMesh->GetNumberOfCells() > 0)
            renderer->AddActor(make_actor(topMesh, TOP_COLOR, 0.95));
        auto bottomMesh = load_vtp(bottomPath);
        if (bottomMesh && bottomMesh->GetNumberOfCells() > 0)
            renderer->AddActor(make_actor(bottomMesh, BOTTOM_COLOR, 0.95));

        renderer->AddActor(make_plane(topCenter, topNormal, axisX));
        renderer->AddActor(make_plane(bottomCenter, bottomNormal, axisX));
        renderer->AddActor(make_arrow(topCenter, topNormal, 18.0));
        renderer->AddActor(make_arrow(bottomCenter, v_neg(bottomNormal), 18.0));

        centroids.push_back(centroid);
    }
    if (centroids.size() >= 2) renderer->AddActor(make_spine_curve(centroids));

    // 2D text overlay with Cobb angles
    std::ostringstream txt;
    txt << "Cobb segments (" << plane << "):\n";
    if (segs.empty()) txt << "  (no valid curve)\n";
    for (const auto& s : segs) {
        txt.precision(1); txt << std::fixed;
        txt << "  " << s.at("start_name").get<std::string>() << "-"
            << s.at("end_name").get<std::string>()
            << "  " << s.at("cobb_deg").get<double>() << " deg";
        if (plane == "coronal")
            txt << "  apex=" << s.at("apex_name").get<std::string>()
                << "  type=" << s.at("curve_type").get<std::string>();
        txt << "\n";
    }
    auto text = vtkSmartPointer<vtkTextActor>::New();
    text->SetInput(txt.str().c_str());
    text->GetTextProperty()->SetFontSize(16);
    text->GetTextProperty()->SetColor(0, 0, 0);
    text->SetPosition(20, 20);
    renderer->AddActor2D(text);

    auto axes = vtkSmartPointer<vtkAxesActor>::New();
    axes->SetTotalLength(30, 30, 30);
    auto omw = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    omw->SetOrientationMarker(axes);
    omw->SetViewport(0.85, 0.0, 1.0, 0.18);

    auto win = vtkSmartPointer<vtkRenderWindow>::New();
    win->AddRenderer(renderer);
    win->SetWindowName("SpineX Cobb (C++/Python hybrid)");
    win->SetSize(1280, 900);

    auto iren = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    iren->SetRenderWindow(win);
    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    iren->SetInteractorStyle(style);
    omw->SetInteractor(iren);
    omw->EnabledOn();
    omw->InteractiveOff();

    renderer->ResetCamera();
    if (!centroids.empty()) {
        Vec3 mid = centroids[centroids.size() / 2];
        auto cam = renderer->GetActiveCamera();
        cam->SetFocalPoint(mid.x, mid.y, mid.z);
        cam->SetPosition(mid.x, mid.y - 600, mid.z);
        cam->SetViewUp(0, 0, 1);
    }
    renderer->ResetCamera();
    win->Render();
    iren->Start();
    return 0;
}
