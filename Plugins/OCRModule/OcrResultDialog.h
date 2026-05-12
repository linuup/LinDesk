#pragma once

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QStyleHints>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>

struct OcrTextBox
{
    QString text;
    QRectF imageRect;
};

class OcrResultImageView : public QLabel
{
    Q_OBJECT

public:
    explicit OcrResultImageView(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignTop | Qt::AlignLeft);
        setBackgroundRole(QPalette::Base);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setScaledContents(false);
    }

    void setImage(const QImage& image)
    {
        m_image = image;
        refreshDisplayPixmap();
        update();
    }

    void setBoxes(const QVector<OcrTextBox>& boxes)
    {
        m_boxes = boxes;
        update();
    }

    void setViewportSize(const QSize& viewportSize)
    {
        m_viewportSize = viewportSize;
        if (m_fitToViewport) {
            refreshDisplayPixmap();
        }
    }

    void setFitToViewport(bool fit)
    {
        if (m_fitToViewport == fit) {
            return;
        }
        if (!fit) {
            m_manualScale = currentScale();
        }
        m_fitToViewport = fit;
        refreshDisplayPixmap();
    }

    bool fitToViewport() const
    {
        return m_fitToViewport;
    }

    bool hasBoxAt(const QPoint& pos) const
    {
        for (int index = m_boxes.size() - 1; index >= 0; --index) {
            if (mappedRect(m_boxes.at(index).imageRect).contains(pos)) {
                return true;
            }
        }
        return false;
    }

    void setManualScale(double scale)
    {
        m_fitToViewport = false;
        m_manualScale = qBound(0.10, scale, 8.0);
        refreshDisplayPixmap();
    }

    double manualScale() const
    {
        return m_manualScale;
    }

    double currentScale() const
    {
        if (m_image.isNull() || m_image.width() <= 0 || m_displaySize.width() <= 0) {
            return 1.0;
        }
        return static_cast<double>(m_displaySize.width()) / static_cast<double>(m_image.width());
    }

signals:
    void textCopied(const QString& text);

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (!event || event->button() != Qt::LeftButton) {
            QLabel::mousePressEvent(event);
            return;
        }

        m_pressedBoxIndex = -1;
        m_pressPos = event->pos();
        m_pressMoved = false;
        for (int index = m_boxes.size() - 1; index >= 0; --index) {
            if (mappedRect(m_boxes.at(index).imageRect).contains(event->pos())) {
                m_pressedBoxIndex = index;
                event->accept();
                return;
            }
        }

        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event && m_pressedBoxIndex >= 0
            && (event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
            m_pressMoved = true;
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!event || event->button() != Qt::LeftButton || m_pressedBoxIndex < 0) {
            QLabel::mouseReleaseEvent(event);
            return;
        }

        const int boxIndex = m_pressedBoxIndex;
        m_pressedBoxIndex = -1;
        if (!m_pressMoved && boxIndex < m_boxes.size()
            && mappedRect(m_boxes.at(boxIndex).imageRect).contains(event->pos())) {
            const QString text = m_boxes.at(boxIndex).text.trimmed();
            if (!text.isEmpty() && QApplication::clipboard()) {
                QApplication::clipboard()->setText(text);
                emit textCopied(text);
            }
            event->accept();
            return;
        }

        QLabel::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QLabel::paintEvent(event);
        if (m_image.isNull() || m_boxes.isEmpty()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(11, 127, 143, 220), 2.0));
        painter.setBrush(QColor(11, 127, 143, 42));
        for (const OcrTextBox& box : m_boxes) {
            const QRectF rect = mappedRect(box.imageRect);
            if (rect.isValid()) {
                painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
            }
        }
    }

