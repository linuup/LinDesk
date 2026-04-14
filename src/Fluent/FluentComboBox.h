#pragma once
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QAbstractItemView>

class FluentComboBoxDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // Background
        if (opt.state & QStyle::State_Selected) {
            painter->setBrush(QColor(0, 120, 212)); // Fluent Blue #0078d4
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(opt.rect.adjusted(4, 2, -4, -2), 4, 4);
        } else if (opt.state & QStyle::State_MouseOver) {
            painter->fillRect(opt.rect.adjusted(4, 2, -4, -2), QColor(0, 0, 0, 8));
        }

        // Text
        painter->setPen((opt.state & QStyle::State_Selected) ? Qt::white : QColor(31, 41, 55));
        QFont font = painter->font();
        font.setPixelSize(13);
        painter->setFont(font);
        
        QRect textRect = opt.rect.adjusted(12, 0, -12, 0);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, index.data().toString());

        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return QSize(option.rect.width(), 36);
    }
};

class FluentComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit FluentComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
        setFixedSize(200, 32);
        setItemDelegate(new FluentComboBoxDelegate(this));
        
        // Disable default arrow with stylesheet and apply modern look
        setStyleSheet(
            "QComboBox { "
            "   background: #ffffff; border: 1px solid #e5e5e5; border-radius: 4px; "
            "   padding-left: 12px; font-size: 13px; color: #333; "
            "} "
            "QComboBox:hover { background: #fafafa; border: 1px solid #d0d0d0; } "
            "QComboBox::drop-down { border: none; width: 30px; } "
            "QComboBox::down-arrow { image: none; } " // We'll paint our own
            "QAbstractItemView { "
            "   background-color: white; border: 1px solid #e5e5e5; "
            "   border-radius: 8px; outline: 0px; padding: 4px; "
            "} "
        );
        
        // Ensure pop-up uses frameless/shadow if possible (system-dependent, but padding helps)
        view()->window()->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        view()->window()->setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QComboBox::paintEvent(e);
        
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(100, 100, 100));
        
        // Paint Fluent Chevron Down icon
        QFont f("Segoe MDL2 Assets");
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(rect().adjusted(0,0,-12,0), Qt::AlignVCenter | Qt::AlignRight, u8"\uE70D");
    }
};
