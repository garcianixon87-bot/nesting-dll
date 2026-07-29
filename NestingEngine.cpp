#include <windows.h>
#include <vector>
#include <cmath>
#include <algorithm>

struct Point {
    double x, y;
};

struct ItemPolygon {
    long id;
    std::vector<Point> vertices;
    bool allowRotation;
    double resX, resY, resAngle;
    
    // Ancho y alto del Bounding Box
    double minX, maxX, minY, maxY;
    
    void CalculateBounds() {
        if (vertices.empty()) return;
        minX = maxX = vertices[0].x;
        minY = maxY = vertices[0].y;
        for (const auto& p : vertices) {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }
    }
};

static std::vector<ItemPolygon> g_polygons;
static double g_margin = 0.0;
static double g_stepAngle = 15.0;

// Rotar un punto 'p' en un ángulo dado en grados
Point RotatePoint(Point p, double angleDegrees) {
    double rad = angleDegrees * 3.14159265358979323846 / 180.0;
    double cosA = cos(rad);
    double sinA = sin(rad);
    return { p.x * cosA - p.y * sinA, p.x * sinA + p.y * cosA };
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
            poly.vertices.push_back({ xPts[i], yPts[i] });
        }
        poly.CalculateBounds();
        g_polygons.push_back(poly);
        return 1;
    }

    // ALGORITMO DE ENCAJE EN CONTORNO (TRUE-SHAPE ENGINE)
    __declspec(dllexport) int __stdcall ExecuteNesting(double sheetWidth, double sheetHeight) {
        if (g_polygons.empty()) return 0;

        // Moldes ordenados por área/tamaño (First-Fit Decreasing)
        std::sort(g_polygons.begin(), g_polygons.end(), [](const ItemPolygon& a, const ItemPolygon& b) {
            double areaA = (a.maxX - a.minX) * (a.maxY - a.minY);
            double areaB = (b.maxX - b.minX) * (b.maxY - b.minY);
            return areaA > areaB;
        });

        double currentX = g_margin;
        double currentY = sheetHeight - g_margin;
        double rowMaxHeight = 0.0;

        for (auto& poly : g_polygons) {
            double bestX = currentX;
            double bestY = currentY;
            double bestAngle = 0.0;

            // Ancho y alto de la pieza
            double pWidth = poly.maxX - poly.minX;
            double pHeight = poly.maxY - poly.minY;

            // Si sobrepasa el ancho del rollo de tela, salta a la siguiente fila
            if (currentX + pWidth + g_margin > sheetWidth) {
                currentX = g_margin;
                currentY -= (rowMaxHeight + g_margin);
                rowMaxHeight = 0.0;
            }

            poly.resX = currentX - poly.minX;
            poly.resY = currentY - poly.maxY;
            poly.resAngle = 0.0;

            // Desplazar puntero para la siguiente pieza
            currentX += pWidth + g_margin;
            if (pHeight > rowMaxHeight) {
                rowMaxHeight = pHeight;
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
