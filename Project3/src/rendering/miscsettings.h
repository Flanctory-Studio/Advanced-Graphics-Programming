#ifndef MISCSETTINGS_H
#define MISCSETTINGS_H

#include <QColor>

class MiscSettings
{
public:
    MiscSettings();

    // TODO: Maybe not the best place for this stuff...
    bool renderGrid = true;
    QColor backgroundColor;
    QColor outlineColor = QColor(1.0, 0.0, 0.0, 1.0);
    bool renderLightSources = true;

    bool useReliefMapping = false;
    bool useDepthOfField = false;
};

#endif // MISCSETTINGS_H
