#pragma once
#include <QObject>
#include <QPointer>
#include "ListItem.h"
#include "FilenameEditor.h"

class RenameManager : public QObject {
    Q_OBJECT

public:
    static RenameManager* instance();

    void startRename(ListItem* item);
    void finishRename();
    void cancelRename();

    bool isRenaming() const { return m_activeItem != nullptr; }
    ListItem* activeItem() const { return m_activeItem.data(); }

private slots:
    void onCommit();
    void onCancel();

private:
    RenameManager(QObject* parent = nullptr);
    ~RenameManager();

    static RenameManager* m_instance;

    QPointer<ListItem> m_activeItem;
    FilenameEditor* m_editor = nullptr;
    QString m_extensionToRestore; // Reserved extension for shortcuts
    QString m_originalDisplayName;
};
