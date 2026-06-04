#pragma once
#include <QObject>
#include <QVector>
#include <QSet>
#include <QString>
#include <QStringList>
#include "desktop/DesktopRootItemState.h"

class ListViewModel : public QObject
{
    Q_OBJECT
public:
    explicit ListViewModel(QObject* parent = nullptr);
    ~ListViewModel() override = default;

    // 获取数据状态
    const QVector<DesktopRootItemState>& itemStates() const { return m_itemStates; }
    QVector<DesktopRootItemState>& itemStates() { return m_itemStates; }

    const QSet<QString>& selectedDesktopPathSet() const { return m_selectedDesktopPathSet; }
    QSet<QString>& selectedDesktopPathSet() { return m_selectedDesktopPathSet; }

    const QVector<QString>& selectedDesktopPaths() const { return m_selectedDesktopPaths; }
    QVector<QString>& selectedDesktopPaths() { return m_selectedDesktopPaths; }

    const QString& focusedDesktopPath() const { return m_focusedDesktopPath; }
    QString& focusedDesktopPath() { return m_focusedDesktopPath; }

    const QString& anchorDesktopPath() const { return m_anchorDesktopPath; }
    QString& anchorDesktopPath() { return m_anchorDesktopPath; }

    // 数据操作方法
    void clearSelection();
    void selectPath(const QString& normalizedPath, bool selected);
    void setItems(const QVector<DesktopRootItemState>& states);

    // 物理文件/IO操作下沉
    bool createShortcutsInFolder(const QStringList& sourcePaths, const QString& targetFolder);
    bool handleDropFiles(const QStringList& draggedPaths, const QString& desktopDir, bool preferShortcut, bool isCopyAction);
    bool deleteSelectedItems(const QStringList& desktopPaths, const QStringList& boxPaths, bool permanently, QStringList& outDeletedDesktopPaths, QStringList& outDeletedBoxPaths);

signals:
    void dataChanged();
    void selectionChanged();

private:
    QVector<DesktopRootItemState> m_itemStates;
    QSet<QString> m_selectedDesktopPathSet;
    QVector<QString> m_selectedDesktopPaths;
    QString m_focusedDesktopPath;
    QString m_anchorDesktopPath;
};
