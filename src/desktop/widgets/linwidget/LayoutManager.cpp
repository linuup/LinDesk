#include "desktop/widgets/linwidget/LayoutManager.h"

namespace LinWidget {

LayoutManager::LayoutManager(QObject* parent)
    : QObject(parent)
{
}

void LayoutManager::addOrUpdate(const WidgetLayoutState& state)
{
    if (state.instanceId.isEmpty()) {
        return;
    }
    m_states.insert(state.instanceId, state);
    emit changed();
}

void LayoutManager::remove(const QString& instanceId)
{
    if (m_states.remove(instanceId) > 0) {
        emit changed();
    }
}

bool LayoutManager::setGeometry(const QString& instanceId, const QRect& geometry)
{
    auto it = m_states.find(instanceId);
    if (it == m_states.end() || !geometry.isValid()) {
        return false;
    }
    if (it->geometry == geometry) {
        return true;
    }
    it->geometry = geometry;
    emit changed();
    return true;
}

bool LayoutManager::setVisible(const QString& instanceId, bool visible)
{
    auto it = m_states.find(instanceId);
    if (it == m_states.end()) {
        return false;
    }
    if (it->visible == visible) {
        return true;
    }
    it->visible = visible;
    emit changed();
    return true;
}

bool LayoutManager::contains(const QString& instanceId) const
{
    return m_states.contains(instanceId);
}

WidgetLayoutState LayoutManager::state(const QString& instanceId) const
{
    return m_states.value(instanceId);
}

QVector<WidgetLayoutState> LayoutManager::states() const
{
    QVector<WidgetLayoutState> result = m_states.values().toVector();
    std::sort(result.begin(), result.end(), [](const WidgetLayoutState& left, const WidgetLayoutState& right) {
        return left.zOrder < right.zOrder;
    });
    return result;
}

void LayoutManager::setStates(const QVector<WidgetLayoutState>& states)
{
    m_states.clear();
    for (const WidgetLayoutState& state : states) {
        if (!state.instanceId.isEmpty()) {
            m_states.insert(state.instanceId, state);
        }
    }
    emit changed();
}

int LayoutManager::nextZOrder() const
{
    int highest = 0;
    for (const WidgetLayoutState& state : m_states) {
        highest = qMax(highest, state.zOrder);
    }
    return highest + 1;
}

}
