#ifndef SELECTION_H
#define SELECTION_H

#include <QObject>

class Entity;

#define MAX_SELECTED_ENTITIES 255

class Selection : public QObject
{
    Q_OBJECT

public:
    Selection();

    void clear();
    void select(Entity *);
    bool contains(Entity* );

    int count = 0;
    Entity *entities[MAX_SELECTED_ENTITIES] = {};

    QList<Entity*> GetEntities();

signals:
    void entitySelected(Entity *);

public slots:

    void onEntitySelectedFromEditor(Entity *);
    void onEntityRemovedFromEditor(Entity *);
};

#endif // SELECTION_H
