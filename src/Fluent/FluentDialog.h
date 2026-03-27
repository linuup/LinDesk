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

/**
 * @brief Formal Fluent Design Dialog with Smoke Backdrop.
 * Based on Microsoft's "ContentDialog" specification.
 */
class FluentDialog : public QDialog {
    Q_OBJECT
public:
    enum class ActionType {
        Primary,    // Solid Accent
        Secondary,  // White with Border
        Close       // Transparent
    };

    struct ActionInfo {
        QPushButton* button;
        ActionType type;
        std::function<void()> callback;
    };

    explicit FluentDialog(QWidget* parent = nullptr);
    virtual ~FluentDialog();

    // Configuration
    void setDialogTitle(const QString& title);
    void setMessage(const QString& msg);
    void addActionButton(const QString& text, ActionType type, std::function<void()> callback = nullptr);

    // Static Helpers
    static void showInfo(QWidget* parent, const QString& title, const QString& msg);

protected:
    void setupUi();
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QWidget* m_background;      // Smoke layer
    QWidget* m_contentWidget;   // Centered Card
    QVBoxLayout* m_mainLayout;  // Component layout
    QHBoxLayout* m_buttonLayout; // Horizontal button row
    
    QLabel* m_titleLabel;
    QLabel* m_messageLabel;
    QVector<ActionInfo> m_actions;
};
