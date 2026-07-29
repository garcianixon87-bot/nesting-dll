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
static double g_stepAngle = 90.0;

// Rotar punto alrededor del origen
Point RotatePt(Point p, double deg) {
    double rad = deg * 3.14159265358979323846 / 180.0;
    double c = cos(rad), s = sin(rad);
    return { p.x * c - p.y * s, p.x * s + p.y * c };
}

// Algoritmo de Separating Axis Theorem (SAT) para detección exacta de superposición de polígonos
bool PolygonsOverlap(const std::vector<Point>& polyA, const std::vector<Point>& polyB, double margin) {
    auto checkAxis = [](const std::vector<Point>& p1, const std::vector<Point>& p2, double m) {
        for (size_t i = 0; i < p1.size(); i++) {
            size_t j = (i + 1) % p1.size();
            Point axis = { -(p1[j].y - p1[i].y), p1[j].x - p1[i].x };
            double len = sqrt(axis.x * axis.x + axis.y * axis.y);
            if (len == 0) continue;
            axis.x /= len; axis.y /= len;

            double minA = 1e15, maxA = -1e15;
            for (const auto& p : p1) {
                double proj = p.x * axis.x + p.y * axis.y;
                minA = (std::min)(minA, proj); maxA = (std::max)(maxA, proj);
            }

            double minB = 1e15, maxB = -1e15;
            for (const auto& p : p2) {
                double proj = p.x * axis.x + p.y * axis.y;
                minB = (std::min)(minB, proj); maxB = (std::max)(maxB, proj);
            }

            if (maxA + m < minB || maxB + m < minA) return false;
        }
        return true;
    };

    return checkAxis(polyA, polyB, margin) && checkAxis(polyB, polyA, margin);
}

extern "C" {

    __declspec(dllexport) int __stdcall InitEngine(double margin, double stepAngle) {
        g_polygons.clear();
        g_margin = margin;
        g_stepAngle = (stepAngle > 0) ? stepAngle : 90.0;
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

        // Ordenar piezas de mayor a menor para colocar los cuerpos grandes primero
        std::sort(g_polygons.begin(), g_polygons.end(), [](const ItemPolygon& a, const ItemPolygon& b) {
            return a.rawVertices.size() > b.rawVertices.size();
        });

        std::vector<std::vector<Point>> placedPolygons;

        std::vector<double> angles = { 0.0 };
        if (g_stepAngle > 0) {
            for (double a = g_stepAngle; a < 360.0; a += g_stepAngle) angles.push_back(a);
        }

        double gridStep = 5.0; // Resolución de deslizamiento en mm

        for (auto& poly : g_polygons) {
            bool placed = false;
            double bestX = 0, bestY = 0, bestAngle = 0;
            double bestScore = 1e15;

            std::vector<double> testAngles = poly.allowRotation ? angles : std::vector<double>{ 0.0 };

            for (double ang : testAngles) {
                // 1. Obtener forma rotada
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

                // 2. Probar encastre continuo sobre la tela (Bottom-Left real)
                for (double ty = sheetHeight - pH - g_margin; ty >= g_margin; ty -= gridStep) {
                    for (double tx = g_margin; tx <= sheetWidth - pW - g_margin; tx += gridStep) {

                        double offsetX = tx - minX;
                        double offsetY = ty - minY;

                        std::vector<Point> candidate;
                        candidate.reserve(rotPts.size());
                        for (const auto& p : rotPts) {
                            candidate.push_back({ p.x + offsetX, p.y + offsetY });
                        }

                        // Validar si choca con los POLÍGONOS REALES ya colocados
                        bool collide = false;
                        for (const auto& placedPoly : placedPolygons) {
                            if (PolygonsOverlap(candidate, placedPoly, g_margin)) {
                                collide = true;
                                break;
                            }
                        }

                        if (!collide) {
                            double score = (sheetHeight - ty) * 2.0 + tx; // Priorizar esquina inferior izquierda
                            if (score < bestScore) {
                                bestScore = score;
                                bestX = offsetX;
                                bestY = offsetY;
                                bestAngle = ang;
                                placed = true;
                            }
                            break; // Encontró la posición más baja para esta columna
                        }
                    }
                }
            }

            if (placed) {
                poly.resX = bestX;
                poly.resY = bestY;
                poly.resAngle = bestAngle;

                // Guardar la silueta final de la pieza para las siguientes
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
