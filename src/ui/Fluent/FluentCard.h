#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include "app/AppFonts.h"

class FluentCard : public QWidget {
    Q_OBJECT
public:
    explicit FluentCard(const QString& iconChar, const QString& title, const QString& sub, QWidget* action = nullptr, QWidget* parent = nullptr) 
        : QWidget(parent) 
    {
        setObjectName("fluentCard");
        setAttribute(Qt::WA_StyledBackground); 
        
        setStyleSheet(
            "QWidget#fluentCard { "
            "   background: #ffffff; "
            "   border: 1px solid rgba(0, 0, 0, 0.08); "
            "   border-bottom: 1px solid rgba(0, 0, 0, 0.12); "
            "} "
            "QWidget#fluentCard:hover { "
            "   background: rgba(0, 0, 0, 0.02); "
            "} "
        );
        
        setFixedHeight(72);

        QHBoxLayout* hLayout = new QHBoxLayout(this);
        hLayout->setContentsMargins(16, 0, 16, 0);
        hLayout->setSpacing(0);

        QLabel* iconL = new QLabel(iconChar, this);
        iconL->setFixedWidth(40);
        iconL->setAlignment(Qt::AlignCenter);
        iconL->setStyleSheet(
            "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; "
            "font-size: 24px; "
            "color: #0078d4; "
            "border: none; "
            "background: transparent;"
        );

        QVBoxLayout* vText = new QVBoxLayout();
        vText->setSpacing(0);
        vText->setContentsMargins(8, 0, 0, 0);
        vText->setAlignment(Qt::AlignVCenter);

        m_mainText = new QLabel(title, this);
        m_mainText->setStyleSheet(QStringLiteral(
            "font-family: %1; "
            "font-weight: 500; "
            "font-size: 14px; "
            "color: #1f2937; "
            "border: none; "
            "background: transparent;"
        ).arg(AppFonts::uiCssFamily()));

        m_subText = new QLabel(sub, this);
        m_subText->setWordWrap(true);
        m_subText->setStyleSheet(QStringLiteral(
            "font-family: %1; "
            "font-size: 12px; "
            "color: #616161; "
            "border: none; "
            "background: transparent;"
        ).arg(AppFonts::uiCssFamily()));

        vText->addWidget(m_mainText);
        vText->addWidget(m_subText);

        hLayout->addWidget(iconL);
        hLayout->addLayout(vText);
        hLayout->addStretch();
        
        m_actionsLayout = new QHBoxLayout();
        m_actionsLayout->setSpacing(10);
        hLayout->addLayout(m_actionsLayout);

        if (action) {
            addActionWidget(action);
        }
    }

    void addActionWidget(QWidget* w) {
        if (!w) return;
        w->setParent(this);
        m_actionsLayout->addWidget(w);
    }

    void setTitle(const QString& title) {
        if (m_mainText) {
            m_mainText->setText(title);
        }
    }

    void setSubtitle(const QString& subtitle) {
        if (m_subText) {
            m_subText->setText(subtitle);
        }
    }

private:
    QHBoxLayout* m_actionsLayout;
    QLabel* m_mainText = nullptr;
    QLabel* m_subText = nullptr;
};
