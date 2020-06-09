#ifndef MISCSETTINGSWIDGET_H
#define MISCSETTINGSWIDGET_H

#include <QWidget>

namespace Ui {
class MiscSettingsWidget;
}

class MiscSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MiscSettingsWidget(QWidget *parent = nullptr);
    ~MiscSettingsWidget();

signals:

    void settingsChanged();
    void RenderingPipelineChanged();

public slots:

    void onCameraSpeedChanged(double speed);
    void onOutlineWidth(double width);
    void onCameraFovYChanged(double speed);
    void onMaxSubmeshesChanged(int n);
    void onBackgroundColorClicked();
    void onOutlineColorClicked();
    void onVisualHintChanged();
    void onGridStateChanged(int state);

    void RenderingPipelineStateChanged(int state);

    void StateChangeSSAO(int state);
    void StateChangeOutline(int state);

private slots:
    void on_buttonBackgroundColor_clicked();

private:
    Ui::MiscSettingsWidget *ui;
};

#endif // MISCSETTINGSWIDGET_H
