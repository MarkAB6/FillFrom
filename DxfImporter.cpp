#include "DxfImporter.h"
#include <QtMath>
#include <QLineF>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

namespace bg = boost::geometry;
using BgPoint   = bg::model::d2::point_xy<double>;
using BgPolygon = bg::model::polygon<BgPoint>;

//  工具：安全追加顶点（跳过与末点重合的点）
void DxfImporter::appendVertex(ImportedContour& c, const QPointF& pt)
{
    if (!c.vertices.isEmpty() &&
        QLineF(c.vertices.last(), pt).length() <= kEps)
        return;
    c.vertices.push_back(pt);
}
//  圆弧离散：给定圆心/半径/起止角（度），返回均匀采样点序列
QVector<QPointF> DxfImporter::discretizeArc(double cx, double cy, double r,
                                            double startDeg, double endDeg,
                                            bool ccw, int segments)
{
    double span = endDeg - startDeg;
    if (ccw) { if (span <= 0.0) span += 360.0; }
    else     { if (span >= 0.0) span -= 360.0; }

    QVector<QPointF> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double t   = static_cast<double>(i) / segments;
        const double rad = qDegreesToRadians(startDeg + t * span);
        pts.push_back(QPointF(cx + r * qCos(rad), cy + r * qSin(rad)));
    }
    return pts;
}

//  bulge 圆弧离散

QVector<QPointF> DxfImporter::discretizeBulge(const QPointF& p1, const QPointF& p2,
                                              double bulge, int segments)
{
    // 退化：零长度线段
    if (QLineF(p1, p2).length() < kEps)
        return { p1 };

    // 退化：bulge ≈ 0，视为直线段
    if (qAbs(bulge) < kEps)
        return { p1, p2 };

    const double chord    = QLineF(p1, p2).length();
    const double theta    = 4.0 * qAtan(bulge);
    const double absTheta = qAbs(theta);

    // 退化：极小包角
    if (absTheta < kEps || qAbs(qSin(absTheta * 0.5)) < kEps)
        return { p1, p2 };

    // 求圆心：从弦中点沿弦法线偏移
    const double dx = p2.x() - p1.x();
    const double dy = p2.y() - p1.y();
    const QPointF mid(( p1.x() + p2.x()) * 0.5,
                      ( p1.y() + p2.y()) * 0.5);

    const double nx     = -dy / chord;           // 弦法向量（单位）
    const double ny     =  dx / chord;
    const double offset = chord / (2.0 * qTan(absTheta * 0.5));
    const double sign   = (bulge > 0.0) ? 1.0 : -1.0;

    const QPointF center(mid.x() + nx * offset * sign,
                         mid.y() + ny * offset * sign);

    const double radius   = chord / (2.0 * qSin(absTheta * 0.5));
    const double startDeg = qRadiansToDegrees(qAtan2(p1.y() - center.y(),
                                                     p1.x() - center.x()));
    const double endDeg   = qRadiansToDegrees(qAtan2(p2.y() - center.y(),
                                                     p2.x() - center.x()));

    if (segments <= 0)
        segments = qMax(4, static_cast<int>(qCeil(qRadiansToDegrees(absTheta) / kArcStepDeg)));

    return discretizeArc(center.x(), center.y(), radius,
                         startDeg, endDeg, bulge > 0.0, segments);
}


