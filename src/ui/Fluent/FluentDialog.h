#pragma once
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <functional>
#include <QVector>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPaintEvent>
#include "ui/Fluent/FluentButton.h"

/**
 * @brief Windows 11 WinUI 3 ContentDialog implementation.
 */
class FluentDialog : public QDialog {
    Q_OBJECT
public:
    enum class ActionType {
        Primary,      // Solid Win11 Accent Blue
        Secondary,    // Neutral bordered button
        Close,        // Neutral bordered close button
        Destructive   // Solid Win11 Danger Red
    };

    struct ActionInfo {
        FluentButton* button;
        ActionType type;
        std::function<void()> callback;
    };

    explicit FluentDialog(QWidget* parent = nullptr);
    ~FluentDialog() override;

    // Configuration
    void setDialogTitle(const QString& title);
    void setMessage(const QString& msg);
    QVBoxLayout* contentLayout() const;
    void addActionButton(const QString& text, ActionType type, std::function<void()> callback = nullptr);
    void keepOpenOnce();

    // Static Helpers
    static void showInfo(QWidget* parent, const QString& title, const QString& msg);
    static void showWarning(QWidget* parent, const QString& title, const QString& msg);
    static bool confirm(QWidget* parent,
                        const QString& title,
                        const QString& msg,
                        const QString& confirmText = QString(),
                        const QString& cancelText = QString(),
                        bool destructive = false);

protected:
    void setupUi();
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QWidget* hostTopWindow() const;
    bool hasVisibleHostWindow() const;
    void rebuildButtonLayout();
    void syncDialogGeometry();

    QWidget* m_contentWidget = nullptr;   // Centered ContentDialog card
    QVBoxLayout* m_cardLayout = nullptr;  // Card outer layout (body + footer)
    QVBoxLayout* m_bodyLayout = nullptr;  // Padded body layout (title, message, custom widgets)
    QWidget* m_footerWidget = nullptr;    // Bottom #f3f3f3 command bar
    QHBoxLayout* m_buttonLayout = nullptr;// Horizontal button row
    QVBoxLayout* m_outerLayout = nullptr; // Top-level dialog layout

    QLabel* m_titleLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QVector<ActionInfo> m_actions;
    bool m_keepOpenOnce = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
