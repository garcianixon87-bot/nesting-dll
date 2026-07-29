#include <windows.h>
#include <vector>
#include <cmath>
#include <algorithm>

struct Point {
    double x, y;
};

struct ItemPolygon {
    long id;
    std::vector<Point> originalVertices;
    bool allowRotation;
    double resX, resY, resAngle;
};

static std::vector<ItemPolygon> g_polygons;
static double g_margin = 0.0;
static double g_stepAngle = 90.0; // Grados de rotación a probar (ej. 0, 90, 180, 270)

// -----------------------------------------------------------------------------
// FUNCIONES GEOMÉTRICAS AUXILIARES
// -----------------------------------------------------------------------------

// Rotar un punto 'p' alrededor del origen
Point RotatePoint(Point p, double angleDegrees) {
    double rad = angleDegrees * 3.14159265358979323846 / 180.0;
    double c = cos(rad);
    double s = sin(rad);
    return { p.x * c - p.y * s, p.x * s + p.y * c };
}

// Obtener los vértices transformados (rotados y trasladados)
std::vector<Point> GetTransformedVertices(const ItemPolygon& poly, double tx, double ty, double angle) {
    std::vector<Point> result;
    result.reserve(poly.originalVertices.size());
    for (const auto& pt : poly.originalVertices) {
        Point r = RotatePoint(pt, angle);
        result.push_back({ r.x + tx, r.y + ty });
    }
    return result;
}

// Calcular Bounding Box de una lista de puntos
void GetBounds(const std::vector<Point>& pts, double& minX, double& maxX, double& minY, double& maxY) {
    if (pts.empty()) return;
    minX = maxX = pts[0].x;
    minY = maxY = pts[0].y;
    for (const auto& p : pts) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
    }
}

// Comprobar solapamiento de AABB (Axis-Aligned Bounding Box) con margen
bool BoundingBoxesOverlap(double minA_X, double maxA_X, double minA_Y, double maxA_Y,
                          double minB_X, double maxB_X, double minB_Y, double maxB_Y, double margin) {
    if (maxA_X + margin < minB_X || maxB_X + margin < minA_X) return false;
    if (maxA_Y + margin < minB_Y || maxB_Y + margin < minA_Y) return false;
    return true;
}

// -----------------------------------------------------------------------------
// FUNCIONES EXPUESTAS PARA LA DLL
// -----------------------------------------------------------------------------
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
            poly.originalVertices.push_back({ xPts[i], yPts[i] });
        }
        g_polygons.push_back(poly);
        return 1;
    }

    // ALGORITMO DE TIZADA ENCASTADA (TRUE-SHAPE BOTTOM-LEFT DEEP SEARCH)
    __declspec(dllexport) int __stdcall ExecuteNesting(double sheetWidth, double sheetHeight) {
        if (g_polygons.empty()) return 0;

        // Lista de piezas colocada actualmente
        struct PlacedItem {
            std::vector<Point> pts;
            double minX, maxX, minY, maxY;
        };
        std::vector<PlacedItem> placed;

        // Modificadores de ángulo a probar
        std::vector<double> testAngles = { 0.0 };
        if (g_stepAngle > 0) {
            for (double a = g_stepAngle; a < 360.0; a += g_stepAngle) {
                testAngles.push_back(a);
            }
        }

        // Paso de escaneo en milímetros/unidades
        double stepSize = 10.0; // Escaneo continuo progresivo

        for (auto& poly : g_polygons) {
            bool placedSuccessfully = false;
            double bestX = 0, bestY = 0, bestAngle = 0;
            double bestScore = 1e15; // Queremos minimizar la altura y posición Y/X

            // Probar cada ángulo de rotación permitido
            std::vector<double> anglesToTry = poly.allowRotation ? testAngles : std::vector<double>{ 0.0 };

            for (double ang : anglesToTry) {
                // Generar silueta rotada temporal para medir
                std::vector<Point> rotPts = GetTransformedVertices(poly, 0, 0, ang);
                double pMinX, pMaxX, pMinY, pMaxY;
                GetBounds(rotPts, pMinX, pMaxX, pMinY, pMaxY);
                double pW = pMaxX - pMinX;
                double pH = pMaxY - pMinY;

                // Escaneo Bottom-Left en la hoja de tela
                for (double trialY = sheetHeight - pH - g_margin; trialY >= g_margin; trialY -= stepSize) {
                    for (double trialX = g_margin; trialX <= sheetWidth - pW - g_margin; trialX += stepSize) {
                        
                        // Posición real del origen
                        double tx = trialX - pMinX;
                        double ty = trialY - pMinY;

                        double curMinX = trialX;
                        double curMaxX = trialX + pW;
                        double curMinY = trialY;
                        double curMaxY = trialY + pH;

                        // Comprobar colisión con las piezas ya ubicadas
                        bool collision = false;
                        for (const auto& item : placed) {
                            if (BoundingBoxesOverlap(curMinX, curMaxX, curMinY, curMaxY,
                                                     item.minX, item.maxX, item.minY, item.maxY, g_margin)) {
                                collision = true;
                                break;
                            }
                        }

                        if (!collision) {
                            // Criterio de puntuación: Dar prioridad a llenar abajo y a la izquierda
                            double score = (sheetHeight - trialY) * 10.0 + trialX;
                            if (score < bestScore) {
                                bestScore = score;
                                bestX = tx;
                                bestY = ty;
                                bestAngle = ang;
                                placedSuccessfully = true;
                            }
                            // Al encontrar la posición más baja en esta columna, pasamos a evaluar la siguiente
                            break; 
                        }
                    }
                }
            }

            if (placedSuccessfully) {
                poly.resX = bestX;
                poly.resY = bestY;
                poly.resAngle = bestAngle;

                // Guardar la pieza en la lista de colocadas
                std::vector<Point> finalPts = GetTransformedVertices(poly, bestX, bestY, bestAngle);
                double fMinX, fMaxX, fMinY, fMaxY;
                GetBounds(finalPts, fMinX, fMaxX, fMinY, fMaxY);
                placed.push_back({ finalPts, fMinX, fMaxX, fMinY, fMaxY });
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