ImportedContour DxfImporter::validateContour(ImportedContour c)
{
    if (c.vertices.size() < 3) {
        c.isValid       = false;
        c.isFillable    = false;
        c.invalidReason = QStringLiteral("Too few vertices (need >= 3)");
        return c;
    }

    QVector<QPointF> clean;
    clean.reserve(c.vertices.size());
    clean.push_back(c.vertices.first());
    for (int i = 1; i < c.vertices.size(); ++i) {
        if (QLineF(clean.last(), c.vertices[i]).length() > kDedupTol)
            clean.push_back(c.vertices[i]);
    }
    // 去除首尾重合
    while (clean.size() >= 2 &&
           QLineF(clean.first(), clean.last()).length() <= kDedupTol)
        clean.pop_back();

    if (clean.size() < 3) {
        c.isValid       = false;
        c.isFillable    = c.isClosed;
        c.invalidReason = QStringLiteral("Degenerate contour after deduplication");
        return c;
    }
    c.vertices = std::move(clean);


	// 使用 Boost.Geometry 验证轮廓有效性
   BgPolygon poly;
    poly.outer().reserve(static_cast<std::size_t>(c.vertices.size() + 1));
    for (const QPointF& p : c.vertices)
        bg::append(poly.outer(), BgPoint(p.x(), p.y()));

    const double headTailDist = QLineF(c.vertices.first(), c.vertices.last()).length();
    if (c.isClosed || headTailDist < kCloseTol) {
        // 确保 BgPolygon 首尾相同（仅在尚未闭合时补点）
        if (headTailDist > 0.0)
            bg::append(poly.outer(), BgPoint(c.vertices.first().x(),
                                             c.vertices.first().y()));
        c.isClosed = true;
    }
    bg::correct(poly);

    std::string reason;
    c.isValid = bg::is_valid(poly, reason);
    if (!c.isValid)
        c.invalidReason = QString::fromStdString(reason);

    c.isFillable = c.isClosed;
    return c;
}

//  散乱 LINE 实体 → 拼接轮廓链
void DxfImporter::chainLineSegments()
{
    if (m_rawLines.isEmpty()) return;

    QVector<bool> used(m_rawLines.size(), false);

    // 逐条取未用线段作为种子，向两端延伸形成链
    for (int seed = 0; seed < m_rawLines.size(); ++seed) {
        if (used[seed]) continue;

        QVector<QPointF> chain = { m_rawLines[seed].first,
                                   m_rawLines[seed].second };
        used[seed] = true;

        // 向尾端延伸
        bool extended = true;
        while (extended) {
            extended = false;
            const QPointF tail = chain.last();
            for (int i = 0; i < m_rawLines.size(); ++i) {
                if (used[i]) continue;
                if (QLineF(tail, m_rawLines[i].first).length() < kCloseTol) {
                    chain.push_back(m_rawLines[i].second);
                    used[i] = true; extended = true; break;
                }
                if (QLineF(tail, m_rawLines[i].second).length() < kCloseTol) {
                    chain.push_back(m_rawLines[i].first);
                    used[i] = true; extended = true; break;
                }
            }
        }

        // 向头端延伸
        extended = true;
        while (extended) {
            extended = false;
            const QPointF head = chain.first();
            for (int i = 0; i < m_rawLines.size(); ++i) {
                if (used[i]) continue;
                if (QLineF(head, m_rawLines[i].first).length() < kCloseTol) {
                    chain.push_front(m_rawLines[i].second);
                    used[i] = true; extended = true; break;
                }
                if (QLineF(head, m_rawLines[i].second).length() < kCloseTol) {
                    chain.push_front(m_rawLines[i].first);
                    used[i] = true; extended = true; break;
                }
            }
        }

        ImportedContour c;
        c.vertices = std::move(chain);
        c.isClosed = (QLineF(c.vertices.first(), c.vertices.last()).length() < kCloseTol);
        m_contours.push_back(validateContour(std::move(c)));
    }
}

template<typename TVertList>
ImportedContour DxfImporter::buildPolylineContour(const TVertList& vertlist,
                                                  bool closed) const
{
    ImportedContour c;
    c.isClosed = closed;

    const int count     = static_cast<int>(vertlist.size());
    const int edgeCount = closed ? count : (count - 1);

    for (int i = 0; i < edgeCount; ++i) {
        const auto& v1 = vertlist[static_cast<std::size_t>(i)];
        const auto& v2 = vertlist[static_cast<std::size_t>((i + 1) % count)];

        const QPointF p1 = v1.toQPointF();
        const QPointF p2 = v2.toQPointF();

        for (const QPointF& pt : discretizeBulge(p1, p2, v1.bulge))
            appendVertex(c, pt);
    }

    // 极端退化：回退到直接使用原始顶点
    if (!closed && c.vertices.isEmpty()) {
        for (const auto& v : vertlist)
            c.vertices.push_back(v.toQPointF());
    }

    return c;
}

