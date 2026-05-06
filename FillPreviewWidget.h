#pragma once
#include <QWidget>
#include <QPainterPath>
#include <QPushButton>
#include "FillFrom.h"
#include "DxfImporter.h"
#include "clipper2/clipper.h"
#include <QFutureWatcher>
using namespace Clipper2Lib;

/// 每个图层的完整绘制缓存（全部在异步线程中预算好）
struct LayerCacheData {
    QPainterPath fillPath;      ///< 填充线路径
    QPainterPath basePath;      ///< 轮廓闭合路径
    QPainterPath boundaryPath;  ///< 所有边界环路径
};

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
    static constexpr int kMaxHatchLines = 10000; ///< 单层最大填充线数，防止密度过高时卡死

    QFutureWatcher<QList<LayerCacheData>> m_watcher;
    QList<LayerCacheData> m_cachedLayers; ///< 缓存每层的完整绘制数据
    bool m_isCalculating = false;

    QList<FillData>     m_data;
    bool         m_hasFill = false;
    bool         m_hasDxf  = false;

    QPainterPath m_shape1;
    QPainterPath m_shape2;

    QVector<ImportedContour> m_importedContours;

    QPushButton* m_importBtn = nullptr;

    void startAsyncCompute();

    PathsD buildBasePolygon(const FillData& data)                                  const;
    PathsD buildDxfBasePolygon()                                                   const;
    PathsD buildClipPolygon(const PathsD& base, const FillData& data)              const;
    PathsD generateHatchLines(const PathsD& clipPolygon, double angleDeg, const FillData& data) const;
    QPainterPath clipHatchToPath(const PathsD& clipPolygon, double angleDeg, const FillData& data) const;

    /// 计算单层的完整缓存（填充 + 轮廓 + 边界环）
    LayerCacheData computeLayerCache(const FillData& data)                         const;
    QList<LayerCacheData> generateLayerCaches()                                    const;

    QTransform   computeViewTransform()                                            const;
    int          countFillableContours()                                           const;
    void         paintContours(QPainter& painter)                                  const;
    void         paintStatusText(QPainter& painter)                                const;
};

