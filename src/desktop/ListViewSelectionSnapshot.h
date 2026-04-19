#pragma once

#include <QString>
#include <QStringList>

class ListView;

class ListViewSelectionSnapshot
{
public:
    static ListViewSelectionSnapshot capture(const ListView& view);
    void restore(ListView& view, bool expandSingle = false) const;

private:
    QStringList m_selectedPaths;
    QString m_focusedPath;
    QString m_anchorPath;
};
