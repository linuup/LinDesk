#pragma once

#include <QWidget>
#include <QPointer>
#include <QListWidget>

class ContextMenuSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit ContextMenuSettingsPage(QWidget* parent = nullptr);
    ~ContextMenuSettingsPage() override = default;

    void updateTranslations() {}

    void ensureInitialSelection();

private:
    void initUI();

    QPointer<QListWidget> m_shellTypeList;
};
