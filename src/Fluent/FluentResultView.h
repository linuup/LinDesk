#pragma once
#include "FluentBaseWidget.h"
#include <QTextEdit>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QGuiApplication>
#include <QClipboard>

/**
 * @brief Reusable Result Page for Fluent UI.
 * Used for OCR results, file info summaries, etc.
 */
class FluentResultView : public FluentBaseWidget {
    Q_OBJECT
public:
    explicit FluentResultView(const QString& titleText, const QString& iconCode = u8"\uEBD2", QWidget* parent = nullptr) 
        : FluentBaseWidget(parent) 
    {
        // 1. Use the built-in, robust base class buttons for window management
        setWindowBtnMode(CloseOnly); 
        resize(480, 560);
        
        QVBoxLayout* layout = centralLayout();
        layout->setContentsMargins(25, 0, 25, 25);
        layout->setSpacing(15);
        
        // 2. Header (Integrated with Title Area)
        QHBoxLayout* header = new QHBoxLayout();
        QLabel* icon = new QLabel(iconCode, container());
        icon->setStyleSheet("font-family: 'Segoe MDL2 Assets'; font-size: 20px; color: #0078d4;");
        
        QLabel* title = new QLabel(titleText, container());
        title->setStyleSheet("font-size: 18px; font-weight: 600; color: #1f2937;");
        
        header->addWidget(icon);
        header->addWidget(title);
        header->addStretch();
        layout->addLayout(header);

        // 3. Text Area
        m_textEdit = new QTextEdit(container());
        m_textEdit->setPlaceholderText("No result data...");
        m_textEdit->setStyleSheet(
            "QTextEdit { "
            "   background: rgba(0, 0, 0, 0.03); "
            "   border: 1px solid rgba(0, 0, 0, 0.06); "
            "   border-radius: 8px; "
            "   font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; "
            "   font-size: 14px; "
            "   line-height: 1.6; "
            "   color: #1f2937; "
            "   padding: 10px; "
            "} "
            "QScrollBar:vertical { border: none; background: transparent; width: 4px; } "
            "QScrollBar::handle:vertical { background: rgba(0, 0, 0, 0.1); border-radius: 2px; } "
        );
        layout->addWidget(m_textEdit, 1);

        // 4. Divider
        QFrame* line = new QFrame(container());
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("background-color: rgba(0, 0, 0, 0.06); height: 1px; border: none;");
        layout->addWidget(line);

        // 5. Actions Footer
        m_btnLayout = new QHBoxLayout();
        m_btnLayout->addStretch();
        layout->addLayout(m_btnLayout);

        // Default Copy Action
        addFooterAction("Copy to Clipboard", [this]() {
            QGuiApplication::clipboard()->setText(m_textEdit->toPlainText());
            m_textEdit->selectAll();
        }, true);
    }

    void setResultText(const QString& text) {
        m_textEdit->setPlainText(text);
        show();
        raise();
        activateWindow();
    }

    QTextEdit* textEdit() const { return m_textEdit; }

    void addFooterAction(const QString& label, const std::function<void()>& callback, bool primary = false) {
        QPushButton* btn = new QPushButton(label, container());
        btn->setCursor(Qt::PointingHandCursor);
        if (primary) {
            btn->setStyleSheet(
                "QPushButton { background-color: #0078d4; color: white; border-radius: 6px; padding: 8px 16px; font-weight: 500; } "
                "QPushButton:hover { background-color: #006cbd; } "
            );
        } else {
            btn->setStyleSheet(
                "QPushButton { background-color: #ffffff; color: #1f2937; border: 1px solid #e5e7eb; border-radius: 6px; padding: 8px 16px; font-weight: 500; } "
                "QPushButton:hover { background-color: #f9fafb; border-color: #d1d5db; } "
            );
        }
        connect(btn, &QPushButton::clicked, this, callback);
        m_btnLayout->addWidget(btn);
    }

private:
    QTextEdit* m_textEdit = nullptr;
    QHBoxLayout* m_btnLayout = nullptr;
};
