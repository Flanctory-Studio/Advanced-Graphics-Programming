#ifndef MISCSETTINGS_H
#define MISCSETTINGS_H

#include <QColor>

class MiscSettings
{
public:
    MiscSettings();

    // TODO: Maybe not the best place for this stuff...
    QColor backgroundColor;
    QColor outlineColor;
    bool renderLightSources = true;

    bool useReliefMapping = false;
    bool useDepthOfField = false;
};

#endif // MISCSETTINGS_H
