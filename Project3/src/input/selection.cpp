#include "selection.h"
#include <cassert>


Selection::Selection()
{
}

void Selection::clear()
{
    for (int i = 0; i < count; ++i) entities[i] = nullptr;
    count = 0;
}

void Selection::select(Entity *entity)
{
    if (entity != nullptr)
    {
        assert(count < MAX_SELECTED_ENTITIES && "Reached max number of selected items");
        // TODO: Only selects one entity by now
        entities[0] = entity;
        count = 1;
    }
    else
    {
        entities[0] = entity;
        count = 0;
    }
    emit entitySelected(entity);
}

bool Selection::contains(Entity* entity)
{
    for(int i = 0; i < count; ++i)
    {
        if(entities[i] == entity)
            return true;
    }

    return false;
}

QList<Entity*> Selection::GetEntities()
{
    QList<Entity*> ret;
    for (int i = 0; i < count; ++i)
    {
        ret << entities[i];
    }

    return ret;
}


void Selection::onEntitySelectedFromEditor(Entity *entity)
{
    if (entity != nullptr) {
        entities[0] = entity;
        count = 1;
    } else {
        count = 0;
    }
}

void Selection::onEntityRemovedFromEditor(Entity *entity)
{
    if (entity == entities[0])
    {
        entities[0] = nullptr;
        count = 0;
    }
}
