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

// 辅助哈希函数，用于快速匹配空间网格
struct PointHash {
    std::size_t operator()(const std::pair<int, int>& p) const {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

void DxfImporter::chainLineSegments()
{
    if (m_rawLines.isEmpty()) return;

    // 1. 构建空间网格哈希以加速查找临近点
    const double gridSize = qMax(kCloseTol * 2.0, 1e-4);
    using GridKey = std::pair<int, int>;
    std::unordered_multimap<GridKey, int, PointHash> spatialGrid;

    auto getGridKey = [gridSize](const QPointF& pt) -> GridKey {
        return { static_cast<int>(std::floor(pt.x() / gridSize)),
                 static_cast<int>(std::floor(pt.y() / gridSize)) };
        };

    QVector<bool> used(m_rawLines.size(), false);
    for (int i = 0; i < m_rawLines.size(); ++i) {
        spatialGrid.insert({ getGridKey(m_rawLines[i].first), i });
        spatialGrid.insert({ getGridKey(m_rawLines[i].second), i });
    }

    auto findConnectedLine = [&](const QPointF& pt) -> int {
        GridKey key = getGridKey(pt);
        // 搜索相邻的 3x3 个网格以应对边界情况
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                GridKey neighborKey = { key.first + dx, key.second + dy };
                auto range = spatialGrid.equal_range(neighborKey);
                for (auto it = range.first; it != range.second; ++it) {
                    int i = it->second;
                    if (used[i]) continue;
                    if (QLineF(pt, m_rawLines[i].first).length() < kCloseTol ||
                        QLineF(pt, m_rawLines[i].second).length() < kCloseTol) {
                        return i;
                    }
                }
            }
        }
        return -1;
        };

