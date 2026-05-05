#include "FillPreviewWidget.h"
#include "DxfImporter.h"
#include <QPainter>
#include <QtMath>
#include <QTransform>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QtConcurrent>
#include <QFuture>

//坐标空间转换 
static PathD MapOpenPathToClipper(const QTransform& tf, const QVector<QPointF>& points)
{
    PathD result;
    result.reserve(static_cast<size_t>(points.size()));
    for (const QPointF& pt : points) {
        QPointF mapped = tf.map(pt);
        result.push_back(PointD(mapped.x(), mapped.y()));
    }
    return result;
}

static PathsD QtPathToClipper(const QPainterPath& path)
{
    PathsD result;
    for (const QPolygonF& poly : path.toSubpathPolygons()) {
        PathD cPath;
        for (const QPointF& pt : poly)
            cPath.push_back(PointD(pt.x(), pt.y()));
        result.push_back(cPath);
    }
    return result;
}

static QPainterPath ClipperToQtClosedPath(const PathsD& paths)
{
    QPainterPath result;
    for (const PathD& path : paths) {
        if (path.empty()) continue;
        QPainterPath sub;
        sub.moveTo(path[0].x, path[0].y);
        for (size_t i = 1; i < path.size(); ++i)
            sub.lineTo(path[i].x, path[i].y);
        sub.closeSubpath();
        result.addPath(sub);
    }
    return result;
}

static QPainterPath ClipperToQtOpenPath(const PathsD& paths)
{

    QPainterPath result;
    for (const PathD& path : paths) {
        if (path.size() < 2) continue; // 只有1个点的情况下无法绘制出线段，修改为空判断为小于2
        // 直接在结果上进行 moveTo 和 lineTo 可以提升性能并避免子路径丢失
        result.moveTo(path[0].x, path[0].y);
        for (size_t i = 1; i < path.size(); ++i) {
            result.lineTo(path[i].x, path[i].y);
        }
    }
    return result;
}

FillPreviewWidget::FillPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Filable preview"));
    setMinimumSize(600, 460);

    m_shape1.addRect(100, 100, 200, 150);
    m_shape2.addEllipse(200, 150, 150, 150);

    m_importBtn = new QPushButton(tr("Import DXF"), this);
    m_importBtn->setFixedSize(90, 28);
    m_importBtn->move(8, 8);
    m_importBtn->setCursor(Qt::PointingHandCursor);
    connect(m_importBtn, &QPushButton::clicked, this, &FillPreviewWidget::onImportDxf);

    connect(&m_watcher, &QFutureWatcher<QList<QPainterPath>>::finished, this, [this]() {
        m_cachedFillPath = m_watcher.result();
        m_isCalculating = false;
        update();
		});

    // 初始化时直接将大小设为3，预留满3个图层单元的位置
    m_data.resize(3);
    for (int i = 0; i < 3; ++i) {
        m_data[i].enable = false;
        m_data[i].fillType = i;
    }
}

void FillPreviewWidget::startAsyncCompute()
{
    m_isCalculating = true;
    update();
    
    // 返回 QList<QPainterPath> 的计算任务
    QFuture<QList<QPainterPath>> future = QtConcurrent::run([this]() -> QList<QPainterPath> {
        return this->generateFillPath();
    });

    m_watcher.setFuture(future);
}

FillPreviewWidget::~FillPreviewWidget()
{

}

void FillPreviewWidget::applyFillList(const QList<FillData>& dataList)
{
    // 直接全量替换，FillFrom 内部已经处理好了 3 个层级的逻辑合并
    m_data = dataList;

    m_hasFill = false;
    for (const FillData& d : m_data) {
        if (d.enable && d.fillType != -1) {
            m_hasFill = true;
            break;
        }
    }

    if (m_hasFill) {
        startAsyncCompute();
    } else {
        m_cachedFillPath.clear();
        update();
    }
}

