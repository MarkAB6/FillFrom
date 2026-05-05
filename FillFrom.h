#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_FillFrom.h"
#include<QList>
#include<QPixmap>
#include<QButtonGroup>
#include<QMessageBox>

struct FillData
{
    bool  enableProfile = false; // 使能轮廓
	int  profileIconIndex = 0; // 轮廓图标索引
    int   fillType = -1;    // 填充类型 0/1/2，-1表示未选
    bool  enable = false; // 使能
    bool  objectCalculation = false; // 对象整体计算
    bool  walkAround = false; // 绕边走一次
    bool  crossFill = false; // 交叉填充
    int   fillIconIndex = 0;     // 类型图标索引
    double angle = 0.0;   // 角度
	int penNumber = 0;     // 笔数量
	QString penColor;     // 笔颜色 hex值
    double lineSpacing = 0.001;   // 线间距
	int processCount = 1;   // 处理次数
    bool  averageDistribute = false; // 平均分布填充线
    double margin = 0.0;   // 边距
    double startOffset = 0.0;   // 开始偏移
    double endOffset = 0.0;   // 结束偏移
    double straightIndent = 0.0;   // 直线缩进
    int   boundaryLoops = 0;     // 边界环数
    double pitch = 0;   // 环间距
    bool  autoRotate = false; // 自动旋转填充角度
    double rotationAngle = 0;  // 旋转角度
    bool  keepIndependent = false; // 保留填充对象的独立
};

class FillFrom : public QMainWindow
{
    Q_OBJECT

public:
    FillFrom(QWidget *parent = nullptr);
    ~FillFrom();
	void init();//初始化函数

signals:
    void fillConfirmed(const QList<FillData>& dataList); // 点击确定时发出三个图层数据
    void fillDeleted();                       // 点击删除填充时发出


private slots:
	void onProfileChanged(); // 轮廓图片改变槽函数
	void FillLableChanged();//填充图片改变槽函数
    void onConfirm();              // 确定
    void onCancel();               // 取消
    void onRemovePadding();        // 删除填充
	void updateControlStates(); // 根据当前状态更新控件的可用性
private:
	void setDefaultValues(); // 设置默认状态
    void loadFillDataToUI(int index); // 读取指定索引的缓存到UI
    FillData collectFillData(int fillType) const; // 收集当前设置的数据并赋予对应索引标识

	QList<QPixmap> m_profileList;//存储轮廓图片列表
	QList<QPixmap> m_mapList;//存储图片列表   
	QList<QPair<QString, QString>>m_colorList;//存储颜色列表
	QButtonGroup* m_checkGroup; //管理互斥按钮组
	int currentIndex;//当前图片索引
	int currentProfileIndex;//当前轮廓图片索引

    int m_currentFillIndex; // 当前选中的填充按钮ID(0, 1, 2)
    QList<FillData> m_fillDataList; // 缓存各个图层状态(固定大小为3)

    Ui::FillFromClass ui;
};

