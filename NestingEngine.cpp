#include <windows.h>
#include <vector>
#include <cmath>
#include <algorithm>

struct Point {
    double x, y;
};

struct ItemPolygon {
    long id;
    std::vector<Point> rawVertices;
    bool allowRotation;
    double resX, resY, resAngle;
};

static std::vector<ItemPolygon> g_polygons;
static double g_margin = 0.0;
static double g_stepAngle = 180.0; // 180 para permitir giros invertidos en mangas

// Rotar punto
Point RotatePt(Point p, double deg) {
    double rad = deg * 3.14159265358979323846 / 180.0;
    double c = cos(rad), s = sin(rad);
    return { p.x * c - p.y * s, p.x * s + p.y * c };
}

// Punto dentro de polígono (Ray-Casting) para detección de colisión cóncava
bool PointInPolygon(Point p, const std::vector<Point>& poly) {
    bool inside = false;
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

// Verificación de intersección de segmentos de contorno
bool SegmentsIntersect(Point p1, Point q1, Point p2, Point q2) {
    auto CCW = [](Point A, Point B, Point C) {
        return (C.y - A.y) * (B.x - A.x) > (B.y - A.y) * (C.x - A.x);
    };
    return (CCW(p1, p2, q2) != CCW(q1, p2, q2)) && (CCW(p1, q1, p2) != CCW(p1, q1, q2));
}

// Detección de colisión True-Shape exacta entre dos piezas
bool TrueShapeOverlap(const std::vector<Point>& polyA, const std::vector<Point>& polyB, double margin) {
    // 1. Validar intersección de bordes
    size_t nA = polyA.size(), nB = polyB.size();
    for (size_t i = 0; i < nA; ++i) {
        Point a1 = polyA[i];
        Point a2 = polyA[(i + 1) % nA];
        for (size_t j = 0; j < nB; ++j) {
            Point b1 = polyB[j];
            Point b2 = polyB[(j + 1) % nB];
            if (SegmentsIntersect(a1, a2, b1, b2)) return true;
        }
    }
    // 2. Validar inclusión de vértices (pieza contenida en hueco o dentro)
    for (const auto& p : polyA) {
        if (PointInPolygon(p, polyB)) return true;
    }
    for (const auto& p : polyB) {
        if (PointInPolygon(p, polyA)) return true;
    }
    return false;
}

extern "C" {

    __declspec(dllexport) int __stdcall InitEngine(double margin, double stepAngle) {
        g_polygons.clear();
        g_margin = margin;
        g_stepAngle = (stepAngle > 0) ? stepAngle : 180.0;
        return 1;
    }

    __declspec(dllexport) int __stdcall AddPolygon(long polyID, long count, double* xPts, double* yPts, int allowRotation) {
        ItemPolygon poly;
        poly.id = polyID;
        poly.allowRotation = (allowRotation != 0);
        poly.resX = 0; poly.resY = 0; poly.resAngle = 0;

        for (long i = 0; i < count; i++) {
            poly.rawVertices.push_back({ xPts[i], yPts[i] });
        }
        g_polygons.push_back(poly);
        return 1;
    }

    __declspec(dllexport) int __stdcall ExecuteNesting(double sheetWidth, double sheetHeight) {
        if (g_polygons.empty()) return 0;

        // Ordenar piezas: Piezas más complejas/grandes primero
        std::sort(g_polygons.begin(), g_polygons.end(), [](const ItemPolygon& a, const ItemPolygon& b) {
            return a.rawVertices.size() > b.rawVertices.size();
        });

        std::vector<std::vector<Point>> placedPolygons;

        // Probar rotaciones permitidas (ej: 0 y 180 para textil)
        std::vector<double> angles = { 0.0 };
        if (g_stepAngle > 0) {
            for (double a = g_stepAngle; a < 360.0; a += g_stepAngle) angles.push_back(a);
        }

        double stepXY = 8.0; // Resolución de deslizamiento en mm/unidades

        for (auto& poly : g_polygons) {
            bool placed = false;
            double bestX = 0, bestY = 0, bestAngle = 0;
            double bestScore = 1e15;

            std::vector<double> testAngles = poly.allowRotation ? angles : std::vector<double>{ 0.0 };

            for (double ang : testAngles) {
                std::vector<Point> rotPts;
                double minX = 1e15, maxX = -1e15, minY = 1e15, maxY = -1e15;

                for (const auto& pt : poly.rawVertices) {
                    Point r = RotatePt(pt, ang);
                    rotPts.push_back(r);
                    minX = (std::min)(minX, r.x); maxX = (std::max)(maxX, r.x);
                    minY = (std::min)(minY, r.y); maxY = (std::max)(maxY, r.y);
                }

                double pW = maxX - minX;
                double pH = maxY - minY;

                // Escaneo Bottom-Left encastrado
                for (double ty = g_margin; ty <= sheetHeight - pH - g_margin; ty += stepXY) {
                    for (double tx = g_margin; tx <= sheetWidth - pW - g_margin; tx += stepXY) {

                        double offsetX = tx - minX;
                        double offsetY = ty - minY;

                        std::vector<Point> candidate;
                        candidate.reserve(rotPts.size());
                        for (const auto& p : rotPts) {
                            candidate.push_back({ p.x + offsetX, p.y + offsetY });
                        }

                        // Colisión True-Shape
                        bool collide = false;
                        for (const auto& placedPoly : placedPolygons) {
                            if (TrueShapeOverlap(candidate, placedPoly, g_margin)) {
                                collide = true;
                                break;
                            }
                        }

                        if (!collide) {
                            // Evaluación para compactar la tizada hacia el origen
                            double score = ty * 10.0 + tx;
                            if (score < bestScore) {
                                bestScore = score;
                                bestX = offsetX;
                                bestY = offsetY;
                                bestAngle = ang;
                                placed = true;
                            }
                            break; 
                        }
                    }
                }
            }

            if (placed) {
                poly.resX = bestX;
                poly.resY = bestY;
                poly.resAngle = bestAngle;

                std::vector<Point> finalPts;
                for (const auto& pt : poly.rawVertices) {
                    Point r = RotatePt(pt, bestAngle);
                    finalPts.push_back({ r.x + bestX, r.y + bestY });
                }
                placedPolygons.push_back(finalPts);
            }
        }
        return 1;
    }

    __declspec(dllexport) int __stdcall GetResult(long polyID, double* outX, double* outY, double* outAngle) {
        for (const auto& poly : g_polygons) {
            if (poly.id == polyID) {
                *outX = poly.resX;
                *outY = poly.resY;
                *outAngle = poly.resAngle;
                return 1;
            }
        }
        return 0;
    }

    __declspec(dllexport) void __stdcall FreeEngine() {
        g_polygons.clear();
    }
}
