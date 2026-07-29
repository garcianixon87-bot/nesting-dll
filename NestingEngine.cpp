#include <windows.h>
#include <vector>
#include <cmath>

#define DLLEXPORT __declspec(dllexport)

// Estructura para almacenar puntos 2D
struct Point {
    double x;
    double y;
};

// Estructura para representar un molde/polígono
struct PolygonShape {
    long id;
    long nodeCount;
    std::vector<Point> nodes;
    long allowRotation;
    double resX;
    double resY;
    double resAngle;
};

// Variables globales del motor de tizada
static double g_margin = 0.0;
static double g_stepAngle = 180.0;
static std::vector<PolygonShape> g_polygons;

// DllMain para inicialización estándar de Windows DLL
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C" {

    // 1. Inicializar el motor
    DLLEXPORT long __stdcall InitEngine(double margin, double stepAngle) {
        g_margin = margin;
        g_stepAngle = stepAngle;
        g_polygons.clear();
        return 1; // OK
    }

    // 2. Agregar polígonos/moldes desde VBA
    DLLEXPORT long __stdcall AddPolygon(long polyID, long nodeCount, double* xPts, double* yPts, long allowRotation) {
        if (nodeCount < 3 || xPts == nullptr || yPts == nullptr) return 0;

        PolygonShape poly;
        poly.id = polyID;
        poly.nodeCount = nodeCount;
        poly.allowRotation = allowRotation;
        poly.resX = 0.0;
        poly.resY = 0.0;
        poly.resAngle = 0.0;

        for (long i = 0; i < nodeCount; ++i) {
            poly.nodes.push_back({ xPts[i], yPts[i] });
        }

        g_polygons.push_back(poly);
        return 1; // OK
    }

    // 3. Ejecutar algoritmo de empaquetado (Nesting)
    DLLEXPORT long __stdcall ExecuteNesting(double sheetWidth, double sheetHeight) {
        double currentX = g_margin;
        double currentY = g_margin;
        double maxRowHeight = 0.0;

        for (auto& poly : g_polygons) {
            // Calcular Bounding Box simple de la pieza
            double minX = poly.nodes[0].x, maxX = poly.nodes[0].x;
            double minY = poly.nodes[0].y, maxY = poly.nodes[0].y;

            for (const auto& pt : poly.nodes) {
                if (pt.x < minX) minX = pt.x;
                if (pt.x > maxX) maxX = pt.x;
                if (pt.y < minY) minY = pt.y;
                if (pt.y > maxY) maxY = pt.y;
            }

            double width = maxX - minX;
            double height = maxY - minY;

            // Salto de fila si excede el ancho de la mesa/lámina
            if (currentX + width + g_margin > sheetWidth) {
                currentX = g_margin;
                currentY += maxRowHeight + g_margin;
                maxRowHeight = 0.0;
            }

            // Asignar nuevas coordenadas relativas
            poly.resX = currentX - minX;
            poly.resY = currentY - minY;
            poly.resAngle = 0.0; // Ángulo por defecto si no requiere rotación

            // Actualizar límites
            currentX += width + g_margin;
            if (height > maxRowHeight) {
                maxRowHeight = height;
            }
        }
        return 1; // OK
    }

    // 4. Devolver resultado a VBA
    DLLEXPORT long __stdcall GetResult(long polyID, double* outX, double* outY, double* outAngle) {
        for (const auto& poly : g_polygons) {
            if (poly.id == polyID) {
                if (outX) *outX = poly.resX;
                if (outY) *outY = poly.resY;
                if (outAngle) *outAngle = poly.resAngle;
                return 1; // Encontrado
            }
        }
        return 0; // No encontrado
    }

    // 5. Limpiar memoria al finalizar
    DLLEXPORT void __stdcall FreeEngine() {
        g_polygons.clear();
        g_polygons.shrink_to_fit();
    }

}
