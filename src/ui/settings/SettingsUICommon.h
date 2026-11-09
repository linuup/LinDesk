#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QString>
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QImageReader>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include "app/AppFonts.h"
#include "services/AppPaths.h"
#include "ui/Fluent/FluentToggleSwitch.h"

namespace SettingsUICommon {

inline QString glyph(char16_t code) {
    return QString(QChar(code));
}

inline QString settingsConfigPath() {
    return AppPaths::configFilePath();
}

inline QRect centeredPreviewImageRect(const QSize& size) {
    const int imageWidth = qMax(1, size.width() * 90 / 100);
    return QRect((size.width() - imageWidth) / 2, 0, imageWidth, size.height());
}

QLabel* createPageTitle(const QString& text, QWidget* parent);
QLabel* createPageSubtitle(const QString& text, QWidget* parent);
QLabel* createCardTitle(const QString& text, QWidget* parent);
QLabel* createCardSubtitle(const QString& text, QWidget* parent);
QWidget* createPanel(QWidget* parent, const QString& objectName = QStringLiteral("settingsPanel"));
QLabel* createGlyphLabel(const QString& glyphText, QWidget* parent, int pixelSize = 18, const QColor& color = QColor(0x1b, 0x1b, 0x1b));
QFrame* createDivider(QWidget* parent);
QPushButton* createSettingsActionButton(const QString& text, QWidget* parent, bool accent = false, bool destructive = false);
QWidget* createSwitchRow(const QString& icon, const QString& title, const QString& subtitle, FluentToggleSwitch* toggle, QWidget* parent);
QPushButton* createColorPreset(const QColor& color, QWidget* parent);
void updateColorPresetIndicators(QWidget* container, const QColor& selectedColor);
QWidget* createHomeTile(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent);
QWidget* createQuickAction(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent);
QWidget* createPluginBadge(const QString& icon, const QString& text, QWidget* parent, const QColor& color);

QString settingsWidgetAccentColor(const QString& id);
QString settingsWidgetIconGlyph(const QString& id);
QString settingsDefaultWidgetIconPath();
bool settingsIsSafeRelativeWidgetPath(const QString& path);
bool settingsCanLoadIconImage(const QString& path);
QString settingsResolveWidgetIconPath(const QString& packagePath, const QString& iconPath);
QLabel* createWidgetIconLabel(const QString& id, const QString& iconPath, QWidget* parent, int size = 56);
QLabel* createWidgetMetaLabel(const QString& title, const QString& value, QWidget* parent);
QPixmap getDesktopWallpaperThumbnail(const QSize& logicalSize, qreal dpr);

} // namespace SettingsUICommon
