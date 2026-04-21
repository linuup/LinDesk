#pragma once

#include <QFont>
#include <QString>

namespace AppFonts {

inline QString uiFamily()
{
    return QStringLiteral("Microsoft YaHei UI");
}

inline QString uiCssFamily()
{
    return QStringLiteral("'Microsoft YaHei UI'");
}

inline QFont uiFont(int pointSize = 9, int weight = QFont::Normal)
{
    QFont font(uiFamily(), pointSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace AppFonts
