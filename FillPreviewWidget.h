#pragma once
#include <QWidget>
#include <QPainterPath>
#include <QPushButton>
#include "FillFrom.h"
#include "DxfImporter.h"
#include "clipper2/clipper.h"
#include<QFutureWatcher>
using namespace Clipper2Lib;

class FillPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FillPreviewWidget(QWidget* parent = nullptr);

    ~FillPreviewWidget();
    void applyFillList(const QList<FillData>& dataList);
    void connectToForm(FillFrom* form);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onImportDxf();
 
private:
    QFutureWatcher<QList<QPainterPath>> m_watcher; //监听计算结果
    QList<QPainterPath> m_cachedFillPath;//缓存计算好的填充路径
	bool m_isCalculating = false; //是否正在计算填充路径

    QList<FillData>     m_data;
    bool         m_hasFill = false;
    bool         m_hasDxf  = false;

    QPainterPath m_shape1;
    QPainterPath m_shape2;

    QVector<ImportedContour> m_importedContours;//存储轮廓数据

    QPushButton* m_importBtn = nullptr;

	void startAsyncCompute(); //开始计算填充路径

    PathsD buildBasePolygon(const FillData& data)                               const;//构建基础多边形
    PathsD buildDxfBasePolygon()                            const;//构建导入 DXF 的基础多边形
	PathsD buildClipPolygon(const PathsD& base, const FillData& data)             const;//计算裁剪多边形（基础多边形缩放、偏移、添加边距等）

    PathsD generateHatchLines(const PathsD& clipPolygon, double angleDeg, const FillData& data) const;//生成填充线

    /// 对单个轮廓区域执行裁剪并返回填充路径（消除重复逻辑的核心辅助）
    QPainterPath clipHatchToPath(const PathsD& clipPolygon, double angleDeg, const FillData& data) const;
    QList<QPainterPath> generateFillPath()                         const;

  
    QTransform   computeViewTransform()                     const;
    int          countFillableContours()                    const;
    void         paintContours(QPainter& painter)           const;
    void         paintBoundaryLoops(QPainter& painter,
                                     const PathsD& base,
                                     const FillData& data, const QColor& color)   const;
    void         paintStatusText(QPainter& painter)         const;
};

