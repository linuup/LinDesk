#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVariant>

class QApplication;

class LanguageManager : public QObject
{
    Q_OBJECT
public:
    static LanguageManager& instance();

    static QString configPath();
    static QString normalizeLanguageCode(const QVariant& value);
    static QString configuredLanguageCode();

    bool initialize(QApplication& app);
    bool setLanguage(QApplication& app, const QString& languageCode);
    bool setConfiguredLanguageCode(const QString& languageCode);
    QString currentLanguageCode() const;

signals:
    void languageChanged(const QString& languageCode);

private:
    explicit LanguageManager(QObject* parent = nullptr);
    bool loadTranslator(QApplication& app, const QString& languageCode);

    QTranslator m_translator;
    QString m_currentLanguageCode = QStringLiteral("en_US");
};
