#include "app/LanguageManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {

QString toLanguageCode(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.startsWith(QStringLiteral("zh"))) {
        return QStringLiteral("zh_CN");
    }
    if (normalized.startsWith(QStringLiteral("en"))) {
        return QStringLiteral("en_US");
    }
    return QStringLiteral("en_US");
}

QStringList candidateTranslationDirectories()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        appDir + "/translations",
        QDir(appDir).absoluteFilePath("../translations"),
        QDir(appDir).absoluteFilePath("../../translations")
    };
}

} // namespace

LanguageManager& LanguageManager::instance()
{
    static LanguageManager manager;
    return manager;
}

LanguageManager::LanguageManager(QObject* parent)
    : QObject(parent)
{
}

QString LanguageManager::configPath()
{
    return QCoreApplication::applicationDirPath() + "/config.ini";
}

QString LanguageManager::normalizeLanguageCode(const QVariant& value)
{
    if (!value.isValid()) {
        return QStringLiteral("en_US");
    }

    return toLanguageCode(value.toString());
}

QString LanguageManager::configuredLanguageCode()
{
    QSettings settings(configPath(), QSettings::IniFormat);
    return normalizeLanguageCode(settings.value("General/Language", QStringLiteral("en_US")));
}

bool LanguageManager::initialize(QApplication& app)
{
    return setLanguage(app, configuredLanguageCode());
}

bool LanguageManager::loadTranslator(QApplication& app, const QString& languageCode)
{
    app.removeTranslator(&m_translator);

    if (languageCode == QStringLiteral("en_US")) {
        return true;
    }

    const QString baseName = QStringLiteral("lindesk_%1").arg(languageCode);
    for (const QString& dirPath : candidateTranslationDirectories()) {
        QDir dir(dirPath);
        if (!dir.exists()) {
            continue;
        }

        if (m_translator.load(baseName, dir.absolutePath())) {
            app.installTranslator(&m_translator);
            return true;
        }
    }

    return false;
}

bool LanguageManager::setLanguage(QApplication& app, const QString& languageCode)
{
    const QString normalizedCode = normalizeLanguageCode(languageCode);
    const bool loaded = loadTranslator(app, normalizedCode);
    if (!loaded) {
        return false;
    }

    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue("General/Language", normalizedCode);

    const bool changed = (m_currentLanguageCode != normalizedCode);
    m_currentLanguageCode = normalizedCode;
    if (changed) {
        emit languageChanged(normalizedCode);
    }
    return true;
}

bool LanguageManager::setConfiguredLanguageCode(const QString& languageCode)
{
    const QString normalizedCode = normalizeLanguageCode(languageCode);
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue("General/Language", normalizedCode);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QString LanguageManager::currentLanguageCode() const
{
    return m_currentLanguageCode;
}
