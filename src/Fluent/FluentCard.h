#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

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
            "   border-bottom: 1px solid rgba(0, 0, 0, 0.15); " 
            "   border-radius: 8px; "
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
        iconL->setFixedWidth(32);
        iconL->setAlignment(Qt::AlignCenter);
        iconL->setStyleSheet("font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 18px; color: #005fb8; border: none; background: transparent;");
        
        QVBoxLayout* vText = new QVBoxLayout();
        vText->setSpacing(0); 
        vText->setContentsMargins(16, 0, 0, 0);
        vText->setAlignment(Qt::AlignVCenter);
        
        QLabel* mainText = new QLabel(title, this);
        mainText->setStyleSheet("font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; font-weight: 500; font-size: 14px; color: #1c1c1c; border: none; background: transparent;");
        
        QLabel* subText = new QLabel(sub, this);
        subText->setWordWrap(true); 
        subText->setStyleSheet("font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif; font-size: 12px; color: #616161; border: none; background: transparent;");
        
        vText->addWidget(mainText);
        vText->addWidget(subText);
        
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

private:
    QHBoxLayout* m_actionsLayout;
};
