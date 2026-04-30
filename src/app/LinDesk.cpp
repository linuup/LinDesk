#include "app/LinDesk.h"

#include <QContextMenuEvent>

LinDesk* LinDesk::m_instance = nullptr;

LinDesk::LinDesk(QWidget* parent) : QWidget(parent)
{
    setWindowTitle("LinDesk");
    setObjectName("LinDesk");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setGeometry(600, 600, 600, 500);
}

LinDesk::~LinDesk()
{
}

void LinDesk::contextMenuEvent(QContextMenuEvent* event)
{
    Q_UNUSED(event);
}

LinDesk* LinDesk::instance(QWidget* parent)
{
    if (!m_instance) {
        m_instance = new LinDesk(parent);
    }
    return m_instance;
}
