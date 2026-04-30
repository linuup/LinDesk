#pragma once
#include <QObject>
#include <QString>
#include "desktop/FilenameEditor.h"

class RenameManager : public QObject {
    Q_OBJECT

public:
    static RenameManager* instance();

    void startRename(const QString& filePath);
    void finishRename();
    void cancelRename();

    bool isRenaming() const { return !m_activeFilePath.isEmpty(); }
    QString activeFilePath() const { return m_activeFilePath; }
    bool isActivePath(const QString& filePath) const;

private slots:
    void onCommit();
    void onCancel();

private:
    RenameManager(QObject* parent = nullptr);
    ~RenameManager();

    static RenameManager* m_instance;

    QString m_activeFilePath;
    FilenameEditor* m_editor = nullptr;
    QString m_extensionToRestore; // Reserved extension for shortcuts
    QString m_originalDisplayName;
};
