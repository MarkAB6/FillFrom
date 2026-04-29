#include "FillFrom.h"
#include "FillPreviewWidget.h"
#include <QtWidgets/QApplication>
#include <QMetaType> // 确保包含此头文件

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 将自定义结构体注册到 Qt 的元对象系统中
    qRegisterMetaType<FillData>("FillData");

    FillFrom*         fillForm    = new FillFrom();
    FillPreviewWidget* previewWnd = new FillPreviewWidget();

    previewWnd->show();
    previewWnd->move(800, 200);

    fillForm->show();
    fillForm->move(200, 200);

    // 将预览窗口与表单信号进行绑定
    previewWnd->connectToForm(fillForm);
   
    return app.exec();
}
