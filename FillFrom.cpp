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
    ui.Line_Spacing->setText("0.001");
    ui.Margin->setText("0");
    ui.Start_Offset->setText("0");
    ui.End_Offset->setText("0");
    ui.Straight_indentation->setText("0");
    ui.Boundary_Loops->setText("0");
    ui.Pitch->setText("0");
    ui.Number->setText("1");
    ui.Rotation_AngleEdit->setText("0");
    ui.Pen_Number->setCurrentIndex(0);
    ui.Select_Enable->setChecked(false);
    ui.Enable_Profile->setChecked(false);
    ui.Object_Calculation->setChecked(false);
    ui.Walk_Around->setChecked(false);
    ui.Cross_Fill->setChecked(false);
    ui.checkBox_8->setChecked(false);
    ui.Automatic_Fill_Angle->setChecked(false);
    ui.Keep_Padding_independent->setChecked(false);
}

void FillFrom::loadFillDataToUI(int index)
{
    if (index < 0 || index >= m_fillDataList.size()) return;
    const FillData& data = m_fillDataList[index];

    ui.Select_Enable->setChecked(data.enable);
    ui.Enable_Profile->setChecked(data.enableProfile);
    ui.Object_Calculation->setChecked(data.objectCalculation);
    ui.Walk_Around->setChecked(data.walkAround);
    ui.Cross_Fill->setChecked(data.crossFill);
    ui.checkBox_8->setChecked(data.averageDistribute);
    ui.Automatic_Fill_Angle->setChecked(data.autoRotate);
    ui.Keep_Padding_independent->setChecked(data.keepIndependent);

    ui.Angle_Edit->setText(QString::number(data.angle));
    ui.Line_Spacing->setText(QString::number(data.lineSpacing));
    ui.Margin->setText(QString::number(data.margin));
    ui.Start_Offset->setText(QString::number(data.startOffset));
    ui.End_Offset->setText(QString::number(data.endOffset));
    ui.Straight_indentation->setText(QString::number(data.straightIndent));
    ui.Boundary_Loops->setText(QString::number(data.boundaryLoops));
    ui.Pitch->setText(QString::number(data.pitch));
    ui.Number->setText(QString::number(data.processCount));
    ui.Rotation_AngleEdit->setText(QString::number(data.rotationAngle));

    if (data.penNumber >= 0 && data.penNumber < ui.Pen_Number->count())
        ui.Pen_Number->setCurrentIndex(data.penNumber);

    currentIndex = data.fillIconIndex;
    currentProfileIndex = data.profileIconIndex;

    if (!m_mapList.isEmpty()) {
        ui.Fill_Lable->setIcon(QIcon(m_mapList[currentIndex % m_mapList.size()]));
    }
    if (!m_profileList.isEmpty()) {
        ui.Icon->setIcon(QIcon(m_profileList[currentProfileIndex % m_profileList.size()]));
    }
}

void FillFrom::onConfirm()
{
    // 如果有使能选项但没有选中任何按钮，先校验一次（实际逻辑已通过按钮切换实时保存）
    if (m_currentFillIndex != -1) {
        m_fillDataList[m_currentFillIndex] = collectFillData(m_currentFillIndex);
    }
    emit fillConfirmed(m_fillDataList);
    close();
}

void FillFrom::onCancel()
{
	close();
}

void FillFrom::onRemovePadding()
{
    m_fillDataList.clear();
    m_fillDataList.resize(3);
    for (int i = 0; i < 3; ++i) {
        m_fillDataList[i].fillType = i;
        m_fillDataList[i].enable = false;
    }

    for (QAbstractButton* btn : m_checkGroup->buttons())
        btn->setChecked(false);
    
    m_currentFillIndex = -1;
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
    const bool shouldEnable = ui.Select_Enable->isChecked() && (m_currentFillIndex != -1);

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
    ui.Keep_Padding_independent->setEnabled(shouldEnable);
}

FillData FillFrom::collectFillData(int fillType) const
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
	data.penColor = m_colorList.value(data.penNumber).second; 
    data.lineSpacing = ui.Line_Spacing->text().toDouble();
    data.processCount = ui.Number->text().toInt();
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

    data.profileIconIndex = currentProfileIndex;
    data.fillType = fillType;
   
    return data;
}


void FillFrom::init()
{
    m_currentFillIndex = -1;
    m_fillDataList.resize(3);
    for (int i = 0; i < 3; ++i) {
        m_fillDataList[i].fillType = i;
        m_fillDataList[i].enable = false;
    }

	//初始话轮廓图片列表
    m_profileList.append(QPixmap(":/image/8.png"));
    m_profileList.append(QPixmap(":/image/9.png"));

    if (m_profileList.size() > 0)
    {
        currentProfileIndex = 0;
        ui.Icon->setIcon(QIcon(m_profileList[currentProfileIndex]));
        ui.Icon->setIconSize(ui.Icon->size());
    }

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
        QPixmap pix(24, 24);
        pix.fill(QColor(hex));
        icon = QIcon(pix);

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
            // 切换前，保存当前 UI 面板的参数到旧的层级索引
            if (m_currentFillIndex != -1) {
                m_fillDataList[m_currentFillIndex] = collectFillData(m_currentFillIndex);
            }
            // 排他逻辑：把其余的按钮勾选去掉
            for (QAbstractButton* btn : m_checkGroup->buttons()) {
                if (m_checkGroup->id(btn) != id && btn->isChecked())
                    btn->setChecked(false);
            }
            // 更新层级并回显对应的数据
            m_currentFillIndex = id;
            loadFillDataToUI(m_currentFillIndex);
        } else {
            // 如果是被取消的本按钮（比如被自己点击取消掉）
            if (m_currentFillIndex == id) {
                m_fillDataList[m_currentFillIndex] = collectFillData(m_currentFillIndex);
                m_currentFillIndex = -1;
                setDefaultValues();
            }
        }
        updateControlStates();
    });

    connect(ui.Select_Enable, &QCheckBox::toggled, this, &FillFrom::updateControlStates);
    connect(ui.checkBox_8, &QCheckBox::toggled, this, &FillFrom::updateControlStates);
    connect(ui.Automatic_Fill_Angle, &QCheckBox::toggled, this, &FillFrom::updateControlStates);

    connect(ui.Icon, &QPushButton::clicked, this, [=] {
        currentProfileIndex++;
        if (currentProfileIndex >= m_profileList.size())
            currentProfileIndex = 0;
        ui.Icon->setIcon(QIcon(m_profileList[currentProfileIndex]));
        });
    connect(ui.Fill_Lable, &QPushButton::clicked, this, &FillFrom::FillLableChanged);

    // 确定 / 取消 / 删除填充
    connect(ui.Confirm, &QPushButton::clicked, this, &FillFrom::onConfirm);
    connect(ui.Cancel, &QPushButton::clicked, this, &FillFrom::onCancel);
    connect(ui.Remove_Padding, &QPushButton::clicked, this, &FillFrom::onRemovePadding);
}

void FillFrom::FillLableChanged()
{
    currentIndex++;
    if (currentIndex >= m_mapList.size())
        currentIndex = 0;
    ui.Fill_Lable->setIcon(QIcon(m_mapList[currentIndex]));
}

void FillFrom::onProfileChanged()
{
  
}  