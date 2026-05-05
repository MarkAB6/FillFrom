#pragma once
#include <QVector>
#include <QPointF>
#include <QString>

// libdxfrw
#include "3rdparty/libdxfrw/src/drw_interface.h"
#include "3rdparty/libdxfrw/src/libdxfrw.h"
#include "3rdparty/libdxfrw/src/drw_objects.h"

//单个轮廓
struct ImportedContour
{
    QVector<QPointF> vertices;
    bool   isClosed     = false;  ///< DXF 实体本身标记为闭合
    bool   isValid      = false;  ///< Boost.Geometry is_valid 通过
    bool   isFillable   = false;  ///< isClosed && isValid
    QString invalidReason;        ///< 无效原因（仅用于调试）
};

struct DxfImportResult
{
    QVector<ImportedContour> contours;
    int     totalEntities    = 0;
    int     validContours    = 0;
    int     fillableContours = 0;
    bool    success          = false;
    QString message;
};

class DxfImporter : public DRW_Interface
{
public:
    DxfImportResult importFile(const QString& filePath);

private:
   static constexpr double kDedupTol  = 1e-6;  ///< 顶点去重容差
    static constexpr double kCloseTol  = 1e-4;  ///< 首尾闭合判定容差
    static constexpr double kEps       = 1e-9;  ///< 通用极小值
    static constexpr int    kArcSegDef = 64;    ///< 圆弧默认离散段数
    static constexpr int    kCircleSeg = 72;    ///< 圆默认离散段数
    static constexpr double kArcStepDeg = 10.0; ///< bulge 自动分段步长（度）

     void addLine      (const DRW_Line&       data) override;
    void addArc       (const DRW_Arc&        data) override;
    void addCircle    (const DRW_Circle&     data) override;
    void addLWPolyline(const DRW_LWPolyline& data) override;
    void addPolyline  (const DRW_Polyline&   data) override;
    void addSpline    (const DRW_Spline*     data) override;

    static QVector<QPointF> discretizeArc(double cx, double cy, double r,
                                          double startDeg, double endDeg,
                                          bool ccw, int segments = kArcSegDef);

    static QVector<QPointF> discretizeBulge(const QPointF& p1, const QPointF& p2,
                                            double bulge, int segments = 0);

    static ImportedContour  validateContour(ImportedContour c);

    /// 将顶点追加到轮廓，自动跳过重复点
    static void appendVertex(ImportedContour& c, const QPointF& pt);

    //  散乱 LINE 实体拼接 
    void chainLineSegments();

    /// addLWPolyline / addPolyline 的公共实现（模板消除重复）
    template<typename TVertList>
    ImportedContour buildPolylineContour(const TVertList& vertlist, bool closed) const;

    //  内部状态 
    QVector<ImportedContour>        m_contours;
    QVector<QPair<QPointF,QPointF>> m_rawLines;
    int                             m_totalEntities = 0;

	//其余纯虚函数，空实现，后续要求时再完善
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
};