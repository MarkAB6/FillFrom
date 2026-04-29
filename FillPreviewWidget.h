#pragma once
#include <QWidget>
#include <QPainterPath>
#include <QPushButton>
#include "FillFrom.h"
#include "DxfImporter.h"
#include "clipper2/clipper.h"

using namespace Clipper2Lib;

class FillPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FillPreviewWidget(QWidget* parent = nullptr);

    void applyFill(const FillData& data);
    void connectToForm(FillFrom* form);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onImportDxf();

private:
    FillData     m_data;
    bool         m_hasFill    = false;
    bool         m_hasDxf     = false;    // 是否已导入 DXF

    // 测试图形（未导入 DXF 时使用）
    QPainterPath m_shape1;
    QPainterPath m_shape2;

    // 导入的 DXF 轮廓
    QVector<ImportedContour> m_importedContours;

    QPushButton* m_importBtn = nullptr;

    // ── 填充计算（作用于当前激活图形）──────────────────────────────
    PathsD buildBasePolygon()                              const;
    PathsD buildClipPolygon(const PathsD& base)            const;
    PathsD generateHatchLines(const PathsD& clip,
                               double angleDeg)            const;
    QPainterPath generateFillPath()                        const;

    // ── 从导入轮廓构建 PathsD ──────────────────────────────────────
    PathsD buildDxfBasePolygon()                          const;
};