private:
    void refreshDisplayPixmap()
    {
        if (m_image.isNull()) {
            clear();
            m_displaySize = QSize();
            resize(QSize(1, 1));
            return;
        }

        QSize targetSize = m_image.size();
        if (m_fitToViewport && m_viewportSize.isValid()) {
            const int maxW = qMax(1, m_viewportSize.width() - 10);
            const int maxH = qMax(1, m_viewportSize.height() - 10);
            const double sx = static_cast<double>(maxW) / static_cast<double>(m_image.width());
            const double sy = static_cast<double>(maxH) / static_cast<double>(m_image.height());
            const double scale = qMin(sx, sy);
            targetSize = QSize(
                qMax(1, static_cast<int>(m_image.width() * scale)),
                qMax(1, static_cast<int>(m_image.height() * scale)));
        } else {
            targetSize = QSize(
                qMax(1, static_cast<int>(m_image.width() * m_manualScale)),
                qMax(1, static_cast<int>(m_image.height() * m_manualScale)));
        }

        const QImage scaled = m_image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_displaySize = scaled.size();
        setPixmap(QPixmap::fromImage(scaled));
        resize(m_displaySize);
        setMinimumSize(m_displaySize);
        setMaximumSize(m_displaySize);
    }

    QRectF mappedRect(const QRectF& imageRect) const
    {
        if (m_image.isNull() || m_displaySize.width() <= 0 || m_displaySize.height() <= 0) {
            return QRectF();
        }

        const double scaleX = static_cast<double>(m_displaySize.width()) / static_cast<double>(m_image.width());
        const double scaleY = static_cast<double>(m_displaySize.height()) / static_cast<double>(m_image.height());
        return QRectF(imageRect.left() * scaleX,
                      imageRect.top() * scaleY,
                      imageRect.width() * scaleX,
                      imageRect.height() * scaleY);
    }

    QImage m_image;
    QVector<OcrTextBox> m_boxes;
    QSize m_viewportSize;
    QSize m_displaySize;
    bool m_fitToViewport = true;
    bool m_pressMoved = false;
    int m_pressedBoxIndex = -1;
    QPoint m_pressPos;
    double m_manualScale = 1.0;
};