    // 2. 拼接轮廓链
    for (int seed = 0; seed < m_rawLines.size(); ++seed) {
        if (used[seed]) continue;

        std::deque<QPointF> chain;
        chain.push_back(m_rawLines[seed].first);
        chain.push_back(m_rawLines[seed].second);
        used[seed] = true;

        // 向两端延伸
        bool extended = true;
        while (extended) {
            extended = false;

            // 尝试延伸队尾
            int tailMatch = findConnectedLine(chain.back());
            if (tailMatch != -1) {
                if (QLineF(chain.back(), m_rawLines[tailMatch].first).length() < kCloseTol)
                    chain.push_back(m_rawLines[tailMatch].second);
                else
                    chain.push_back(m_rawLines[tailMatch].first);
                used[tailMatch] = true;
                extended = true;
            }

            // 尝试延伸队头
            int headMatch = findConnectedLine(chain.front());
            if (headMatch != -1) {
                if (QLineF(chain.front(), m_rawLines[headMatch].first).length() < kCloseTol)
                    chain.push_front(m_rawLines[headMatch].second);
                else
                    chain.push_front(m_rawLines[headMatch].first);
                used[headMatch] = true;
                extended = true;
            }
        }

        ImportedContour c;
        c.vertices = QVector<QPointF>(chain.begin(), chain.end());
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


namespace {
    // De Boor 算法核心求值辅助函数（支持普通 B-Spline 和 NURBS）
    QPointF evaluateSplinePoint(double t, int degree,
        const QVector<QPointF>& cps,
        const QVector<double>& knots,
        const QVector<double>& weights)
    {
        const int n = cps.size();
        const int p = degree;

        // 1. 查找 t 所在的节点区间 k  (满足: knots[k] <= t < knots[k+1])
        int k = p;
        while (k < n && knots[k + 1] <= t) {
            k++;
        }
        if (k >= n) k = n - 1; // 极值容错处理

        QVector<QPointF> d(p + 1);
        QVector<double> w(p + 1, 1.0);
        const bool rational = (weights.size() == n); // 是否有有效权重数组(NURBS)

        // 2. 选取参与当前区间求值的区间控制点（共 p+1 个）
        for (int j = 0; j <= p; ++j) {
            const int idx = k - p + j;
            d[j] = cps[idx];
            if (rational) {
                w[j] = weights[idx];
                d[j] = QPointF(d[j].x() * w[j], d[j].y() * w[j]); // 转换为齐次坐标 (x*w, y*w)
            }
        }

        // 3. 递推降阶求值
        for (int r = 1; r <= p; ++r) {
            for (int j = p; j >= r; --j) {
                const int i = k - p + j;
                const double den = knots[i + p + 1 - r] - knots[i];
                const double alpha = (den == 0.0) ? 0.0 : (t - knots[i]) / den;

                d[j] = QPointF((1.0 - alpha) * d[j - 1].x() + alpha * d[j].x(),
                    (1.0 - alpha) * d[j - 1].y() + alpha * d[j].y());
                if (rational) {
                    w[j] = (1.0 - alpha) * w[j - 1] + alpha * w[j];
                }
            }
        }

        // 4. NURBS需要转换回非齐次坐标
        if (rational && w[p] != 0.0) {
            return QPointF(d[p].x() / w[p], d[p].y() / w[p]);
        }
        return d[p];
    }
}

//void DxfImporter::addSpline(const DRW_Spline* data)
//{
//    if (!data) return;
//    ++m_totalEntities;
//    ImportedContour c;
//    c.vertices.reserve(static_cast<int>(data->controllist.size()));
//    for (const auto& v : data->controllist)
//        c.vertices.push_back(QPointF(v->x, v->y));
//    c.isClosed = (data->flags & 1);
//    m_contours.push_back(validateContour(std::move(c)));
//}

void DxfImporter::addSpline(const DRW_Spline* data)
{
    if (!data) return;
    ++m_totalEntities;

    const int degree = data->degree;
    const int n = static_cast<int>(data->controllist.size());

    // 提取控制点
    QVector<QPointF> cps;
    cps.reserve(n);
    for (const auto& v : data->controllist)
        cps.push_back(QPointF(v->x, v->y));

    // 提取权重(如果 DXF 中带有完整的权重列表作为 NURBS)
    QVector<double> weights;
    if (data->weightlist.size() == static_cast<std::size_t>(n)) {
        for (double w : data->weightlist)
            weights.push_back(w);
    }

    // 提取节点向量（knotlist）
    QVector<double> knots;
    knots.reserve(static_cast<int>(data->knotslist.size()));
    for (double k_val : data->knotslist) {
        knots.push_back(k_val);
    }

    ImportedContour c;
    c.isClosed = (data->flags & 1);

    // 标准 DXF 的 knotlist 尺寸是 n + degree + 1。
    // 如果数据极度缺失、控制点小于2或度数 <= 0，则降级为只连控制点
    if (degree <= 0 || n < 2 || static_cast<int>(knots.size()) < n + degree + 1) {
        c.vertices = cps;
    }
    else {
        // [有效区间] B-Spline 起点和终点通常映射在 knots[degree] 和 knots[n]
        const double uMin = knots[degree];
        const double uMax = knots[n];

        // 决定曲线采样的精细度：此处默认通过一段平滑的密集点实现（比如大致每个控制点分 15~20 份计算）
        const int segments = qMax(64, n * 20);
        c.vertices.reserve(segments + 1);

        for (int i = 0; i <= segments; ++i) {
            double t = uMin + (uMax - uMin) * i / segments;

            // 安全限制避免浮点数漂移越界
            if (t < uMin) t = uMin;
            if (t > uMax) t = uMax;

            QPointF pt = evaluateSplinePoint(t, degree, cps, knots, weights);

            // 对于头尾或者距离很近的点复用已有的跳出逻辑
            if (i > 0 && QLineF(c.vertices.last(), pt).length() <= kEps) {
                continue;
            }
            c.vertices.push_back(pt);
        }
    }

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