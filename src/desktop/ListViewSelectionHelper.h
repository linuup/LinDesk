#pragma once

#include "desktop/ListView.h"
#include "desktop/DesktopRootItemState.h"
#include <QVector>
#include <QString>

class ListViewSelectionHelper
{
public:
    static QString findDirectionalFocusCandidateInList(
        const ListView* view,
        const QVector<DesktopRootItemState>& states,
        const QString& focusedPath,
        ListView::FocusMoveDirection direction);
};
