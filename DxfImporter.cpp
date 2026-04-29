#include "DxfImporter.h"
#include <QtMath>
#include <QLineF>

// Boost.Geometry
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

namespace bg = boost::geometry;
using BgPoint   = bg::model::d2::point_xy<double>;
using BgPolygon = bg::model::polygon<BgPoint>;

// ─────────────────────────────────────────────────────────────────────────────
// Boost.Geometry 校验 + 标记
// ─────────────────────────────────────────────────────────────────────────────
ImportedContour DxfImporter::validateContour(ImportedContour c)
{
    if (c.vertices.size() < 3) {
        c.isValid    = false;
        c.isFillable = false;
        c.invalidReason = QStringLiteral("Insufficient number of vertices 3");
        return c;
    }

    BgPolygon poly;
    for (const QPointF& p : c.vertices)
        bg::append(poly.outer(), BgPoint(p.x(), p.y()));

    // Boost 要求多边形首尾相同
    const QPointF& first = c.vertices.first();
    const QPointF& last  = c.vertices.last();
    if (QLineF(first, last).length() > 1e-4)
        bg::append(poly.outer(), BgPoint(first.x(), first.y()));

    bg::correct(poly);   // 修正方向

    std::string reason;
    c.isValid = bg::is_valid(poly, reason);
    if (!c.isValid)
        c.invalidReason = QString::fromStdString(reason);

    c.isFillable = c.isClosed && c.isValid;
    return c;
}

// ─────────────────────────────────────────────────────────────────────────────
// 圆弧离散
// ─────────────────────────────────────────────────────────────────────────────
QVector<QPointF> DxfImporter::discretizeArc(double cx, double cy, double r,
                                             double startDeg, double endDeg,
                                             bool ccw, int segments)
{
    QVector<QPointF> pts;
    // 规范化角度范围
    double span = endDeg - startDeg;
    if (ccw) {
        if (span <= 0.0) span += 360.0;
    } else {
        if (span >= 0.0) span -= 360.0;
    }
    for (int i = 0; i <= segments; ++i) {
        double t   = static_cast<double>(i) / segments;
        double deg = startDeg + t * span;
        double rad = qDegreesToRadians(deg);
        pts.push_back(QPointF(cx + r * qCos(rad), cy + r * qSin(rad)));
    }
    return pts;
}

// ─────────────────────────────────────────────────────────────────────────────
// 将散乱 LINE 实体按端点容差拼接成闭合轮廓
// ─────────────────────────────────────────────────────────────────────────────
void DxfImporter::chainLineSegments()
{
    if (m_rawLines.isEmpty()) return;

    const double tol = 1e-4;
    QVector<bool> used(m_rawLines.size(), false);

    while (true) {
        // 找第一条未使用的线段作为种子
        int seed = -1;
        for (int i = 0; i < m_rawLines.size(); ++i) {
            if (!used[i]) { seed = i; break; }
        }
        if (seed < 0) break;

        QVector<QPointF> chain;
        chain.push_back(m_rawLines[seed].first);
        chain.push_back(m_rawLines[seed].second);
        used[seed] = true;

        bool extended = true;
        while (extended) {
            extended = false;
            QPointF tail = chain.last();
            for (int i = 0; i < m_rawLines.size(); ++i) {
                if (used[i]) continue;
                if (QLineF(tail, m_rawLines[i].first).length() < tol) {
                    chain.push_back(m_rawLines[i].second);
                    used[i] = true;
                    extended = true;
                    break;
                }
                if (QLineF(tail, m_rawLines[i].second).length() < tol) {
                    chain.push_back(m_rawLines[i].first);
                    used[i] = true;
                    extended = true;
                    break;
                }
            }
        }

        ImportedContour c;
        c.vertices = chain;
        // 判断首尾是否闭合
        c.isClosed = (QLineF(chain.first(), chain.last()).length() < tol);
        m_contours.push_back(validateContour(c));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 实体回调
// ─────────────────────────────────────────────────────────────────────────────
void DxfImporter::addLine(const DRW_Line& data)
{
    ++m_totalEntities;
    QPointF p1(data.basePoint.x, data.basePoint.y);
    QPointF p2(data.secPoint.x,  data.secPoint.y);
    m_rawLines.push_back(qMakePair(p1, p2));
}

void DxfImporter::addArc(const DRW_Arc& data)
{
    ++m_totalEntities;
    ImportedContour c;
    c.vertices  = discretizeArc(data.basePoint.x, data.basePoint.y,
                                data.radious,
                                data.staangle * 180.0 / M_PI,
                                data.endangle * 180.0 / M_PI,
                                true);
    c.isClosed  = false;
    c.isValid   = false;
    c.isFillable = false;
    m_contours.push_back(c);
}

void DxfImporter::addCircle(const DRW_Circle& data)
{
    ++m_totalEntities;
    ImportedContour c;
    c.vertices  = discretizeArc(data.basePoint.x, data.basePoint.y,
                                data.radious, 0.0, 360.0, true, 72);
    c.isClosed  = true;
    m_contours.push_back(validateContour(c));
}

void DxfImporter::addLWPolyline(const DRW_LWPolyline& data)
{
    ++m_totalEntities;
    ImportedContour c;
    for (const std::shared_ptr<DRW_Vertex2D>& v : data.vertlist)
        c.vertices.push_back(QPointF(v->x, v->y));
    c.isClosed = (data.flags & 1);
    m_contours.push_back(validateContour(c));
}

void DxfImporter::addPolyline(const DRW_Polyline& data)
{
    ++m_totalEntities;
    ImportedContour c;
    for (const std::shared_ptr<DRW_Vertex>& v : data.vertlist)
        c.vertices.push_back(QPointF(v->basePoint.x, v->basePoint.y));
    c.isClosed = (data.flags & 1);
    m_contours.push_back(validateContour(c));
}

void DxfImporter::addSpline(const DRW_Spline* data)
{
    if (!data) return;
    ++m_totalEntities;
    ImportedContour c;
    // 直接用控制点近似（若需精确可用 de Boor 算法）
    for (const std::shared_ptr<DRW_Coord>& v : data->controllist)
        c.vertices.push_back(QPointF(v->x, v->y));
    c.isClosed = (data->flags & 1);
    m_contours.push_back(validateContour(c));
}

// ─────────────────────────────────────────────────────────────────────────────
// 主入口
// ─────────────────────────────────────────────────────────────────────────────
DxfImportResult DxfImporter::importFile(const QString& filePath)
{
    m_contours.clear();
    m_rawLines.clear();
    m_totalEntities = 0;

    dxfRW reader(filePath.toLocal8Bit().constData());
    bool ok = reader.read(this, true);

    // 拼接散乱 LINE 实体
    chainLineSegments();

    DxfImportResult result;
    result.success       = ok;
    result.contours      = m_contours;
    result.totalEntities = m_totalEntities;

    for (const ImportedContour& c : m_contours) {
        if (c.isValid)    ++result.validContours;
        if (c.isFillable) ++result.fillableContours;
    }

    if (!ok) {
        result.message = QStringLiteral("DXF import Failure.");
    } else {
        result.message = QString(
            "import Success\n"
            "FinalExist:%1\n"
            "Effective contour:%2\n"
            "fillable outline:%3"
        ).arg(result.totalEntities)
         .arg(result.validContours)
         .arg(result.fillableContours);
    }
    return result;
}