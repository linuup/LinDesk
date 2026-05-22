#pragma once

#include "desktop/widgets/DesktopComponentItem.h"

#include <QPainter>
#include <QRegion>

void paintDesktopComponent(QPainter& painter,
                           const DesktopComponentItemModel& item,
                           const QRegion& dirtyRegion,
                           qreal dpiScale);
