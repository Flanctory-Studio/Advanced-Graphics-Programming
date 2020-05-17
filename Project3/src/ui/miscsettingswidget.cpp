#include "miscsettingswidget.h"
#include "ui_miscsettingswidget.h"
#include "globals.h"
#include <QColorDialog>

#include <QDebug>

MiscSettingsWidget::MiscSettingsWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiscSettingsWidget)
{
    ui->setupUi(this);

    ui->spinCameraSpeed->setValue(DEFAULT_CAMERA_SPEED);
    ui->spinFovY->setValue(DEFAULT_CAMERA_FOVY);

    connect(ui->spinCameraSpeed, SIGNAL(valueChanged(double)), this, SLOT(onCameraSpeedChanged(double)));
    connect(ui->spinFovY, SIGNAL(valueChanged(double)), this, SLOT(onCameraFovYChanged(double)));
    connect(ui->buttonBackgroundColor, SIGNAL(clicked()), this, SLOT(onBackgroundColorClicked()));
    connect(ui->checkBoxGrid, SIGNAL(clicked()), this, SLOT(onVisualHintChanged()));
    connect(ui->checkBoxLightSources, SIGNAL(clicked()), this, SLOT(onVisualHintChanged()));
    connect(ui->checkBoxSelectionOutline, SIGNAL(clicked()), this, SLOT(onVisualHintChanged()));

    connect(ui->renderingPipeline, SIGNAL(currentIndexChanged(int)), this, SLOT(RenderingPipelineStateChanged(int)));

    connect(ui->ReliefMapping, SIGNAL(stateChanged(int)), this, SLOT(ReliefMappingStateChange(int)));
    connect(ui->DepthOfField, SIGNAL(stateChanged(int)), this, SLOT(DepthOfFieldStateChange(int)));
}

void MiscSettingsWidget::RenderingPipelineStateChanged(int activeIndex)
{
    enum RenderingPipelines { ForwardRendering, DeferredRendering };

    switch(activeIndex)
    {
        case RenderingPipelines::ForwardRendering:
        {
            qDebug("ForwardRendering selected");
            break;
        }
        case RenderingPipelines::DeferredRendering:
        {
            qDebug("DeferredRendering selected");
            break;
        }
    }
}

void MiscSettingsWidget::ReliefMappingStateChange(int state)
{
    Qt::CheckState checked = Qt::CheckState(state);

    switch(checked)
    {
        case Qt::CheckState::Unchecked:
        {
            miscSettings->useReliefMapping = false;
            break;
        }

        case Qt::CheckState::Checked:
        {
            miscSettings->useReliefMapping = true;
            break;
        }
    }
}

void MiscSettingsWidget::DepthOfFieldStateChange(int state)
{
    Qt::CheckState checked = Qt::CheckState(state);

    switch(checked)
    {
        case Qt::CheckState::Unchecked:
        {
            miscSettings->useDepthOfField = false;
            break;
        }

        case Qt::CheckState::Checked:
        {
            miscSettings->useDepthOfField = true;
            break;
        }
    }
}


MiscSettingsWidget::~MiscSettingsWidget()
{
    delete ui;
}

void MiscSettingsWidget::onCameraSpeedChanged(double speed)
{
    camera->speed = speed;
}

void MiscSettingsWidget::onCameraFovYChanged(double fovy)
{
    camera->fovy = fovy;
    emit settingsChanged();
}

int g_MaxSubmeshes = 100;

void MiscSettingsWidget::onMaxSubmeshesChanged(int n)
{
    g_MaxSubmeshes = n;
    emit settingsChanged();
}

void MiscSettingsWidget::onBackgroundColorClicked()
{
    QColor color = QColorDialog::getColor(miscSettings->backgroundColor, this, "Background color");
    if (color.isValid())
    {
        QString colorName = color.name();
        ui->buttonBackgroundColor->setStyleSheet(QString::fromLatin1("background-color: %0").arg(colorName));
        miscSettings->backgroundColor = color;
        emit settingsChanged();
    }
}

void MiscSettingsWidget::onVisualHintChanged()
{
    miscSettings->renderLightSources = ui->checkBoxLightSources->isChecked();
    emit settingsChanged();
}
