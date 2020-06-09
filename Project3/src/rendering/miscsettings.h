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
    QColor outlineColor = QColorConstants::Red;
    bool renderLightSources = true;

    bool userSSAO = false;
    bool useOutline = false;

    double outlineWidth = 2.0;
};

#endif // MISCSETTINGS_H
