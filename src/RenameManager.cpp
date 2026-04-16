#include "RenameManager.h"
#include "ListView.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

RenameManager* RenameManager::m_instance = nullptr;

RenameManager* RenameManager::instance() {
    if (!m_instance) {
        m_instance = new RenameManager();
    }
    return m_instance;
}

RenameManager::RenameManager(QObject* parent)
    : QObject(parent)
{
}

RenameManager::~RenameManager() {
    if (m_editor) {
        m_editor->deleteLater();
    }
}

void RenameManager::startRename(ListItem* item) {
    if (!item) return;

    // If another rename is active, finish it first
    if (m_activeItem) {
        finishRename();
    }

    m_activeItem = item;
    
    // Preparation
    ListItemData data = item->getItemData();
    QString editorText = data.displayNameForRename();
    m_originalDisplayName = editorText;
    m_extensionToRestore = data.extensionForRename();

    // Create or show editor
    if (!m_editor) {
        m_editor = new FilenameEditor(nullptr); // Will be parented to ListView manually or stay top-level
        connect(m_editor, &FilenameEditor::commitRequested, this, &RenameManager::onCommit);
        connect(m_editor, &FilenameEditor::cancelRequested, this, &RenameManager::onCancel);
        connect(m_editor, &QTextEdit::textChanged, this, [this]() {
            if (!m_activeItem || !m_editor) {
                return;
            }

            m_activeItem->setExpandedPreviewText(m_editor->text());

            ListView* lv = ListView::instance();
            if (!lv) {
                return;
            }

            const QPoint itemTopLeft = lv->mapFromGlobal(m_activeItem->mapToGlobal(QPoint(0, 0)));
            const QRect localEditorRect = m_activeItem->renameEditorRect();
            m_editor->setGeometry(QRect(itemTopLeft + localEditorRect.topLeft(), localEditorRect.size()));
            m_editor->adjustHeightToContents();

            QRect editorGeometry = m_editor->geometry();
            editorGeometry.setTopLeft(itemTopLeft + localEditorRect.topLeft());
            m_editor->setGeometry(editorGeometry);
            m_editor->raise();
        });
    }

    // Set Parent to ListView to avoid clipping by ListItem, but position over the Item
    ListView* lv = ListView::instance();
    m_editor->setParent(lv);
    
    const QPoint itemTopLeft = lv->mapFromGlobal(item->mapToGlobal(QPoint(0, 0)));
    const QRect localEditorRect = item->renameEditorRect();
    m_editor->setGeometry(QRect(itemTopLeft + localEditorRect.topLeft(), localEditorRect.size()));
    m_editor->setInitialText(editorText, data.fullName());
    m_editor->adjustHeightToContents();
    QRect editorGeometry = m_editor->geometry();
    editorGeometry.setTopLeft(itemTopLeft + localEditorRect.topLeft());
    m_editor->setGeometry(editorGeometry);
    m_editor->show();
    m_editor->raise();
    m_editor->viewport()->raise();
    m_editor->setFocusPolicy(Qt::StrongFocus);
    m_editor->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_editor->setFocus();

    item->update(); // Trigger repaint to hide the static label
}

void RenameManager::cancelRename() {
    if (!m_activeItem) return;

    if (m_editor) {
        m_editor->hide();
    }

    ListItem* item = m_activeItem.data();
    m_activeItem = nullptr;
    m_originalDisplayName.clear();
    
    if (item) {
        item->setExpandedPreviewText(QString());
        item->update();
    }
}

void RenameManager::finishRename() {
    if (!m_activeItem || !m_editor) return;

    const QString editedText = m_editor->text().trimmed();
    QString oldPath = m_activeItem->filePath();
    
    m_editor->hide();
    ListItem* item = m_activeItem.data();
    m_activeItem = nullptr;
    const QString originalDisplayName = m_originalDisplayName;
    m_originalDisplayName.clear();

    if (!item) {
        return;
    }

    item->setExpandedPreviewText(QString());

    if (editedText.isEmpty() || editedText == originalDisplayName) {
        item->update();
        return;
    }

    // Reconstruct full new path
    QFileInfo oldInfo(oldPath);
    QString newFileName = editedText;
    if (!m_extensionToRestore.isEmpty()) {
        newFileName += m_extensionToRestore;
    }
    
    // If it wasn't a shortcut, we might need to preserve the original extension if the user didn't type it
    // But usually in Windows, if you edit foo.txt to bar, it becomes bar.txt unless you show extensions.
    // Our logic currently shows full extension for non-shortcuts.
    
    QString newPath = oldInfo.absoluteDir().filePath(newFileName);
    
    if (oldPath != newPath) {
        if (QFile::rename(oldPath, newPath)) {
            qDebug() << "[RenameManager] Successfully renamed" << oldPath << "to" << newPath;
            // The FileSystemWatcher will trigger ListView::onFileSystemChanged, which handles the UI update.
        } else {
            qCritical() << "[RenameManager] Failed to rename" << oldPath << "to" << newPath;
        }
    }

    item->update();
}

void RenameManager::onCommit() {
    finishRename();
}

void RenameManager::onCancel() {
    cancelRename();
}
