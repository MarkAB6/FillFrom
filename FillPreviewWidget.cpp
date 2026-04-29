#include "FillPreviewWidget.h"
#include "DxfImporter.h"
#include <QPainter>
#include <QtMath>
#include <QTransform>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>

// ── 静态辅助函数（与原文件相同，保留不变）────────────────────────────────
static PathD MapOpenPathToClipper(const QTransform& tf, const QVector<QPointF>& points)
{
    PathD result;
    result.reserve(static_cast<size_t>(points.size()));
    for (const QPointF& pt : points) {
        const QPointF mapped = tf.map(pt);
        result.push_back(PointD(mapped.x(), mapped.y()));
    }
    return result;
}

static PathsD QtPathToClipper(const QPainterPath& path) {
    PathsD result;
    for (const QPolygonF& poly : path.toSubpathPolygons()) {
        PathD cPath;
        for (const QPointF& pt : poly)
            cPath.push_back(PointD(pt.x(), pt.y()));
        result.push_back(cPath);
    }
    return result;
}

static QPainterPath ClipperToQtClosedPath(const PathsD& paths) {
    QPainterPath result;
    for (const auto& path : paths) {
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

static QPainterPath ClipperToQtOpenPath(const PathsD& paths) {
    QPainterPath result;
    for (const auto& path : paths) {
        if (path.empty()) continue;
        QPainterPath sub;
        sub.moveTo(path[0].x, path[0].y);
        for (size_t i = 1; i < path.size(); ++i)
            sub.lineTo(path[i].x, path[i].y);
        result.addPath(sub);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
FillPreviewWidget::FillPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Filable preview"));
    setMinimumSize(600, 460);

    // 测试图形（未导入 DXF 时的后备）
    m_shape1.addRect(100, 100, 200, 150);
    m_shape2.addEllipse(200, 150, 150, 150);

    // 导入按钮（左上角悬浮）
    m_importBtn = new QPushButton(tr("Import DXF"), this);
    m_importBtn->setFixedSize(90, 28);
    m_importBtn->move(8, 8);
    m_importBtn->setCursor(Qt::PointingHandCursor);
    connect(m_importBtn, &QPushButton::clicked, this, &FillPreviewWidget::onImportDxf);
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

void FillPreviewWidget::connectToForm(FillFrom* form)
{
    if (!form) return;
    connect(form, &FillFrom::fillConfirmed, this, [this](const FillData& data) {
        this->applyFill(data);
        this->raise();
        this->activateWindow();
    });
    connect(form, &FillFrom::fillDeleted, this, [this]() {
        FillData empty;
        empty.enable = false;
        this->applyFill(empty);
    });
}

void FillPreviewWidget::applyFill(const FillData& data)
{
    m_data   = data;
    m_hasFill = true;
    update();
}

// ── 从导入轮廓构建可填充多边形 PathsD ────────────────────────────────────────
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

// ── 基础多边形（自动选取数据源）──────────────────────────────────────────────
PathsD FillPreviewWidget::buildBasePolygon() const
{
    if (m_hasDxf) {
        PathsD dxfBase = buildDxfBasePolygon();
        if (m_data.objectCalculation)
            return Union(dxfBase, FillRule::EvenOdd);
        return dxfBase;
    }

    PathsD p1 = QtPathToClipper(m_shape1);
    PathsD p2 = QtPathToClipper(m_shape2);
    if (m_data.objectCalculation)
        return Union(p1, p2, FillRule::EvenOdd);

    PathsD combined;
    combined.insert(combined.end(), p1.begin(), p1.end());
    combined.insert(combined.end(), p2.begin(), p2.end());
    return combined;
}

PathsD FillPreviewWidget::buildClipPolygon(const PathsD& base) const
{
    double totalMargin = (m_data.margin + m_data.boundaryLoops * m_data.pitch) * 10.0;
    if (totalMargin <= 0.0)
        return base;
        
    if (m_data.objectCalculation) {
        // 整体合并偏移
        return InflatePaths(base, -totalMargin, JoinType::Miter, EndType::Polygon);
    }
    else {
        // 独立对象逐个偏移
        PathsD result;
        for (const auto& path : base) {
            PathsD singlePath;
            singlePath.push_back(path);
            PathsD inf = InflatePaths(singlePath, -totalMargin, JoinType::Miter, EndType::Polygon);
            result.insert(result.end(), inf.begin(), inf.end());
        }
        return result;
    }
}

PathsD FillPreviewWidget::generateHatchLines(const PathsD& clipPolygon, double angleDeg) const
{
    PathsD hatchLines;
    if (clipPolygon.empty()) return hatchLines;

    RectD origBounds = GetBounds(clipPolygon);
    double cx = (origBounds.left + origBounds.right) / 2.0;
    double cy = (origBounds.top + origBounds.bottom) / 2.0;

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
            double rx = 0.0;
            double ry = 0.0;
            tfInv.map(pt.x, pt.y, &rx, &ry);
            minX = std::min(minX, rx);
            maxX = std::max(maxX, rx);
            minY = std::min(minY, ry);
            maxY = std::max(maxY, ry);
        }
    }
    if (!hasPts) return hatchLines;

    double startY = minY + m_data.startOffset * 10.0;
    double endY = maxY - m_data.endOffset * 10.0;
    if (startY >= endY) return hatchLines;

    const double boundaryInset = 0.01;
    double usableStartY = startY + boundaryInset;
    double usableEndY = endY - boundaryInset;
    if (usableStartY > usableEndY) {
        usableStartY = startY;
        usableEndY = endY;
    }

    const double usableRange = usableEndY - usableStartY;
    if (usableRange < 0.0) return hatchLines;

    int linesToGenerate = 0;
    double spacing = 0.0;
    double firstY = usableStartY;

    if (m_data.lineCount > 0) {
        linesToGenerate = qMax(1, m_data.lineCount);
        if (linesToGenerate == 1) {
            firstY = (usableStartY + usableEndY) * 0.5;
        } else {
            if (m_data.lineSpacing > 0.0) {
                spacing = qMax(0.1, m_data.lineSpacing * 10.0);
                if (spacing * (linesToGenerate - 1) > usableRange)
                    spacing = usableRange / (linesToGenerate - 1);
            } else {
                spacing = usableRange / (linesToGenerate - 1);
            }
        }
    } else if (m_data.averageDistribute) {
        const double base = qMax(0.1, m_data.lineSpacing * 10.0);
        linesToGenerate = qMax(1, static_cast<int>(qFloor(usableRange / base)) + 1);
        if (linesToGenerate == 1)
            firstY = (usableStartY + usableEndY) * 0.5;
        else
            spacing = usableRange / (linesToGenerate - 1);
    } else {
        spacing = qMax(2.0, m_data.lineSpacing * 10.0);
        linesToGenerate = qMax(1, static_cast<int>(qFloor(usableRange / spacing)) + 1);
        firstY = usableStartY;
    }

    QTransform tf;
    tf.translate(cx, cy);
    tf.rotate(angleDeg);
    tf.translate(-cx, -cy);

    const double indent  = m_data.straightIndent * 10.0;
    bool         toRight = true;

    for (int i = 0; i < linesToGenerate; ++i) {
        double y = firstY + i * spacing;
        if (!(m_data.lineCount > 0 || m_data.averageDistribute)) {
            if (y > usableEndY + 1e-6) break;
        }

        double xL = minX + indent;
        double xR = maxX - indent;
        if (xL >= xR) continue;

        QVector<QPointF> points;
        if (m_data.fillType == 1) {
            if (toRight) {
                points.push_back(QPointF(xL, y));
                points.push_back(QPointF(xR, y));
            } else {
                points.push_back(QPointF(xR, y));
                points.push_back(QPointF(xL, y));
                if (i > 0)
                    points.push_back(QPointF(xR, y - spacing));
            }
            toRight = !toRight;
        } else {
            points.push_back(QPointF(xL, y));
            points.push_back(QPointF(xR, y));
        }

        PathD mappedLine = MapOpenPathToClipper(tf, points);
        if (mappedLine.size() >= 2) {
            hatchLines.push_back(mappedLine);
        }
    }

    return hatchLines;
}

// 核心功能：使用 Clipper2 生成被精确裁剪好的扫描线段
QPainterPath FillPreviewWidget::generateFillPath() const
{
    if (!m_hasFill || !m_data.enable)
        return QPainterPath();
    double angle = m_data.autoRotate ? m_data.rotationAngle : m_data.angle;

    if (m_data.objectCalculation)
    {
        PathsD base = buildBasePolygon();
        PathsD clipPolygon = buildClipPolygon(base);

        PathsD openSubjects = generateHatchLines(clipPolygon, angle);
        if (m_data.crossFill) {
            PathsD cross = generateHatchLines(clipPolygon, angle + 90.0);
            openSubjects.insert(openSubjects.end(), cross.begin(), cross.end());
        }

        ClipperD clipper;
        clipper.AddOpenSubject(openSubjects);
        clipper.AddClip(clipPolygon);

        PathsD openResult, closedResult;
        clipper.Execute(ClipType::Intersection, FillRule::EvenOdd, closedResult, openResult);
        return ClipperToQtOpenPath(openResult);
    }
     else
     {
        QPainterPath finalResultPath;
        if (!m_hasDxf)
        {
            PathsD p1 = QtPathToClipper(m_shape1);
            PathsD p2 = QtPathToClipper(m_shape2);
            PathsD arr[] = { p1, p2 };

            for (const PathsD& singleBase : arr) {
                PathsD singleClip = buildClipPolygon(singleBase);
                PathsD lines = generateHatchLines(singleClip, angle);
                if (m_data.crossFill) {
                    PathsD cross = generateHatchLines(singleClip, angle + 90.0);
                    lines.insert(lines.end(), cross.begin(), cross.end());
                }
                ClipperD clipper;
                clipper.AddOpenSubject(lines);
                clipper.AddClip(singleClip);

                PathsD openResult, closedResult;
                clipper.Execute(ClipType::Intersection, FillRule::EvenOdd, closedResult, openResult);
                finalResultPath.addPath(ClipperToQtOpenPath(openResult));
            }
        }
        else
        {
            PathsD dxfBase = buildDxfBasePolygon();
            for (const auto& path : dxfBase) {
                PathsD singleBase;
                singleBase.push_back(path);

                PathsD singleClip = buildClipPolygon(singleBase);
                PathsD lines = generateHatchLines(singleClip, angle);
                if (m_data.crossFill) {
                    PathsD cross = generateHatchLines(singleClip, angle + 90.0);
                    lines.insert(lines.end(), cross.begin(), cross.end());
                }

                ClipperD clipper;
                clipper.AddOpenSubject(lines);
                clipper.AddClip(singleClip);

                PathsD openResult, closedResult;
                clipper.Execute(ClipType::Intersection, FillRule::EvenOdd, closedResult, openResult);
                finalResultPath.addPath(ClipperToQtOpenPath(openResult));
            }
        }
        return finalResultPath;
    }
    
}

void FillPreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);


    // 计算 DXF 视图自适应变换矩阵
    QTransform viewTransform;
    if (m_hasDxf && !m_importedContours.isEmpty()) {
        QRectF bounds;
        bool first = true;
        for (const ImportedContour& c : m_importedContours) {
            for (const QPointF& pt : c.vertices) {
                if (first) {
                    bounds = QRectF(pt, pt);
                    first = false;
                }
                else {
                    bounds.setLeft(qMin(bounds.left(), pt.x()));
                    bounds.setRight(qMax(bounds.right(), pt.x()));
                    bounds.setTop(qMin(bounds.top(), pt.y()));
                    bounds.setBottom(qMax(bounds.bottom(), pt.y()));
                }
            }
        }

        if (bounds.width() > 0 && bounds.height() > 0) {
            const double margin = 20.0;
            double scale = qMin((width() - margin * 2) / bounds.width(),
                                (height() - margin * 2) / bounds.height());

            // 获取窗口中心和 DXF 包围盒中心
            double screenCx = width() / 2.0;
            double screenCy = height() / 2.0;
            double dxfCx = bounds.center().x();
            double dxfCy = bounds.center().y();

            // 1. 移动到屏幕中心
            viewTransform.translate(screenCx, screenCy);
            // 2. 缩放，同时对 Y 轴施加负数缩放以翻转倒置图像 (CAD 坐标 Y 向上变大)
            viewTransform.scale(scale, -scale);
            // 3. 将 DXF 图形的中心对齐到原点
            viewTransform.translate(-dxfCx, -dxfCy);
        }
    }

    // 保存当前的 painter 状态
    painter.save();

    // 如果有 DXF 文件应用视图变换矩阵
    if (m_hasDxf) {
        painter.setTransform(viewTransform);
    }

    if (m_hasDxf) {
        // ── 渲染导入的 DXF 轮廓 ──────────────────────────────────────
        for (const ImportedContour& c : m_importedContours) {
            if (c.vertices.size() < 2) continue;

            QPainterPath path;
            path.moveTo(c.vertices.first());
            for (int i = 1; i < c.vertices.size(); ++i)
                path.lineTo(c.vertices[i]);
            if (c.isClosed)
                path.closeSubpath();

            // 注意：在 Qt 中线宽设为 0 表示 "Cosmetic Pen" (几何线宽)。
            // 它在屏幕上永远保持1像素宽，无论 QTransform 放大了多少倍，防止变成色块！
            if (c.isFillable) {
                painter.setPen(QPen(QColor(0, 160, 0), 0)); 
            } else if (c.isClosed && !c.isValid) {
                painter.setPen(QPen(QColor(220, 120, 0), 0, Qt::DashLine));
            } else {
                painter.setPen(QPen(Qt::darkGray, 0, Qt::DotLine));
            }
            painter.drawPath(path);
        }
    } else {
        // ── 测试图形底图 ──────────────────────────────────────────────
        painter.setPen(QPen(Qt::lightGray, 1, Qt::DashLine));
        painter.drawPath(m_shape1);
        painter.drawPath(m_shape2);
    }

    if (!m_hasFill || !m_data.enable) {
        painter.restore(); // 必须恢复原状态以绘制 UI 文字
        painter.setPen(Qt::darkGray);
        painter.drawText(8, height() - 10,
            m_hasDxf
            ? QString("import DXF | Contour: %1 | fillable: %2")
                .arg(m_importedContours.size())
                .arg([this]{ int n=0; for(auto& c:m_importedContours) if(c.isFillable) ++n; return n; }())
            : QStringLiteral("Test graphic mode — Click 'Import DXF' to load the file"));
        return;
    }

    PathsD base = buildBasePolygon();
    QPainterPath qtBase = ClipperToQtClosedPath(base);
    QColor penColor(m_data.penColor);

    // 对于填充部分，同样使用线宽 0 以免被放大成色块
    painter.setPen(QPen(penColor, 0));
    for (int i = 1; i <= m_data.boundaryLoops; ++i) {
        double offset = -qMax(1.0, m_data.pitch * 10.0) * i;
        
        if (m_data.objectCalculation) {
            // 整体计算：相交的对象边界会合并
            PathsD r = InflatePaths(base, offset, JoinType::Miter, EndType::Polygon);
            painter.drawPath(ClipperToQtClosedPath(r));
        } else {
            // 独立计算：交叠的对象各自生成自己的边界
            if (m_hasDxf) {
                // 为 DXF 里的每一条独立轮廓单独打环
                for (const auto& path : base) {
                    PathsD singlePath;
                    singlePath.push_back(path);
                    PathsD r = InflatePaths(singlePath, offset, JoinType::Miter, EndType::Polygon);
                    painter.drawPath(ClipperToQtClosedPath(r));
                }
            } else {
                PathsD p1 = QtPathToClipper(m_shape1);
                PathsD p2 = QtPathToClipper(m_shape2);
                PathsD r1 = InflatePaths(p1, offset, JoinType::Miter, EndType::Polygon);
                PathsD r2 = InflatePaths(p2, offset, JoinType::Miter, EndType::Polygon);
                if (!r1.empty()) painter.drawPath(ClipperToQtClosedPath(r1));
                if (!r2.empty()) painter.drawPath(ClipperToQtClosedPath(r2));
            }
        }
    }

    // 轮廓线 / 绕边
    if (m_data.enableProfile || m_data.walkAround) {
        painter.setPen(QPen(m_data.walkAround ? Qt::blue : penColor, 0));
        painter.drawPath(qtBase);
    }

    // 填充线
    painter.setPen(QPen(penColor, 0));
    painter.drawPath(generateFillPath());

    painter.restore();

    // 底部信息
    painter.setPen(Qt::darkGray);
    painter.drawText(8, height() - 10,
        QString("Type:%1 | loops:%2 | pitch:%3 | profile:%4 | %5")
            .arg(m_data.fillType)
            .arg(m_data.boundaryLoops)
            .arg(m_data.pitch)
            .arg(m_data.enableProfile ? "Y" : "N")
            .arg(m_hasDxf ? "DXF Model" : "TEXT Model"));
}

