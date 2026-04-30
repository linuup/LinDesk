#include "desktop/RenameManager.h"
#include "desktop/ListView.h"
#include "box/BoxManager.h"
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

bool RenameManager::isActivePath(const QString& filePath) const
{
    return !m_activeFilePath.isEmpty() &&
        QDir::toNativeSeparators(m_activeFilePath).compare(QDir::toNativeSeparators(filePath), Qt::CaseInsensitive) == 0;
}

void RenameManager::startRename(const QString& filePath) {
    if (filePath.isEmpty()) return;

    ListView* lv = ListView::instance();
    if (!lv) {
        return;
    }

    if (!m_activeFilePath.isEmpty()) {
        finishRename();
    }

    DesktopRootItemModel model;
    if (!lv->resolveItemModelByPath(filePath, model)) {
        return;
    }

    m_activeFilePath = QDir::toNativeSeparators(model.filePath);

    const ListItemData& data = model.itemData;
    QString editorText = data.displayNameForRename();
    m_originalDisplayName = editorText;
    m_extensionToRestore = data.extensionForRename();
    lv->setItemRenaming(m_activeFilePath, true);

    if (!m_editor) {
        m_editor = new FilenameEditor(nullptr);
        connect(m_editor, &FilenameEditor::commitRequested, this, &RenameManager::onCommit);
        connect(m_editor, &FilenameEditor::cancelRequested, this, &RenameManager::onCancel);
        connect(m_editor, &QTextEdit::textChanged, this, [this]() {
            if (m_activeFilePath.isEmpty() || !m_editor) {
                return;
            }

            ListView* lv = ListView::instance();
            if (!lv) {
                return;
            }

            lv->setItemExpandedPreviewText(m_activeFilePath, m_editor->text());

            const QRect globalEditorRect = lv->globalRenameEditorRectForPath(m_activeFilePath);
            const QRect localEditorRect(lv->mapFromGlobal(globalEditorRect.topLeft()), globalEditorRect.size());
            m_editor->setGeometry(localEditorRect);
            m_editor->adjustHeightToContents();

            QRect editorGeometry = m_editor->geometry();
            editorGeometry.moveTopLeft(localEditorRect.topLeft());
            m_editor->setGeometry(editorGeometry);
            m_editor->raise();
        });
    }

    m_editor->setParent(lv);
    const QRect globalEditorRect = lv->globalRenameEditorRectForPath(m_activeFilePath);
    const QRect localEditorRect(lv->mapFromGlobal(globalEditorRect.topLeft()), globalEditorRect.size());
    m_editor->setGeometry(localEditorRect);
    m_editor->setInitialText(editorText, data.fullName());
    m_editor->adjustHeightToContents();
    QRect editorGeometry = m_editor->geometry();
    editorGeometry.moveTopLeft(localEditorRect.topLeft());
    m_editor->setGeometry(editorGeometry);
    m_editor->show();
    m_editor->raise();
    m_editor->viewport()->raise();
    m_editor->setFocusPolicy(Qt::StrongFocus);
    m_editor->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_editor->setFocus();
}

void RenameManager::cancelRename() {
    if (m_activeFilePath.isEmpty()) return;

    if (m_editor) {
        m_editor->hide();
    }

    const QString activePath = m_activeFilePath;
    m_activeFilePath.clear();
    m_originalDisplayName.clear();

    if (ListView* lv = ListView::instance()) {
        lv->setItemExpandedPreviewText(activePath, QString());
        lv->setItemRenaming(activePath, false);
    }
}

void RenameManager::finishRename() {
    if (m_activeFilePath.isEmpty() || !m_editor) return;

    const QString editedText = m_editor->text().trimmed();
    const QString oldPath = m_activeFilePath;
    
    m_editor->hide();
    m_activeFilePath.clear();
    const QString originalDisplayName = m_originalDisplayName;
    m_originalDisplayName.clear();

    if (ListView* lv = ListView::instance()) {
        lv->setItemExpandedPreviewText(oldPath, QString());
        lv->setItemRenaming(oldPath, false);
    }

    if (editedText.isEmpty() || editedText == originalDisplayName) {
        return;
    }

    QFileInfo oldInfo(oldPath);
    QString newFileName = editedText;
    if (!m_extensionToRestore.isEmpty()) {
        newFileName += m_extensionToRestore;
    }

    QString newPath = oldInfo.absoluteDir().filePath(newFileName);
    
    if (oldPath != newPath) {
        if (QFile::rename(oldPath, newPath)) {
            qDebug() << "[RenameManager] Successfully renamed" << oldPath << "to" << newPath;
        } else {
            qCritical() << "[RenameManager] Failed to rename" << oldPath << "to" << newPath;
        }
    }
}

void RenameManager::onCommit() {
    finishRename();
}

void RenameManager::onCancel() {
    cancelRename();
}
