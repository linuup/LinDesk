#include "SettingUI_GL.h"
#include "LinBox.h"
#include "ListView.h"
#include <QPainter>
#include <QMenu>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QListWidget>
#include "Fluent/FluentToggleSwitch.h"
#include "Fluent/FluentButton.h"
#include "Fluent/FluentCard.h"
#include "Fluent/FluentComboBox.h"
#include <QSettings>
#include <QCoreApplication>
#include <QStyledItemDelegate>
#include <QPainterPath>
#include <QTimer>

// Optimized GL Internal Renderer
class GLBackgroundWidget : public QOpenGLWidget {
public:
    using QOpenGLWidget::QOpenGLWidget;
protected:
    void paintEvent(QPaintEvent* e) override {
        // We use a custom painter on the GL surface for high-perf rendering
        QOpenGLWidget::paintEvent(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        // Example: Draw a slight hardware-accelerated gradient
        QLinearGradient grad(rect().topLeft(), rect().bottomRight());
        grad.setColorAt(0, QColor(243, 243, 243));
        grad.setColorAt(1, QColor(255, 255, 255));
        p.fillRect(rect(), grad);
    }
};

// --- Reusing Sidebar Delegate Logic ---
class SidebarDelegateGL : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        bool isSelected = (opt.state & QStyle::State_Selected);
        if (isSelected) {
            painter->setBrush(QColor(0, 0, 0, 10));
            painter->drawRoundedRect(opt.rect.adjusted(6, 2, -6, -2), 4, 4);
            painter->setBrush(QColor(0, 95, 184));
            painter->drawRoundedRect(QRect(opt.rect.left() + 2, opt.rect.top() + 10, 3, 16), 1.5, 1.5);
        }
        painter->drawText(opt.rect.adjusted(48, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

SettingUI_GL::SettingUI_GL(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent)
    : FluentBaseWidget(parent), m_targetBox(targetBox), m_allBoxes(allBoxes)
{
    resize(760, 540);
    setWindowTitle("OpenGL Hardware Settings");
    initUI();
    setupStyles();
}

void SettingUI_GL::initUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout();
    centralLayout()->addLayout(mainLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Sidebar
    QWidget* sidebarContainer = new QWidget(container());
    sidebarContainer->setObjectName("sidebarContainerGL");
    sidebarContainer->setFixedWidth(200);
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(0, 20, 0, 10);

    m_sidebar = new QListWidget(container());
    m_sidebar->setObjectName("sidebarGL");
    m_sidebar->setFrameShape(QFrame::NoFrame);
    
    QStringList items = {"GeneralGL", "Hardware", "Desktop", "Performance", "About"};
    m_sidebar->addItems(items);
    m_sidebar->setItemDelegate(new SidebarDelegateGL(m_sidebar));
    sidebarLayout->addWidget(m_sidebar);

    // GL Context Area (Right)
    GLBackgroundWidget* rightArea = new GLBackgroundWidget(container());
    rightArea->setObjectName("rightAreaGL");
    QVBoxLayout* rightLayout = new QVBoxLayout(rightArea);
    
    m_contentStack = new QStackedWidget(container());
    m_contentStack->addWidget(createGeneralPage());
    m_contentStack->addWidget(createOpenGLPage());
    m_contentStack->addWidget(createDesktopPage());
    
    rightLayout->addWidget(m_contentStack);

    mainLayout->addWidget(sidebarContainer);
    mainLayout->addWidget(rightArea);
    
    connect(m_sidebar, &QListWidget::currentRowChanged, m_contentStack, &QStackedWidget::setCurrentIndex);
}

void SettingUI_GL::setupStyles() {
    setStyleSheet(
        "SettingUI_GL { background-color: #f3f3f3; border-radius: 12px; } "
        "QWidget#sidebarContainerGL { background-color: #f9f9f9; border-right: 1px solid rgba(0,0,0,0.1); border-bottom-left-radius: 11px; } "
        "QListWidget#sidebarGL { background: transparent; border: none; } "
        "QListWidget#sidebarGL::item { height: 36px; } "
    );
}

QWidget* SettingUI_GL::createGeneralPage() {
    QWidget* p = new QWidget();
    QVBoxLayout* l = new QVBoxLayout(p);
    l->addWidget(new QLabel("General Hardware Settings (OpenGL Version)"));
    l->addWidget(new FluentCard(u8"\uE7E6", "Enable Acceleration", "Activate hardware renderer for the entire UI", new FluentToggleSwitch(p), p));
    l->addStretch();
    return p;
}

QWidget* SettingUI_GL::createOpenGLPage() {
    QWidget* p = new QWidget();
    QVBoxLayout* l = new QVBoxLayout(p);
    l->addWidget(new QLabel("OpenGL Configuration"));
    l->addWidget(new FluentCard(u8"\uE756", "V-Sync", "Synchronize refresh rates to eliminate screen tearing", new FluentToggleSwitch(p), p));
    l->addWidget(new FluentCard(u8"\uE921", "Texture Filtering", "Use high-quality sampling for desktop icons", new FluentToggleSwitch(p), p));
    l->addStretch();
    return p;
}

QWidget* SettingUI_GL::createDesktopPage() {
    QWidget* p = new QWidget();
    QVBoxLayout* l = new QVBoxLayout(p);
    l->addWidget(new QLabel("Advanced Desktop Visuals"));
    l->addStretch();
    return p;
}

void SettingUI_GL::setTargetBox(LinBox* target) { m_targetBox = target; }
void SettingUI_GL::closeEvent(QCloseEvent* event) { this->hide(); event->ignore(); }

#include "moc_SettingUI_GL.cpp"