class OcrResultDialog : public QWidget
{
    Q_OBJECT

public:
    explicit OcrResultDialog(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_imageView(new OcrResultImageView(this))
        , m_textEdit(new QPlainTextEdit(this))
    {
        setAttribute(Qt::WA_DeleteOnClose, false);
        setObjectName(QStringLiteral("OcrResultWindow"));
        setWindowTitle(QStringLiteral("OCR Intelligence Result"));
        resize(1180, 760);

        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        auto* contentSplitter = new QSplitter(Qt::Horizontal, this);
        contentSplitter->setObjectName(QStringLiteral("OcrContentSplitter"));
        contentSplitter->setChildrenCollapsible(false);
        contentSplitter->setHandleWidth(1);
        rootLayout->addWidget(contentSplitter, 1);

        auto* imagePanel = new QWidget(this);
        imagePanel->setObjectName(QStringLiteral("OcrImagePane"));
        auto* imagePanelLayout = new QVBoxLayout(imagePanel);
        imagePanelLayout->setContentsMargins(0, 0, 0, 0);
        imagePanelLayout->setSpacing(0);
        auto* imageTitleBar = new QHBoxLayout();
        imageTitleBar->setSpacing(6);
        imageTitleBar->setContentsMargins(10, 8, 10, 8);
        imageTitleBar->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        m_fitButton = new QPushButton(QStringLiteral("Fit"), imagePanel);
        m_fitButton->setObjectName(QStringLiteral("OcrMiniButton"));
        m_zoomOutButton = new QPushButton(QStringLiteral("-"), imagePanel);
        m_zoomOutButton->setObjectName(QStringLiteral("OcrMiniButton"));
        m_zoomInButton = new QPushButton(QStringLiteral("+"), imagePanel);
        m_zoomInButton->setObjectName(QStringLiteral("OcrMiniButton"));
        m_actualButton = new QPushButton(QStringLiteral("1:1"), imagePanel);
        m_actualButton->setObjectName(QStringLiteral("OcrMiniButton"));
        imageTitleBar->addStretch(1);
        imageTitleBar->addWidget(m_fitButton);
        imageTitleBar->addWidget(m_zoomOutButton);
        imageTitleBar->addWidget(m_zoomInButton);
        imageTitleBar->addWidget(m_actualButton);
        imageTitleBar->addStretch(1);
        imagePanelLayout->addLayout(imageTitleBar);

        m_imageScrollArea = new QScrollArea(this);
        m_imageScrollArea->setObjectName(QStringLiteral("OcrImageScrollArea"));
        m_imageScrollArea->setWidgetResizable(false);
        m_imageScrollArea->setAlignment(Qt::AlignCenter);
        m_imageScrollArea->setWidget(m_imageView);
        m_imageScrollArea->setMinimumWidth(680);
        m_imageScrollArea->viewport()->installEventFilter(this);
        m_imageView->installEventFilter(this);
        imagePanelLayout->addWidget(m_imageScrollArea, 1);
        contentSplitter->addWidget(imagePanel);

        auto* textPanel = new QWidget(this);
        textPanel->setObjectName(QStringLiteral("OcrTextPane"));
        textPanel->setMinimumWidth(220);
        textPanel->setMaximumWidth(340);
        auto* textPanelLayout = new QVBoxLayout(textPanel);
        textPanelLayout->setContentsMargins(0, 0, 0, 0);
        textPanelLayout->setSpacing(0);

        m_textEdit->setObjectName(QStringLiteral("OcrTextEdit"));
        m_textEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        m_textEdit->setPlaceholderText(QStringLiteral("Recognized text will appear here."));
        m_textEdit->document()->setDocumentMargin(12);
        m_textEdit->setTabStopDistance(32);
        QFont textFont(QStringLiteral("Segoe UI"));
        textFont.setPixelSize(14);
        m_textEdit->setFont(textFont);
        m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        textPanelLayout->addWidget(m_textEdit, 1);
        contentSplitter->addWidget(textPanel);
        contentSplitter->setStretchFactor(0, 6);
        contentSplitter->setStretchFactor(1, 2);
        contentSplitter->setSizes({920, 260});

        m_copyToast = new QLabel(QStringLiteral("Copied to clipboard"), this);
        m_copyToast->setObjectName(QStringLiteral("OcrCopyToast"));
        m_copyToast->setAlignment(Qt::AlignCenter);
        m_copyToast->hide();
        m_copyToast->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_copyToast->setMinimumHeight(40);
        m_copyToast->setMinimumWidth(220);
        m_copyToast->adjustSize();
        m_copyToast->raise();

        m_copyToastTimer = new QTimer(this);
        m_copyToastTimer->setSingleShot(true);
        connect(m_copyToastTimer, &QTimer::timeout, this, [this]() {
            if (m_copyToast) {
                m_copyToast->hide();
            }
        });

        connect(m_imageView, &OcrResultImageView::textCopied, this, [this](const QString&) {
            showCopyToast();
        });

        connect(m_fitButton, &QPushButton::clicked, this, [this]() {
            if (!m_imageView || !m_imageScrollArea) {
                return;
            }
            m_imageView->setFitToViewport(true);
            m_imageView->setViewportSize(m_imageScrollArea->viewport()->size());
            updateImageModeButtons();
        });
        connect(m_actualButton, &QPushButton::clicked, this, [this]() {
            if (!m_imageView) {
                return;
            }
            m_imageView->setFitToViewport(false);
            updateImageModeButtons();
        });
        connect(m_zoomOutButton, &QPushButton::clicked, this, [this]() {
            zoomImage(1.0 / 1.18);
        });
        connect(m_zoomInButton, &QPushButton::clicked, this, [this]() {
            zoomImage(1.18);
        });

        setStyleSheet(
            "#OcrResultWindow {"
            "  background: #ffffff;"
            "}"
            "#OcrImagePane {"
            "  background: #ffffff;"
            "}"
            "#OcrTextPane {"
            "  background: #ffffff;"
            "}"
            "#OcrCopyToast {"
            "  background: rgba(22, 40, 55, 228);"
            "  color: #ffffff;"
            "  border: 1px solid rgba(143, 203, 212, 230);"
            "  border-radius: 11px;"
            "  padding: 8px 20px;"
            "  font-family: 'Segoe UI';"
            "  font-size: 14px;"
            "  font-weight: 600;"
            "}"
            "#OcrImageScrollArea {"
            "  background: #ffffff;"
            "  border: none;"
            "}"
            "#OcrTextEdit {"
            "  background: #ffffff;"
            "  color: #14283b;"
            "  border: none;"
            "  border-left: 1px solid #d8e4ec;"
            "  selection-background-color: #cceff3;"
            "  selection-color: #102536;"
            "}"
            "#OcrImageScrollArea QScrollBar:vertical, #OcrTextEdit QScrollBar:vertical {"
            "  background: transparent;"
            "  width: 10px;"
            "  margin: 8px 3px 8px 3px;"
            "}"
            "#OcrImageScrollArea QScrollBar:horizontal, #OcrTextEdit QScrollBar:horizontal {"
            "  background: transparent;"
            "  height: 10px;"
            "  margin: 3px 8px 3px 8px;"
            "}"
            "#OcrImageScrollArea QScrollBar::handle:vertical, #OcrTextEdit QScrollBar::handle:vertical {"
            "  background: #b7c9d6;"
            "  border-radius: 5px;"
            "  min-height: 46px;"
            "}"
            "#OcrImageScrollArea QScrollBar::handle:horizontal, #OcrTextEdit QScrollBar::handle:horizontal {"
            "  background: #b7c9d6;"
            "  border-radius: 5px;"
            "  min-width: 46px;"
            "}"
            "#OcrImageScrollArea QScrollBar::handle:vertical:hover, #OcrTextEdit QScrollBar::handle:vertical:hover, "
            "#OcrImageScrollArea QScrollBar::handle:horizontal:hover, #OcrTextEdit QScrollBar::handle:horizontal:hover {"
            "  background: #86a9be;"
            "}"
            "#OcrImageScrollArea QScrollBar::add-line, #OcrImageScrollArea QScrollBar::sub-line, "
            "#OcrTextEdit QScrollBar::add-line, #OcrTextEdit QScrollBar::sub-line {"
            "  width: 0px;"
            "  height: 0px;"
            "  background: transparent;"
            "  border: none;"
            "}"
            "#OcrImageScrollArea QScrollBar::add-page, #OcrImageScrollArea QScrollBar::sub-page, "
            "#OcrTextEdit QScrollBar::add-page, #OcrTextEdit QScrollBar::sub-page {"
            "  background: transparent;"
            "}"
            "QPushButton {"
            "  background: #ffffff;"
            "  color: #17324b;"
            "  border: 1px solid #cbd9e3;"
            "  border-radius: 9px;"
            "  padding: 7px 14px;"
            "  font-family: 'Segoe UI';"
            "  font-size: 13px;"
            "}"
            "QPushButton:hover {"
            "  background: #eef6f8;"
            "  border-color: #8fcbd4;"
            "}"
            "QPushButton:pressed {"
            "  background: #dceef2;"
            "}"
            "#OcrMiniButton {"
            "  min-width: 26px;"
            "  padding: 4px 8px;"
            "  font-size: 12px;"
            "  border-radius: 7px;"
            "}"
            "#OcrContentSplitter::handle {"
            "  background: #d8e4ec;"
            "}"
            "#OcrContentSplitter::handle:hover {"
            "  background: #8fcbd4;"
            "}"
            "#OcrMiniButton[active=\"true\"] {"
            "  background: #d9eef2;"
            "  border: 1px solid #8fcbd4;"
            "  color: #0d4e5a;"
            "}"
        );
        updateImageModeButtons();
        positionCopyToast();
    }

    void setLoading(const QImage& image)
    {
        m_imageView->setImage(image);
        m_imageView->setBoxes({});
        if (m_imageScrollArea) {
            m_imageView->setViewportSize(m_imageScrollArea->viewport()->size());
        }
        m_textEdit->clear();
        show();
        raise();
        activateWindow();
    }

    void setResult(const QImage& image, const QString& text, const QVector<OcrTextBox>& boxes)
    {
        m_imageView->setImage(image);
        m_imageView->setBoxes(boxes);
        if (m_imageScrollArea) {
            m_imageView->setViewportSize(m_imageScrollArea->viewport()->size());
        }
        m_textEdit->setPlainText(text);
        show();
        raise();
        activateWindow();
    }

    void setError(const QImage& image, const QString& message)
    {
        m_imageView->setImage(image);
        m_imageView->setBoxes({});
        if (m_imageScrollArea) {
            m_imageView->setViewportSize(m_imageScrollArea->viewport()->size());
        }
        m_textEdit->setPlainText(message.trimmed().isEmpty()
            ? QStringLiteral("Image text recognition failed.")
            : message);
        show();
        raise();
        activateWindow();
    }

    void setResultText(const QString& text)
    {
        m_textEdit->setPlainText(text);
        show();
        raise();
        activateWindow();
    }

private:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        positionCopyToast();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (m_imageScrollArea && watched == m_imageScrollArea->viewport() && event && event->type() == QEvent::Resize) {
            if (m_imageView && m_imageView->fitToViewport()) {
                m_imageView->setViewportSize(m_imageScrollArea->viewport()->size());
            }
        }
        if (m_imageScrollArea && (watched == m_imageScrollArea->viewport() || watched == m_imageView)
            && event && event->type() == QEvent::Wheel) {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            if ((wheelEvent->modifiers() & Qt::ControlModifier) && m_imageView) {
                QPoint viewportPos = wheelEvent->pos();
                if (watched == m_imageView) {
                    viewportPos = m_imageView->mapTo(m_imageScrollArea->viewport(), wheelEvent->pos());
                }
                if (wheelEvent->angleDelta().y() > 0) {
                    zoomImage(1.12, viewportPos);
                } else if (wheelEvent->angleDelta().y() < 0) {
                    zoomImage(1.0 / 1.12, viewportPos);
                }
                wheelEvent->accept();
                return true;
            }
        }
        if (m_imageScrollArea && watched == m_imageView && event) {
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                const bool emptyLeftPan = mouseEvent->button() == Qt::LeftButton
                    && m_imageView
                    && !m_imageView->fitToViewport()
                    && !m_imageView->hasBoxAt(mouseEvent->pos());
                if (mouseEvent->button() == Qt::MiddleButton || emptyLeftPan) {
                    m_panning = true;
                    m_panButton = mouseEvent->button();
                    m_panAnchorGlobal = mouseEvent->globalPos();
                    m_panStartH = m_imageScrollArea->horizontalScrollBar()->value();
                    m_panStartV = m_imageScrollArea->verticalScrollBar()->value();
                    m_imageView->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove) {
                if (m_panning) {
                    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                    const QPoint delta = mouseEvent->globalPos() - m_panAnchorGlobal;
                    m_imageScrollArea->horizontalScrollBar()->setValue(m_panStartH - delta.x());
                    m_imageScrollArea->verticalScrollBar()->setValue(m_panStartV - delta.y());
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                if (m_panning && mouseEvent->button() == m_panButton) {
                    m_panning = false;
                    m_panButton = Qt::NoButton;
                    m_imageView->unsetCursor();
                    return true;
                }
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void zoomImage(double factor, const QPoint& viewportPos = QPoint())
    {
        if (!m_imageView || !m_imageScrollArea || qFuzzyIsNull(factor)) {
            return;
        }

        const QSize oldDisplaySize = m_imageView->size();
        const QPoint anchor = viewportPos.isNull()
            ? QPoint(m_imageScrollArea->viewport()->width() / 2, m_imageScrollArea->viewport()->height() / 2)
            : viewportPos;
        const QPoint contentPos = anchor + QPoint(
            m_imageScrollArea->horizontalScrollBar()->value(),
            m_imageScrollArea->verticalScrollBar()->value());

        const double newScale = qBound(0.10, m_imageView->currentScale() * factor, 8.0);
        m_imageView->setManualScale(newScale);
        updateImageModeButtons();

        const QSize newDisplaySize = m_imageView->size();
        if (oldDisplaySize.width() <= 0 || oldDisplaySize.height() <= 0) {
            return;
        }

        const double relX = static_cast<double>(contentPos.x()) / static_cast<double>(oldDisplaySize.width());
        const double relY = static_cast<double>(contentPos.y()) / static_cast<double>(oldDisplaySize.height());
        const int newX = static_cast<int>(relX * newDisplaySize.width()) - anchor.x();
        const int newY = static_cast<int>(relY * newDisplaySize.height()) - anchor.y();
        m_imageScrollArea->horizontalScrollBar()->setValue(newX);
        m_imageScrollArea->verticalScrollBar()->setValue(newY);
    }

    void updateImageModeButtons()
    {
        if (!m_imageView || !m_fitButton || !m_actualButton || !m_zoomInButton || !m_zoomOutButton) {
            return;
        }

        const bool fitMode = m_imageView->fitToViewport();
        m_fitButton->setProperty("active", fitMode);
        m_actualButton->setProperty("active", !fitMode);
        m_fitButton->style()->unpolish(m_fitButton);
        m_fitButton->style()->polish(m_fitButton);
        m_actualButton->style()->unpolish(m_actualButton);
        m_actualButton->style()->polish(m_actualButton);
        m_fitButton->update();
        m_actualButton->update();
        m_zoomInButton->update();
        m_zoomOutButton->update();
    }

    void positionCopyToast()
    {
        if (!m_copyToast) {
            return;
        }
        const int toastWidth = qMax(220, m_copyToast->sizeHint().width() + 28);
        const int toastHeight = qMax(40, m_copyToast->sizeHint().height() + 10);
        const int x = (width() - toastWidth) / 2;
        const int y = 16;
        m_copyToast->setGeometry(x, y, toastWidth, toastHeight);
    }

    void showCopyToast()
    {
        if (!m_copyToast || !m_copyToastTimer) {
            return;
        }
        positionCopyToast();
        m_copyToast->raise();
        m_copyToast->show();
        m_copyToastTimer->start(1300);
    }

    OcrResultImageView* m_imageView = nullptr;
    QScrollArea* m_imageScrollArea = nullptr;
    QPushButton* m_fitButton = nullptr;
    QPushButton* m_zoomOutButton = nullptr;
    QPushButton* m_zoomInButton = nullptr;
    QPushButton* m_actualButton = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QLabel* m_copyToast = nullptr;
    QTimer* m_copyToastTimer = nullptr;
    bool m_panning = false;
    Qt::MouseButton m_panButton = Qt::NoButton;
    QPoint m_panAnchorGlobal;
    int m_panStartH = 0;
    int m_panStartV = 0;
};
