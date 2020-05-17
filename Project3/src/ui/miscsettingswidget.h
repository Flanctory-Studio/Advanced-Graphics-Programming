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

public slots:

    void onCameraSpeedChanged(double speed);
    void onCameraFovYChanged(double speed);
    void onMaxSubmeshesChanged(int n);
    void onBackgroundColorClicked();
    void onVisualHintChanged();

    void RenderingPipelineStateChanged(int state);

    void ReliefMappingStateChange(int state);
    void DepthOfFieldStateChange(int state);

private:
    Ui::MiscSettingsWidget *ui;
};

#endif // MISCSETTINGSWIDGET_H
