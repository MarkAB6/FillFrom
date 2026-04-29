#pragma once
#include <QVector>
#include <QPointF>
#include <QString>

// libdxfrw
#include "3rdparty/libdxfrw/src/drw_interface.h"
#include "3rdparty/libdxfrw/src/libdxfrw.h"
#include "3rdparty/libdxfrw/src/drw_objects.h"

// ── 单个轮廓 ─────────────────────────────────────────────────────────────
struct ImportedContour {
    QVector<QPointF> vertices;
    bool isClosed   = false;   // DXF 实体本身标记为闭合
    bool isValid    = false;   // Boost.Geometry is_valid 通过
    bool isFillable = false;   // isClosed && isValid
    QString invalidReason;     // 无效原因（调试用）
};

// ── 导入汇总结果 ──────────────────────────────────────────────────────────
struct DxfImportResult {
    QVector<ImportedContour> contours;
    int totalEntities    = 0;
    int validContours    = 0;
    int fillableContours = 0;
    bool success         = false;
    QString message;
};

// ── DXF 解析器（继承 libdxfrw 回调接口）──────────────────────────────────
class DxfImporter : public DRW_Interface
{
public:
    DxfImportResult importFile(const QString& filePath);

private:
    /* ---- 感兴趣的实体回调 ---- */
    void addLine      (const DRW_Line&       data) override;
    void addArc       (const DRW_Arc&        data) override;
    void addCircle    (const DRW_Circle&     data) override;
    void addLWPolyline(const DRW_LWPolyline& data) override;
    void addPolyline  (const DRW_Polyline&   data) override;
    void addSpline    (const DRW_Spline*     data) override;

    /* ---- 其余纯虚函数，提供空实现 ---- */
    void addHeader      (const DRW_Header*)              override {}
    void addLType       (const DRW_LType&)               override {}
    void addLayer       (const DRW_Layer&)               override {}
    void addDimStyle    (const DRW_Dimstyle&)            override {}
    void addVport       (const DRW_Vport&)               override {}
    void addTextStyle   (const DRW_Textstyle&)           override {}
    void addAppId       (const DRW_AppId&)               override {}
    void addBlock       (const DRW_Block&)               override {}
    void setBlock       (const int)                      override {}
    void endBlock       ()                               override {}
    void addPoint       (const DRW_Point&)               override {}
    void addRay         (const DRW_Ray&)                 override {}
    void addXline       (const DRW_Xline&)               override {}
    void addEllipse     (const DRW_Ellipse&)             override {}
    void addKnot        (const DRW_Entity&)              override {}
    void addInsert      (const DRW_Insert&)              override {}
    void addTrace       (const DRW_Trace&)               override {}
    void add3dFace      (const DRW_3Dface&)              override {}
    void addSolid       (const DRW_Solid&)               override {}
    void addMText       (const DRW_MText&)               override {}
    void addText        (const DRW_Text&)                override {}
    void addDimAlign    (const DRW_DimAligned*)          override {}
    void addDimLinear   (const DRW_DimLinear*)           override {}
    void addDimRadial   (const DRW_DimRadial*)           override {}
    void addDimDiametric(const DRW_DimDiametric*)        override {}
    void addDimAngular  (const DRW_DimAngular*)          override {}
    void addDimAngular3P(const DRW_DimAngular3p*)        override {}
    void addDimOrdinate (const DRW_DimOrdinate*)         override {}
    void addLeader      (const DRW_Leader*)              override {}
    void addHatch       (const DRW_Hatch*)               override {}
    void addViewport    (const DRW_Viewport&)            override {}
    void addImage       (const DRW_Image*)               override {}
    void linkImage      (const DRW_ImageDef*)            override {}
    void addComment     (const char*)                    override {}
    void addPlotSettings(const DRW_PlotSettings*)        override {}

    /* ---- write 系列纯虚函数（签名须与 DRW_Interface 完全一致）---- */
    void writeHeader      (DRW_Header&) override {}
    void writeBlocks      ()            override {}
    void writeBlockRecords()            override {}
    void writeEntities    ()            override {}
    void writeLTypes      ()            override {}
    void writeLayers      ()            override {}
    void writeTextstyles  ()            override {}
    void writeVports      ()            override {}
    void writeDimstyles   ()            override {}
    void writeObjects     ()            override {}
    void writeAppId       ()            override {}

    /* ---- 内部工具函数 ---- */
    static QVector<QPointF> discretizeArc(double cx, double cy, double r,
                                          double startDeg, double endDeg,
                                          bool ccw, int segments = 64);
    static ImportedContour validateContour(ImportedContour c);
    void chainLineSegments();   // 将散乱 LINE 实体拼接成轮廓

    QVector<ImportedContour>         m_contours;
    QVector<QPair<QPointF,QPointF>>  m_rawLines;   // 待拼接的 LINE 实体
    int                              m_totalEntities = 0;
};