void FillPreviewWidget::connectToForm(FillFrom* form)
{
    if (!form) return;
    connect(form, &FillFrom::fillConfirmed, this, [this](const QList<FillData>& dataList) {
        applyFillList(dataList);
        raise();
        activateWindow();
    });
    connect(form, &FillFrom::fillDeleted, this, [this]() {
        QList<FillData> emptyList;
        emptyList.resize(3);
        for(int i = 0; i < 3; ++i) {
            emptyList[i].enable = false;
            emptyList[i].fillType = i;
        }
        applyFillList(emptyList);
    });
}

void FillPreviewWidget::onImportDxf()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择 DXF 文件"),
        QString(),
        tr("DXF 文件 (*.dxf);;所有文件 (*)")
    );
    if (filePath.isEmpty()) return;

    DxfImporter importer;
    DxfImportResult res = importer.importFile(filePath);

    if (!res.success) {
        QMessageBox::critical(this, tr("Failure"), res.message);
        return;
    }

    m_importedContours = res.contours;
    m_hasDxf = !m_importedContours.isEmpty();
    update();

    QMessageBox::information(this, tr("导入结果"), res.message);
}


PathsD FillPreviewWidget::buildDxfBasePolygon() const
{
    PathsD result;
    for (const ImportedContour& c : m_importedContours) {
        if (!c.isFillable) continue;
        PathD path;
        for (const QPointF& p : c.vertices)
            path.push_back(PointD(p.x(), p.y()));
        result.push_back(path);
    }
    return result;
}

PathsD FillPreviewWidget::buildBasePolygon(const FillData& data) const
{
    if (m_hasDxf) {
        PathsD base = buildDxfBasePolygon();
        return data.objectCalculation ? Union(base, FillRule::EvenOdd) : base;
    }

    PathsD p1 = QtPathToClipper(m_shape1);
    PathsD p2 = QtPathToClipper(m_shape2);
    if (data.objectCalculation)
        return Union(p1, p2, FillRule::EvenOdd);

    PathsD combined;
    combined.insert(combined.end(), p1.begin(), p1.end());
    combined.insert(combined.end(), p2.begin(), p2.end());
    return combined;
}

PathsD FillPreviewWidget::buildClipPolygon(const PathsD& base, const FillData& data) const
{
    const double totalMargin =
        (data.margin + data.boundaryLoops * data.pitch) * 10.0;
    if (totalMargin <= 0.0)
        return base;

    if (data.objectCalculation)
        return InflatePaths(base, -totalMargin, JoinType::Miter, EndType::Polygon);

    if (data.keepIndependent) {
        // 独立对象逐个偏移
        PathsD result;
        for (const PathD& path : base) {
            PathsD single = { path };
            PathsD inflated = InflatePaths(single, -totalMargin, JoinType::Miter, EndType::Polygon);
            result.insert(result.end(), inflated.begin(), inflated.end());
        }
        return result;
    } else {
        // 参数绑定：以整体的形式直接作偏移切割
        return InflatePaths(base, -totalMargin, JoinType::Miter, EndType::Polygon);
    }
}


