#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QHash>
#include <QObject>
#include <QVector>

namespace LinWidget {

class LayoutManager : public QObject {
    Q_OBJECT

public:
    explicit LayoutManager(QObject* parent = nullptr);

    void addOrUpdate(const WidgetLayoutState& state);
    void remove(const QString& instanceId);
    bool setGeometry(const QString& instanceId, const QRect& geometry);
    bool setVisible(const QString& instanceId, bool visible);
    bool contains(const QString& instanceId) const;
    WidgetLayoutState state(const QString& instanceId) const;
    QVector<WidgetLayoutState> states() const;
    void setStates(const QVector<WidgetLayoutState>& states);
    int nextZOrder() const;

signals:
    void changed();

private:
    QHash<QString, WidgetLayoutState> m_states;
};

}
