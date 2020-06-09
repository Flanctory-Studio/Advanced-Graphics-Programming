#ifndef MISCSETTINGS_H
#define MISCSETTINGS_H

#include <QColor>

enum RenderingPipeline { ForwardRendering, DeferredRendering };

class MiscSettings
{
public:
    MiscSettings();

    // TODO: Maybe not the best place for this stuff...
    bool renderGrid = true;
    QColor backgroundColor;
    QColor outlineColor = QColorConstants::Red;
    bool renderLightSources = true;

    bool useSSAO = false;
    bool useOutline = true;

    double outlineWidth = 2.0;

    RenderingPipeline renderingPipeline = RenderingPipeline::DeferredRendering;

};

#endif // MISCSETTINGS_H