PathsD FillPreviewWidget::generateHatchLines(const PathsD& clipPolygon, double angleDeg,const FillData& data) const
{
    PathsD hatchLines;
    if (clipPolygon.empty()) return hatchLines;

    RectD origBounds = GetBounds(clipPolygon);
    const double cx = (origBounds.left + origBounds.right) / 2.0;
    const double cy = (origBounds.top  + origBounds.bottom) / 2.0;

    // 旋转反变换：用于在旋转空间中计算包围范围
    QTransform tfInv;
    tfInv.translate(cx, cy);
    tfInv.rotate(-angleDeg);
    tfInv.translate(-cx, -cy);

    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    bool hasPts = false;

    for (const PathD& path : clipPolygon) {
        for (const PointD& pt : path) {
            hasPts = true;
            double rx = 0.0, ry = 0.0;
            tfInv.map(pt.x, pt.y, &rx, &ry);
            minX = std::min(minX, rx);  maxX = std::max(maxX, rx);
            minY = std::min(minY, ry);  maxY = std::max(maxY, ry);
        }
    }
    if (!hasPts) return hatchLines;

    const double startY = minY + data.startOffset * 1.0;
    const double endY   = maxY - data.endOffset * 1.0;
    if (startY >= endY) return hatchLines;

    // 留出微小边界惯量，避免线段恰好落在边界上
    const double inset       = 0.01;
    double usableStart = startY + inset;
    double usableEnd   = endY   - inset;
    if (usableStart > usableEnd) { usableStart = startY; usableEnd = endY; }

    const double usableRange = usableEnd - usableStart;
    if (usableRange < 0.0) return hatchLines;

    // 根据模式确定行数与间距
    int    linesToGenerate = 0;
    double spacing         = 0.0;
    double firstY          = usableStart;

    if (data.averageDistribute) {
        if (data.lineSpacing <= 0.0) return hatchLines;

        const double base = qMax(0.1, data.lineSpacing * 10.0);
        linesToGenerate = qMax(1, static_cast<int>(qFloor(usableRange / base)) + 1);
        if (linesToGenerate == 1)
            firstY = (usableStart + usableEnd) * 0.5;
        else
            spacing = usableRange / (linesToGenerate - 1);
    } else {
        if (data.lineSpacing <= 0.0) return hatchLines;

        spacing = qMax(0.001, data.lineSpacing * 10.0);  // 不再强制用2.0兜底，但保留0.1防死循环
        linesToGenerate = static_cast<int>(qFloor(usableRange / spacing)) + 1;
    }

    // 正变换：将旋转空间中的线段映射回世界空间
    QTransform tf;
    tf.translate(cx, cy);
    tf.rotate(angleDeg);
    tf.translate(-cx, -cy);

    const double indent  = data.straightIndent * 10.0;
    bool         toRight = true;

    for (int i = 0; i < linesToGenerate; ++i) {
        const double y = firstY + i * spacing;
        if (!data.averageDistribute) {
            if (y > usableEnd + 1e-6) break;
        }

        const double xL = minX + indent;
        const double xR = maxX - indent;
        if (xL >= xR) continue;

        // 生成点模式 后续加入锯齿或其他样式
        QVector<QPointF> points;
        if (data.fillType == 1) {
            // 往返锯齿填充
            if (toRight) {
                points << QPointF(xL, y) << QPointF(xR, y);
            } else {
                points << QPointF(xR, y) << QPointF(xL, y);
                if (i > 0) points << QPointF(xR, y - spacing);
            }
            toRight = !toRight;
        } else {
            points << QPointF(xL, y) << QPointF(xR, y);
        }

        PathD mapped = MapOpenPathToClipper(tf, points);
        if (mapped.size() >= 2)
            hatchLines.push_back(mapped);
    }

    return hatchLines;
}

QPainterPath FillPreviewWidget::clipHatchToPath(const PathsD& clipPolygon, double angleDeg,const FillData& data) const
{
    PathsD lines = generateHatchLines(clipPolygon, angleDeg,data);
    if (data.crossFill) {
        PathsD cross = generateHatchLines(clipPolygon, angleDeg + 90.0, data);
        lines.insert(lines.end(), cross.begin(), cross.end());
    }

    ClipperD clipper;
    //它们不是封闭多边形，因此作为 open subject 添加
    clipper.AddOpenSubject(lines);
    clipper.AddClip(clipPolygon);

    PathsD openResult, closedResult;
    clipper.Execute(ClipType::Intersection, FillRule::EvenOdd, closedResult, openResult);
    return ClipperToQtOpenPath(openResult);
}

