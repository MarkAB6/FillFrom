#include "FillFrom.h"

FillFrom::FillFrom(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);    
	this->init();
}

FillFrom::~FillFrom()
{}

void FillFrom::setDefaultValues()
{
    ui.Angle_Edit->setText("0");
    ui.Line_Spacing->setText("0");
    ui.Margin->setText("0");
    ui.Start_Offset->setText("0");
    ui.End_Offset->setText("0");
    ui.Straight_indentation->setText("0");
    ui.Boundary_Loops->setText("0");
    ui.Pitch->setText("0");
    ui.Number->setText("0");
    ui.Rotation_AngleEdit->setText("0");
    ui.Pen_Number->setCurrentIndex(0);
}
void FillFrom::onConfirm()
{
    // 若使能但未选填充类型，提示用户
    if (ui.Select_Enable->isChecked()) {
        bool anyChecked = false;
        for (QAbstractButton* btn : m_checkGroup->buttons()) {
            if (btn->isChecked()) { anyChecked = true; break; }
        }
        if (!anyChecked) {
            QMessageBox::warning(this, tr("提示"), tr("请先选择填充类型（填充1 / 2 / 3）。"));
            return;
        }
    }
    emit fillConfirmed(collectFillData());
    close();
}
void FillFrom::onCancel()
{
	close();
}
void FillFrom::onRemovePadding()
{
    ui.Enable_Profile->setChecked(false);
    ui.Select_Enable->setChecked(false);
    ui.Object_Calculation->setChecked(false);
    ui.Walk_Around->setChecked(false);
    ui.Cross_Fill->setChecked(false);
    ui.checkBox_8->setChecked(false);
    ui.Automatic_Fill_Angle->setChecked(false);
    ui.Keep_Padding_independent->setChecked(false);

    for (QAbstractButton* btn : m_checkGroup->buttons())
        btn->setChecked(false);

    currentIndex = 0;
    if (!m_mapList.isEmpty()) {
        ui.Fill_Lable->setIcon(QIcon(m_mapList[currentIndex]));
        ui.Fill_Lable->setIconSize(ui.Fill_Lable->size());
    }

    setDefaultValues();
    updateControlStates();

    emit fillDeleted();
}
void FillFrom::updateControlStates()
{
    bool isFillChecked = false;
    for (QAbstractButton* btn : m_checkGroup->buttons()) {
        if (btn->isChecked()) { isFillChecked = true; break; }
    }
    const bool shouldEnable = ui.Select_Enable->isChecked() && isFillChecked;

    // 基本输入框
    ui.Angle_Edit->setEnabled(shouldEnable);
    ui.Margin->setEnabled(shouldEnable);
    ui.Straight_indentation->setEnabled(shouldEnable);
    ui.Number->setEnabled(shouldEnable);
    ui.Boundary_Loops->setEnabled(shouldEnable);
    ui.Pitch->setEnabled(shouldEnable);
    ui.Start_Offset->setEnabled(shouldEnable);
    ui.End_Offset->setEnabled(shouldEnable);

    // 线间距：启用状态下且未勾选"平均分布"才可手动输入
    ui.checkBox_8->setEnabled(shouldEnable);
    ui.Line_Spacing->setEnabled(shouldEnable && !ui.checkBox_8->isChecked());

    // 旋转角度：启用状态下且勾选"自动旋转"才可编辑
    ui.Automatic_Fill_Angle->setEnabled(shouldEnable);
    ui.Rotation_AngleEdit->setEnabled(shouldEnable && ui.Automatic_Fill_Angle->isChecked());

    // 附属 CheckBox
    ui.Object_Calculation->setEnabled(shouldEnable);
    ui.Walk_Around->setEnabled(shouldEnable);
    ui.Cross_Fill->setEnabled(shouldEnable);
}
FillData FillFrom::collectFillData() const
{
    FillData data;
    data.enableProfile = ui.Enable_Profile->isChecked();
    data.enable = ui.Select_Enable->isChecked();
    data.objectCalculation = ui.Object_Calculation->isChecked();
    data.walkAround = ui.Walk_Around->isChecked();
    data.crossFill = ui.Cross_Fill->isChecked();
    data.fillIconIndex = currentIndex;
    data.angle = ui.Angle_Edit->text().toDouble();
    data.penNumber = ui.Pen_Number->currentIndex();
	data.penColor = m_colorList.value(data.penNumber).second; // 获取对应的颜色值
    data.lineSpacing = ui.Line_Spacing->text().toDouble();
	data.lineCount = ui.Number->text().toInt();
    data.averageDistribute = ui.checkBox_8->isChecked();
    data.margin = ui.Margin->text().toDouble();
    data.startOffset = ui.Start_Offset->text().toDouble();
    data.endOffset = ui.End_Offset->text().toDouble();
    data.straightIndent = ui.Straight_indentation->text().toDouble();
    data.boundaryLoops = ui.Boundary_Loops->text().toInt();
    data.pitch = ui.Pitch->text().toDouble();
    data.autoRotate = ui.Automatic_Fill_Angle->isChecked();
    data.rotationAngle = ui.Rotation_AngleEdit->text().toDouble();
    data.keepIndependent = ui.Keep_Padding_independent->isChecked();

    data.fillType = -1;
    for (QAbstractButton* btn : m_checkGroup->buttons()) {
        if (btn->isChecked()) {
            data.fillType = m_checkGroup->id(btn);
            break;
        }
    }
    return data;
}