//  实体回调
void DxfImporter::addLine(const DRW_Line& data)
{
    ++m_totalEntities;
    m_rawLines.push_back(qMakePair(QPointF(data.basePoint.x, data.basePoint.y),
                                   QPointF(data.secPoint.x,  data.secPoint.y)));
}

void DxfImporter::addArc(const DRW_Arc& data)
{
    ++m_totalEntities;
    const QVector<QPointF> pts = discretizeArc(
        data.basePoint.x, data.basePoint.y, data.radious,
        qRadiansToDegrees(data.staangle),
        qRadiansToDegrees(data.endangle),
        /*ccw=*/true);

    for (int i = 0; i < pts.size() - 1; ++i)
        m_rawLines.push_back(qMakePair(pts[i], pts[i + 1]));
}

void DxfImporter::addCircle(const DRW_Circle& data)
{
    ++m_totalEntities;
    ImportedContour c;
    c.vertices = discretizeArc(data.basePoint.x, data.basePoint.y,
                               data.radious, 0.0, 360.0,
                               /*ccw=*/true, kCircleSeg);
    c.isClosed = true;
    m_contours.push_back(validateContour(std::move(c)));
}

void DxfImporter::addLWPolyline(const DRW_LWPolyline& data)
{
    ++m_totalEntities;
    if (data.vertlist.empty()) return;

    // 为 DRW_Vertex2D 提供统一的 toQPointF() 接口（局部包装）
    struct Wrap {
        const DRW_Vertex2D* v;
        double bulge;//弧度
		QPointF toQPointF() const { return { v->x, v->y }; }//坐标转换
    };
    QVector<Wrap> wrapped;
    wrapped.reserve(static_cast<int>(data.vertlist.size()));
    for (const auto& sp : data.vertlist)
        wrapped.push_back({ sp.get(), sp->bulge });

    m_contours.push_back(validateContour(buildPolylineContour(wrapped, data.flags & 1)));
}

void DxfImporter::addPolyline(const DRW_Polyline& data)
{
    ++m_totalEntities;
    if (data.vertlist.empty()) return;

    struct Wrap {
        const DRW_Vertex* v;
        double bulge;
        QPointF toQPointF() const { return { v->basePoint.x, v->basePoint.y }; }
    };
    QVector<Wrap> wrapped;
    wrapped.reserve(static_cast<int>(data.vertlist.size()));
    for (const auto& sp : data.vertlist)
        wrapped.push_back({ sp.get(), sp->bulge });

    m_contours.push_back(validateContour(buildPolylineContour(wrapped, data.flags & 1)));
}

void DxfImporter::addSpline(const DRW_Spline* data)
{
    if (!data) return;
    ++m_totalEntities;
    ImportedContour c;
    c.vertices.reserve(static_cast<int>(data->controllist.size()));
    for (const auto& v : data->controllist)
        c.vertices.push_back(QPointF(v->x, v->y));
    c.isClosed = (data->flags & 1);
    m_contours.push_back(validateContour(std::move(c)));
}

DxfImportResult DxfImporter::importFile(const QString& filePath)
{
    m_contours.clear();
    m_rawLines.clear();
    m_totalEntities = 0;

    dxfRW reader(filePath.toLocal8Bit().constData());
    const bool ok = reader.read(this, true);

    chainLineSegments();

    DxfImportResult result;
    result.success       = ok;
    result.contours      = m_contours;
    result.totalEntities = m_totalEntities;

    for (const ImportedContour& c : m_contours) {
        if (c.isValid)    ++result.validContours;
        if (c.isFillable) ++result.fillableContours;
    }

    result.message = ok
        ? QString("Import succeeded\n"
                  "Total entities : %1\n"
                  "Valid contours : %2\n"
                  "Fillable contours: %3")
              .arg(result.totalEntities)
              .arg(result.validContours)
              .arg(result.fillableContours)
        : QStringLiteral("DXF import failed.");

    return result;
}