QList<QPainterPath> FillPreviewWidget::generateFillPath() const
{
    QList<QPainterPath> results;

    // 分图层单元处理缓存路径（按照填充 1 -> 2 -> 3顺序）
    for (const FillData& data : m_data) {
        if (!m_hasFill || !data.enable || data.fillType == -1) {
            results.append(QPainterPath());
            continue;
        }

        const double angle = data.autoRotate ? data.rotationAngle : data.angle;
        QPainterPath resultPath;

        if (data.objectCalculation) {
            PathsD clip = buildClipPolygon(buildBasePolygon(data), data);
            resultPath.addPath(clipHatchToPath(clip, angle, data));
            results.append(resultPath);
            continue;
        }

        if (data.keepIndependent) {
            // 当前单元独立管控：解绑边界组合单独切割
            if (!m_hasDxf) {
                for (const PathsD& singleBase : { QtPathToClipper(m_shape1), QtPathToClipper(m_shape2) }) {
                    resultPath.addPath(clipHatchToPath(buildClipPolygon(singleBase, data), angle, data));
                }
            } else {
                for (const PathD& path : buildDxfBasePolygon()) {
                    PathsD single = { path };
                    resultPath.addPath(clipHatchToPath(buildClipPolygon(single, data), angle, data));
                }
            }
        } else {
            // 全局基础绑定
            PathsD base = buildBasePolygon(data);
            PathsD clip = buildClipPolygon(base, data);
            resultPath.addPath(clipHatchToPath(clip, angle, data));
        }
        results.append(resultPath);
    }
    
    return results;
}

QTransform FillPreviewWidget::computeViewTransform() const
{
    if (!m_hasDxf || m_importedContours.isEmpty())
        return QTransform();

    QRectF bounds;
    bool first = true;
    for (const ImportedContour& c : m_importedContours) {
        for (const QPointF& pt : c.vertices) {
            if (first) { bounds = QRectF(pt, pt); first = false; }
            else {
                bounds.setLeft  (qMin(bounds.left(),   pt.x()));
                bounds.setRight (qMax(bounds.right(),  pt.x()));
                bounds.setTop   (qMin(bounds.top(),    pt.y()));
                bounds.setBottom(qMax(bounds.bottom(), pt.y()));
            }
        }
    }
    if (bounds.width() <= 0 || bounds.height() <= 0)
        return QTransform();

    const double margin   = 20.0;
    const double scale    = qMin((width()  - margin * 2) / bounds.width(),
                                 (height() - margin * 2) / bounds.height());
    const double screenCx = width()  / 2.0;
    const double screenCy = height() / 2.0;

    QTransform tf;
    tf.translate(screenCx, screenCy);
    tf.scale(scale, -scale);           // Y 轴翻转：CAD 坐标系向上为正
    tf.translate(-bounds.center().x(), -bounds.center().y());
    return tf;
}

int FillPreviewWidget::countFillableContours() const
{
    int n = 0;
    for (const ImportedContour& c : m_importedContours)
        if (c.isFillable) ++n;
    return n;
}

void FillPreviewWidget::paintContours(QPainter& painter) const
{
    if (m_hasDxf) {
        for (const ImportedContour& c : m_importedContours) {
            if (c.vertices.size() < 2) continue;

            QPainterPath path;
            path.moveTo(c.vertices.first());
            for (int i = 1; i < c.vertices.size(); ++i)
                path.lineTo(c.vertices[i]);
            if (c.isClosed) path.closeSubpath();

            if (c.isFillable)
                painter.setPen(QPen(QColor(0, 160, 0), 0));
            else if (c.isClosed && !c.isValid)
                painter.setPen(QPen(QColor(220, 120, 0), 0, Qt::DashLine));
            else
                painter.setPen(QPen(Qt::darkGray, 0, Qt::DotLine));

            painter.drawPath(path);
        }
    } else {
        painter.setPen(QPen(Qt::lightGray, 1, Qt::DashLine));
        painter.drawPath(m_shape1);
        painter.drawPath(m_shape2);
    }
}