void FillFrom::init()
{
    //初始化颜色列表
	m_colorList.append(qMakePair(QString("10"), QString("#FF0000")));
    m_colorList.append(qMakePair(QString("9"), QString("#00FF00")));
	m_colorList.append(qMakePair(QString("8"), QString("#0000FF")));
    m_colorList.append(qMakePair(QString("7"), QString("#FFFF00")));
    m_colorList.append(qMakePair(QString("6"), QString("#FFFFFF")));
    m_colorList.append(qMakePair(QString("5"), QString("#000000")));
    m_colorList.append(qMakePair(QString("4"), QString("#FF7F00")));
    m_colorList.append(qMakePair(QString("3"), QString("#800080")));
    m_colorList.append(qMakePair(QString("2"), QString("#888888")));
    m_colorList.append(qMakePair(QString("1"), QString("#D3D3D3")));
  
    for (auto pair : m_colorList)
    {
        QString name = pair.first;
        QString hex = pair.second;

        QIcon icon;
        // 生成一个纯色小方块当做颜色预览
        QPixmap pix(24, 24);
        pix.fill(QColor(hex));
        icon = QIcon(pix);

        // 添加item：颜色图标 + 文字(名称+色号)，data存hex
        ui.Pen_Number->addItem(icon, QString("          %1").arg(name));
    }

	//初始化图片列表
	m_mapList.append(QPixmap(":/image/1.png"));
    m_mapList.append(QPixmap(":/image/2.png"));
    m_mapList.append(QPixmap(":/image/3.png"));

    if(m_mapList.size() > 0)
    {
        currentIndex = 0;
		ui.Fill_Lable->setIcon(QIcon(m_mapList[currentIndex]));
        ui.Fill_Lable->setIconSize(ui.Fill_Lable->size());
	}

    //初始化按钮组
    m_checkGroup = new QButtonGroup(this);
    m_checkGroup->setExclusive(false);
    m_checkGroup->addButton(ui.Fill1, 0);
    m_checkGroup->addButton(ui.Fill2, 1);
    m_checkGroup->addButton(ui.Fill3, 2);

    //输入框验证器
    ui.Angle_Edit->setValidator(new QDoubleValidator(-360.0, 360.0, 2, this));
    ui.Line_Spacing->setValidator(new QDoubleValidator(0.0, 9999.0, 3, this));
    ui.Margin->setValidator(new QDoubleValidator(-9999.0, 9999.0, 3, this));
    ui.Start_Offset->setValidator(new QDoubleValidator(-9999.0, 9999.0, 3, this));
    ui.End_Offset->setValidator(new QDoubleValidator(-9999.0, 9999.0, 3, this));
    ui.Straight_indentation->setValidator(new QDoubleValidator(-9999.0, 9999.0, 3, this));
    ui.Boundary_Loops->setValidator(new QIntValidator(0, 9999, this));
    ui.Pitch->setValidator(new QDoubleValidator(0.0, 9999.0, 3, this));
    ui.Number->setValidator(new QIntValidator(0, 9999, this));
    ui.Rotation_AngleEdit->setValidator(new QDoubleValidator(-360.0, 360.0, 2, this));

	// 设置默认状态
    this->setDefaultValues();
    
    updateControlStates();

    connect(ui.Enable_Profile, &QCheckBox::toggled, this, [=](bool checked) {
        ui.Icon->setEnabled(checked);
        });
    ui.Icon->setEnabled(ui.Enable_Profile->isChecked());

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_checkGroup, &QButtonGroup::idToggled, this, [=](int id, bool checked) {
#else
    connect(m_checkGroup, static_cast<void(QButtonGroup::*)(int, bool)>(&QButtonGroup::buttonToggled), this, [=](int id, bool checked) {
#endif
        if (checked) {
            for (QAbstractButton* btn : m_checkGroup->buttons()) {
                if (m_checkGroup->id(btn) != id && btn->isChecked())
                    btn->setChecked(false);
            }
        }
        updateControlStates();
        });

    // 使能 checkbox
    connect(ui.Select_Enable, &QCheckBox::toggled, this, &FillFrom::updateControlStates);

    // 平均分布填充线：勾选时禁用线间距手动输入
    connect(ui.checkBox_8, &QCheckBox::toggled, this, &FillFrom::updateControlStates);

    // 自动旋转填充角度：勾选时才允许编辑旋转角度
    connect(ui.Automatic_Fill_Angle, &QCheckBox::toggled, this, &FillFrom::updateControlStates);

    // 类型图片切换
    connect(ui.Fill_Lable, &QPushButton::clicked, this, &FillFrom::FillLableChanged);

    // 确定 / 取消 / 删除填充
    connect(ui.Confirm, &QPushButton::clicked, this, &FillFrom::onConfirm);
    connect(ui.Cancel, &QPushButton::clicked, this, &FillFrom::onCancel);
    connect(ui.Remove_Padding, &QPushButton::clicked, this, &FillFrom::onRemovePadding);
    connect(ui.Fill_Lable, &QPushButton::clicked, this, &FillFrom::FillLableChanged);
}

void FillFrom::FillLableChanged()
{
    currentIndex++;
    if (currentIndex >= m_mapList.size())
        currentIndex = 0;
    ui.Fill_Lable->setIcon(QIcon(m_mapList[currentIndex]));
}

