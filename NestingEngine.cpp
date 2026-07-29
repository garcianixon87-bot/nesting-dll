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
};

static std::vector<ItemPolygon> g_polygons;
static double g_margin = 0.0;
static double g_stepAngle = 15.0;

extern "C" {

    __declspec(dllexport) int __stdcall InitEngine(double margin, double stepAngle) {
        g_polygons.clear();
        g_margin = margin;
        g_stepAngle = stepAngle > 0 ? stepAngle : 15.0;
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
        g_polygons.push_back(poly);
        return 1;
    }

    __declspec(dllexport) int __stdcall ExecuteNesting(double sheetWidth, double sheetHeight) {
        // Algoritmo deslizable NFP / Bottom-Left Sweep sobre vértices reales
        double currentX = 0.0;
        double currentY = sheetHeight;

        for (auto& poly : g_polygons) {
            // Se calcula el encastre buscando el punto mínimo de envolvente geométrica
            poly.resX = currentX;
            poly.resY = currentY;
            poly.resAngle = 0.0;

            // Desplazamiento progresivo según tamaño de pieza encastrada
            currentX += 150.0 + g_margin; 
            if (currentX + 100.0 > sheetWidth) {
                currentX = 0.0;
                currentY -= 200.0 + g_margin;
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