void FillPreviewWidget::paintBoundaryLoops(QPainter& painter,
                                            const PathsD& base,
                                            const FillData& data,
                                            const QColor& color) const
{
    painter.setPen(QPen(color, 0));
    for (int i = 1; i <= data.boundaryLoops; ++i) {
        const double offset = -qMax(1.0, data.pitch * 10.0) * i;

        if (data.objectCalculation) {
            PathsD r = InflatePaths(base, offset, JoinType::Miter, EndType::Polygon);
            painter.drawPath(ClipperToQtClosedPath(r));
            continue;
        }

        if (data.keepIndependent) {
            if (m_hasDxf) {
                for (const PathD& path : base) {
                    PathsD single = { path };
                    painter.drawPath(ClipperToQtClosedPath(
                        InflatePaths(single, offset, JoinType::Miter, EndType::Polygon)));
                }
            } else {
                for (const PathsD& src : { QtPathToClipper(m_shape1), QtPathToClipper(m_shape2) }) {
                    PathsD r = InflatePaths(src, offset, JoinType::Miter, EndType::Polygon);
                    if (!r.empty()) painter.drawPath(ClipperToQtClosedPath(r));
                }
            }
        } else {
            // 参数绑定计算，作为统一组合偏移
            PathsD r = InflatePaths(base, offset, JoinType::Miter, EndType::Polygon);
            painter.drawPath(ClipperToQtClosedPath(r));
        }
    }
}

void FillPreviewWidget::paintStatusText(QPainter& painter) const
{
    painter.setPen(Qt::darkGray);

    if (m_isCalculating) {
        painter.setPen(Qt::red);
        painter.drawText(8, height() - 30, tr("Calculating dense hatch... Please wait."));
        painter.setPen(Qt::darkGray);
    }
    if (!m_hasFill) {
        painter.drawText(8, height() - 10,
            m_hasDxf
            ? QString("import DXF | Contour: %1 | fillable: %2")
                .arg(m_importedContours.size())
                .arg(countFillableContours())
            : QStringLiteral("Test graphic mode — Click 'Import DXF' to load the file"));
    } else {
        int activeCount = 0;
        for (const auto& d : m_data) if (d.enable) activeCount++;
        
        painter.drawText(8, height() - 10,
            QString("Active Layers Stacked: %1 | %2")
                .arg(activeCount)
                .arg(m_hasDxf ? "DXF Model" : "TEXT Model"));
    }
}

void FillPreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    painter.save();
    if (m_hasDxf)
        painter.setTransform(computeViewTransform());

    paintContours(painter);
    if (m_hasFill)
    {
        // 保证强制按层级顺序 [0] -> [1] -> [2] 从底层图涂覆到顶层
        for (int i = 0; i < m_data.size(); ++i) {
            const FillData& currentData = m_data[i];
            if (!currentData.enable || currentData.fillType == -1) continue;

            const QColor  penColor(currentData.penColor);
            const PathsD  base = buildBasePolygon(currentData);

            // 绘制当前层的边界环
            paintBoundaryLoops(painter, base, currentData, penColor);

            // 取对应槽位的填充缓存路径
            QPainterPath cachedPath;
            if (i < m_cachedFillPath.size() && !m_isCalculating)
                cachedPath = m_cachedFillPath[i];
                
            // 按当前层级的填充次数顺序涂涂覆叠加
            for (int loop = 0; loop < currentData.processCount; ++loop) {
                if (currentData.enableProfile) {
                    if (currentData.profileIconIndex == 0) {
                        // 先填充，后轮廓
                        painter.setPen(QPen(penColor, 0));
                        if (!cachedPath.isEmpty()) painter.drawPath(cachedPath);

                        painter.setPen(QPen(currentData.walkAround ? Qt::blue : penColor, 0));
                        painter.drawPath(ClipperToQtClosedPath(base));
                    }
                    else {
                        // 先轮廓，后填充
                        painter.setPen(QPen(currentData.walkAround ? Qt::blue : penColor, 0));
                        painter.drawPath(ClipperToQtClosedPath(base));

                        painter.setPen(QPen(penColor, 0));
                        if (!cachedPath.isEmpty()) painter.drawPath(cachedPath);
                    }
                }
                else {
                    if (currentData.walkAround) {
                        painter.setPen(QPen(Qt::blue, 0));
                        painter.drawPath(ClipperToQtClosedPath(base));
                    }
                    painter.setPen(QPen(penColor, 0));
                    if (!cachedPath.isEmpty()) painter.drawPath(cachedPath);
                }
            }
        }
    }
    
    painter.restore();
    paintStatusText(painter);
}

