#pragma once

#include "ui/Fluent/FluentBaseWidget.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

struct OcrTextBox {
    QString text;
    QRectF imageRect;
};

// =========================================================================
// OcrEditDialog: Full text editor with auto-scroll & cursor positioning
// to the double-clicked item.
// =========================================================================
class OcrEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit OcrEditDialog(const QStringList& allRecords, int targetIndex, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        resize(640, 480);

        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(12, 12, 12, 12);

        auto* container = new QWidget(this);
        container->setObjectName(QStringLiteral("ocrEditContainer"));
        container->setStyleSheet(QStringLiteral(
            "#ocrEditContainer {"
            "  background-color: #ffffff;"
            "  border: 1px solid #d4d4d8;"
            "  border-radius: 8px;"
            "}"
            "#ocrEditTitle {"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 15px;"
            "  font-weight: 600;"
            "  color: #18181b;"
            "}"
            "#ocrEditHint {"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 11px;"
            "  color: #71717a;"
            "}"
            "#ocrEditTextEdit {"
            "  background-color: #f9f9f9;"
            "  border: 1px solid #e4e4e7;"
            "  border-radius: 6px;"
            "  color: #18181b;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 13px;"
            "  line-height: 1.6;"
            "  padding: 10px;"
            "}"
            "#ocrEditTextEdit:focus {"
            "  background-color: #ffffff;"
            "  border: 1px solid #0067c0;"
            "}"
            "#ocrCancelBtn {"
            "  background-color: #ffffff;"
            "  color: #18181b;"
            "  border: 1px solid #d4d4d8;"
            "  border-radius: 6px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 12px;"
            "  padding: 6px 18px;"
            "}"
            "#ocrCancelBtn:hover {"
            "  background-color: #f4f4f5;"
            "}"
            "#ocrSaveBtn {"
            "  background-color: #0067c0;"
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 6px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 12px;"
            "  font-weight: 500;"
            "  padding: 6px 20px;"
            "}"
            "#ocrSaveBtn:hover {"
            "  background-color: #1975c5;"
            "}"
        ));
        rootLayout->addWidget(container);

        auto* containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(20, 16, 20, 18);
        containerLayout->setSpacing(12);

        auto* headerLayout = new QHBoxLayout();
        auto* titleLabel = new QLabel(tr("编辑全部识别文本"), container);
        titleLabel->setObjectName(QStringLiteral("ocrEditTitle"));
        headerLayout->addWidget(titleLabel);
        headerLayout->addStretch();
        auto* hintLabel = new QLabel(tr("已定位至选中行 · Ctrl+Enter 保存 · Esc 取消"), container);
        hintLabel->setObjectName(QStringLiteral("ocrEditHint"));
        headerLayout->addWidget(hintLabel);
        containerLayout->addLayout(headerLayout);

        m_textEdit = new QPlainTextEdit(container);
        m_textEdit->setObjectName(QStringLiteral("ocrEditTextEdit"));
        const QString fullText = allRecords.join(QStringLiteral("\n"));
        m_textEdit->setPlainText(fullText);
        containerLayout->addWidget(m_textEdit, 1);

        // Calculate exact target line block index
        if (targetIndex >= 0 && targetIndex < allRecords.size()) {
            int targetStartBlock = 0;
            for (int i = 0; i < targetIndex && i < allRecords.size(); ++i) {
                targetStartBlock += allRecords.at(i).split(QLatin1Char('\n')).size();
            }

            QTextBlock startBlock = m_textEdit->document()->findBlockByNumber(targetStartBlock);
            if (startBlock.isValid()) {
                QTextCursor cursor(startBlock);
                const int numLinesInCard = allRecords.at(targetIndex).split(QLatin1Char('\n')).size();
                for (int l = 1; l < numLinesInCard; ++l) {
                    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
                }
                cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                m_textEdit->setTextCursor(cursor);
                m_textEdit->centerCursor();
            }
        }

        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        auto* cancelBtn = new QPushButton(tr("取消"), container);
        cancelBtn->setObjectName(QStringLiteral("ocrCancelBtn"));
        cancelBtn->setCursor(Qt::PointingHandCursor);
        auto* saveBtn = new QPushButton(tr("保存全部修改"), container);
        saveBtn->setObjectName(QStringLiteral("ocrSaveBtn"));
        saveBtn->setCursor(Qt::PointingHandCursor);
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);
        containerLayout->addLayout(btnLayout);

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
    }

    QString text() const {
        return m_textEdit ? m_textEdit->toPlainText() : QString();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return && (event->modifiers() & Qt::ControlModifier)) {
            accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }

private:
    QPlainTextEdit* m_textEdit = nullptr;
};

// =========================================================================
// OcrTextCard: High-performance record card with QPainter hardware-rendered
// silky smooth drag-and-drop merge, single-click copy, and double-click edit.
// =========================================================================
class OcrTextCard : public QWidget {
    Q_OBJECT
public:
    enum DragHighlight { None, Before, After };

    explicit OcrTextCard(int index, const QString& text, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_index(index)
        , m_text(text)
    {
        setObjectName(QStringLiteral("ocrTextCard"));
        setAcceptDrops(true);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        auto* cardLayout = new QHBoxLayout(this);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(10);

        m_badgeLabel = new QLabel(QString::number(m_index + 1), this);
        m_badgeLabel->setObjectName(QStringLiteral("ocrCardBadge"));
        m_badgeLabel->setFixedWidth(24);
        m_badgeLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_badgeLabel->setStyleSheet(QStringLiteral("color: #9ca3af; font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; font-size: 11px; font-weight: 500; margin-top: 2px;"));

        m_textLabel = new QLabel(m_text, this);
        m_textLabel->setObjectName(QStringLiteral("ocrCardText"));
        m_textLabel->setWordWrap(true);
        m_textLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        m_textLabel->setStyleSheet(QStringLiteral("color: #18181b; font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; font-size: 13px; line-height: 1.45;"));

        m_copyHint = new QLabel(QString::fromUtf8("\xEE\x8C\x5F"), this); // Copy icon
        m_copyHint->setObjectName(QStringLiteral("ocrCardCopyIcon"));
        m_copyHint->setFixedWidth(16);
        m_copyHint->setAlignment(Qt::AlignRight | Qt::AlignTop);
        m_copyHint->setStyleSheet(QStringLiteral("color: #cbd5e1; font-family: 'Segoe MDL2 Assets'; font-size: 12px; margin-top: 3px;"));

        cardLayout->addWidget(m_badgeLabel);
        cardLayout->addWidget(m_textLabel, 1);
        cardLayout->addWidget(m_copyHint);
    }

    int index() const { return m_index; }
    void setIndex(int index) {
        m_index = index;
        if (m_badgeLabel) {
            m_badgeLabel->setText(QString::number(m_index + 1));
        }
    }

    QString text() const { return m_text; }
    void setText(const QString& text) {
        m_text = text;
        if (m_textLabel) {
            m_textLabel->setText(m_text);
        }
        adjustSize();
    }

signals:
    void cardClicked(const QString& text);
    void cardDoubleClicked(OcrTextCard* card);
    void deleteRequested(int index);
    void mergeRequested(int sourceIndex, int targetIndex);
    void copyAllRequested();
    void clearAllRequested();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        QColor bgColor(255, 255, 255);
        QColor borderColor(229, 229, 229);

        if (m_isCopiedFlash) {
            bgColor = QColor(239, 246, 252);
            borderColor = QColor(0, 103, 192);
        } else if (m_dragHighlight != None) {
            bgColor = QColor(239, 246, 252);
            borderColor = QColor(0, 103, 192);
        } else if (m_isHovered) {
            bgColor = QColor(251, 252, 253);
            borderColor = QColor(199, 210, 254);
        }

        painter.setBrush(bgColor);
        painter.setPen(QPen(borderColor, 1.0));
        painter.drawRoundedRect(r, 6.0, 6.0);

        // WinUI 3 accent indicator line for drag merging
        if (m_dragHighlight == Before) {
            painter.setBrush(QColor(0, 103, 192));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(QRectF(6, 1.5, width() - 12, 3.0), 1.5, 1.5);
        } else if (m_dragHighlight == After) {
            painter.setBrush(QColor(0, 103, 192));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(QRectF(6, height() - 4.5, width() - 12, 3.0), 1.5, 1.5);
        }
    }

    void enterEvent(QEvent* event) override {
        QWidget::enterEvent(event);
        if (!m_isDragged) {
            m_isHovered = true;
            if (m_copyHint) m_copyHint->setStyleSheet(QStringLiteral("color: #0067c0; font-family: 'Segoe MDL2 Assets'; font-size: 12px; margin-top: 3px;"));
            update();
        }
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        if (!m_isDragged) {
            m_isHovered = false;
            if (m_copyHint) m_copyHint->setStyleSheet(QStringLiteral("color: #cbd5e1; font-family: 'Segoe MDL2 Assets'; font-size: 12px; margin-top: 3px;"));
            update();
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragStartPos = event->pos();
            m_hasDraggedOut = false;
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) {
            if (!m_hasDraggedOut && (event->pos() - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_hasDraggedOut = true;
                startCardDrag();
                event->accept();
                return;
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (!m_hasDraggedOut) {
                // Single Click: Copy text to clipboard
                QApplication::clipboard()->setText(m_text);
                m_isCopiedFlash = true;
                update();
                emit cardClicked(m_text);
                QTimer::singleShot(220, this, [this]() {
                    m_isCopiedFlash = false;
                    update();
                });
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit cardDoubleClicked(this);
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e5e5;"
            "  border-radius: 8px;"
            "  padding: 4px;"
            "}"
            "QMenu::item {"
            "  padding: 6px 20px 6px 12px;"
            "  border-radius: 4px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 13px;"
            "  color: #1f2937;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #f3f4f6;"
            "  color: #0067c0;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background: #e5e5e5;"
            "  margin: 4px 6px;"
            "}"
        ));

        QAction* actCopy = menu.addAction(tr("复制此条"));
        QAction* actEdit = menu.addAction(tr("编辑全部 (双击)"));
        QAction* actDelete = menu.addAction(tr("删除此条"));
        menu.addSeparator();
        QAction* actCopyAll = menu.addAction(tr("复制全部文本"));
        QAction* actClear = menu.addAction(tr("清空所有记录"));

        QAction* selected = menu.exec(event->globalPos());
        if (selected == actCopy) {
            QApplication::clipboard()->setText(m_text);
            emit cardClicked(m_text);
        } else if (selected == actEdit) {
            emit cardDoubleClicked(this);
        } else if (selected == actDelete) {
            emit deleteRequested(m_index);
        } else if (selected == actCopyAll) {
            emit copyAllRequested();
        } else if (selected == actClear) {
            emit clearAllRequested();
        }
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(QStringLiteral("application/x-ocr-card-index"))) {
            const int srcIdx = event->mimeData()->data(QStringLiteral("application/x-ocr-card-index")).toInt();
            if (srcIdx != m_index) {
                event->acceptProposedAction();
                m_dragHighlight = (srcIdx < m_index) ? Before : After;
                update();
                return;
            }
        }
        event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->mimeData()->hasFormat(QStringLiteral("application/x-ocr-card-index"))) {
            const int srcIdx = event->mimeData()->data(QStringLiteral("application/x-ocr-card-index")).toInt();
            if (srcIdx != m_index) {
                event->acceptProposedAction();
                DragHighlight targetHighlight = (srcIdx < m_index) ? Before : After;
                if (m_dragHighlight != targetHighlight) {
                    m_dragHighlight = targetHighlight;
                    update();
                }
                return;
            }
        }
        event->ignore();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        if (m_dragHighlight != None) {
            m_dragHighlight = None;
            update();
        }
        event->accept();
    }

    void dropEvent(QDropEvent* event) override {
        if (event->mimeData()->hasFormat(QStringLiteral("application/x-ocr-card-index"))) {
            const int srcIdx = event->mimeData()->data(QStringLiteral("application/x-ocr-card-index")).toInt();
            if (srcIdx != m_index) {
                m_dragHighlight = None;
                update();
                event->acceptProposedAction();
                emit mergeRequested(srcIdx, m_index);
                return;
            }
        }
        m_dragHighlight = None;
        update();
        event->ignore();
    }

private:
    void startCardDrag() {
        m_isDragged = true;
        auto* drag = new QDrag(this);
        auto* mimeData = new QMimeData();
        mimeData->setData(QStringLiteral("application/x-ocr-card-index"), QByteArray::number(m_index));
        mimeData->setText(m_text);
        drag->setMimeData(mimeData);

        QPixmap pixmap(size());
        pixmap.fill(Qt::transparent);
        render(&pixmap);
        drag->setPixmap(pixmap);
        drag->setHotSpot(m_dragStartPos);
        drag->exec(Qt::MoveAction);

        m_isDragged = false;
        m_hasDraggedOut = false;
        m_dragHighlight = None;
        m_isHovered = underMouse();
        update();
    }

    int m_index = 0;
    QString m_text;
    QLabel* m_badgeLabel = nullptr;
    QLabel* m_textLabel = nullptr;
    QLabel* m_copyHint = nullptr;
    QPoint m_dragStartPos;
    bool m_hasDraggedOut = false;
    bool m_isDragged = false;
    bool m_isHovered = false;
    bool m_isCopiedFlash = false;
    DragHighlight m_dragHighlight = None;
};

// =========================================================================
// OcrRecordListWidget: Container holding the scrollable stream of record cards
// =========================================================================
class OcrRecordListWidget : public QWidget {
    Q_OBJECT
public:
    explicit OcrRecordListWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("ocrRecordContainer"));
        setStyleSheet(QStringLiteral(
            "#ocrRecordContainer {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e5e5;"
            "  border-radius: 8px;"
            "}"
            "#ocrRecordScroll {"
            "  background: transparent;"
            "  border: none;"
            "}"
            "#ocrRecordScrollContent {"
            "  background: transparent;"
            "}"
            "#ocrListHeaderTitle {"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "  color: #18181b;"
            "}"
            "#ocrListHeaderBadge {"
            "  background-color: #f4f4f5;"
            "  color: #52525b;"
            "  border: 1px solid #e4e4e7;"
            "  border-radius: 9px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 11px;"
            "  padding: 1px 7px;"
            "}"
            "#ocrHeaderMiniBtn {"
            "  background: transparent;"
            "  color: #71717a;"
            "  border: none;"
            "  border-radius: 4px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 12px;"
            "  padding: 3px 8px;"
            "}"
            "#ocrHeaderMiniBtn:hover {"
            "  background-color: #f4f4f5;"
            "  color: #18181b;"
            "}"
            "#ocrFooterStats {"
            "  color: #71717a;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 11px;"
            "}"
        ));

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(14, 12, 14, 12);
        mainLayout->setSpacing(8);

        // Header
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(2, 0, 2, 0);
        headerLayout->setSpacing(8);

        auto* titleLabel = new QLabel(tr("识别记录"), this);
        titleLabel->setObjectName(QStringLiteral("ocrListHeaderTitle"));
        headerLayout->addWidget(titleLabel);

        m_countBadge = new QLabel(tr("0 条"), this);
        m_countBadge->setObjectName(QStringLiteral("ocrListHeaderBadge"));
        headerLayout->addWidget(m_countBadge);

        headerLayout->addStretch(1);

        auto* editAllBtn = new QPushButton(tr("编辑全部"), this);
        editAllBtn->setObjectName(QStringLiteral("ocrHeaderMiniBtn"));
        editAllBtn->setCursor(Qt::PointingHandCursor);
        editAllBtn->setToolTip(tr("打开多行编辑窗口，修改全部识别内容"));
        headerLayout->addWidget(editAllBtn);

        auto* copyAllBtn = new QPushButton(tr("复制全部"), this);
        copyAllBtn->setObjectName(QStringLiteral("ocrHeaderMiniBtn"));
        copyAllBtn->setCursor(Qt::PointingHandCursor);
        copyAllBtn->setToolTip(tr("复制全部记录内容到剪贴板"));
        headerLayout->addWidget(copyAllBtn);

        auto* clearBtn = new QPushButton(tr("清空"), this);
        clearBtn->setObjectName(QStringLiteral("ocrHeaderMiniBtn"));
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setToolTip(tr("清空识别记录列表"));
        headerLayout->addWidget(clearBtn);

        mainLayout->addLayout(headerLayout);

        // Scroll Area holding cards
        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setObjectName(QStringLiteral("ocrRecordScroll"));
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setFrameShape(QFrame::NoFrame);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        m_scrollContent = new QWidget(m_scrollArea);
        m_scrollContent->setObjectName(QStringLiteral("ocrRecordScrollContent"));
        m_cardsLayout = new QVBoxLayout(m_scrollContent);
        m_cardsLayout->setContentsMargins(2, 4, 4, 4);
        m_cardsLayout->setSpacing(6);
        m_cardsLayout->addStretch(1);

        m_scrollArea->setWidget(m_scrollContent);
        mainLayout->addWidget(m_scrollArea, 1);

        // Footer Stats
        auto* footerLayout = new QHBoxLayout();
        footerLayout->setContentsMargins(2, 2, 2, 0);
        m_footerStats = new QLabel(this);
        m_footerStats->setObjectName(QStringLiteral("ocrFooterStats"));
        footerLayout->addWidget(m_footerStats);
        footerLayout->addStretch(1);

        auto* hintLabel = new QLabel(tr("单击复制 · 双击编辑全部 · 拖拽合并"), this);
        hintLabel->setObjectName(QStringLiteral("ocrFooterStats"));
        footerLayout->addWidget(hintLabel);

        mainLayout->addLayout(footerLayout);

        connect(editAllBtn, &QPushButton::clicked, this, [this]() {
            openFullEditor(0);
        });
        connect(copyAllBtn, &QPushButton::clicked, this, &OcrRecordListWidget::copyAll);
        connect(clearBtn, &QPushButton::clicked, this, &OcrRecordListWidget::clearAll);
    }

    void setRecords(const QStringList& records) {
        clearCards();
        for (const QString& line : records) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                createCard(trimmed);
            }
        }
        updateStats();
    }

    QStringList allTexts() const {
        QStringList texts;
        for (const auto* card : m_cards) {
            if (card) {
                texts.append(card->text());
            }
        }
        return texts;
    }

    QString fullText() const {
        return allTexts().join(QStringLiteral("\n"));
    }

    int recordCount() const {
        return m_cards.size();
    }

signals:
    void toastRequested(const QString& message);
    void recordsChanged();

public slots:
    void openFullEditor(int targetIndex) {
        QStringList records = allTexts();
        if (records.isEmpty()) {
            emit toastRequested(tr("暂无记录可编辑"));
            return;
        }

        OcrEditDialog dialog(records, targetIndex, this);
        if (dialog.exec() == QDialog::Accepted) {
            const QString updated = dialog.text();
            QStringList newLines = updated.split(QStringLiteral("\n"), QString::SkipEmptyParts);
            setRecords(newLines);
            emit recordsChanged();
            emit toastRequested(tr("全部记录已更新"));
        }
    }

    void copyAll() {
        const QString text = fullText();
        if (!text.isEmpty()) {
            QApplication::clipboard()->setText(text);
            emit toastRequested(tr("已复制全部记录到剪贴板"));
        }
    }

    void clearAll() {
        clearCards();
        updateStats();
        emit recordsChanged();
        emit toastRequested(tr("已清空识别记录"));
    }

protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e5e5;"
            "  border-radius: 8px;"
            "  padding: 4px;"
            "}"
            "QMenu::item {"
            "  padding: 6px 20px 6px 12px;"
            "  border-radius: 4px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 13px;"
            "  color: #1f2937;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #f3f4f6;"
            "  color: #0067c0;"
            "}"
        ));
        QAction* actEditAll = menu.addAction(tr("编辑全部文本"));
        QAction* actCopyAll = menu.addAction(tr("复制全部文本"));
        QAction* actClear = menu.addAction(tr("清空所有记录"));

        QAction* selected = menu.exec(event->globalPos());
        if (selected == actEditAll) {
            openFullEditor(0);
        } else if (selected == actCopyAll) {
            copyAll();
        } else if (selected == actClear) {
            clearAll();
        }
    }

private:
    void createCard(const QString& text) {
        const int index = m_cards.size();
        auto* card = new OcrTextCard(index, text, m_scrollContent);
        m_cards.append(card);

        // Insert before bottom stretch
        m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);

        connect(card, &OcrTextCard::cardClicked, this, [this](const QString&) {
            emit toastRequested(tr("已复制此条文本"));
        });

        connect(card, &OcrTextCard::cardDoubleClicked, this, [this, card]() {
            const int targetIdx = m_cards.indexOf(card);
            openFullEditor(targetIdx);
        });

        connect(card, &OcrTextCard::deleteRequested, this, [this](int idx) {
            deleteCard(idx);
        });

        connect(card, &OcrTextCard::mergeRequested, this, [this](int srcIdx, int tgtIdx) {
            handleMerge(srcIdx, tgtIdx);
        });

        connect(card, &OcrTextCard::copyAllRequested, this, &OcrRecordListWidget::copyAll);
        connect(card, &OcrTextCard::clearAllRequested, this, &OcrRecordListWidget::clearAll);
    }

    void deleteCard(int idx) {
        if (idx < 0 || idx >= m_cards.size()) return;
        auto* card = m_cards.takeAt(idx);
        if (card) {
            m_cardsLayout->removeWidget(card);
            card->deleteLater();
        }
        reindexCards();
        updateStats();
        emit recordsChanged();
        emit toastRequested(tr("已删除此条记录"));
    }

    void handleMerge(int srcIdx, int tgtIdx) {
        if (srcIdx < 0 || srcIdx >= m_cards.size() || tgtIdx < 0 || tgtIdx >= m_cards.size()) return;
        if (srcIdx == tgtIdx) return;

        OcrTextCard* src = m_cards.at(srcIdx);
        OcrTextCard* tgt = m_cards.at(tgtIdx);
        if (!src || !tgt) return;

        QString combined;
        // User aligned rule:
        // Source < Target (dragged from above): insert source in FRONT of target (src + \n + tgt)
        // Source > Target (dragged from below): append source to TAIL of target (tgt + \n + src)
        if (srcIdx < tgtIdx) {
            combined = src->text() + QStringLiteral("\n") + tgt->text();
        } else {
            combined = tgt->text() + QStringLiteral("\n") + src->text();
        }

        tgt->setText(combined);

        // Remove source card
        m_cards.removeAt(srcIdx);
        m_cardsLayout->removeWidget(src);
        src->deleteLater();

        reindexCards();
        updateStats();
        emit recordsChanged();
        emit toastRequested(tr("已合并记录"));
    }

    void reindexCards() {
        for (int i = 0; i < m_cards.size(); ++i) {
            if (m_cards.at(i)) {
                m_cards.at(i)->setIndex(i);
            }
        }
    }

    void clearCards() {
        for (auto* card : m_cards) {
            if (card) {
                m_cardsLayout->removeWidget(card);
                card->deleteLater();
            }
        }
        m_cards.clear();
    }

    void updateStats() {
        const int count = m_cards.size();
        if (m_countBadge) {
            m_countBadge->setText(tr("%1 条").arg(count));
        }
        if (m_footerStats) {
            const int totalChars = fullText().length();
            m_footerStats->setText(tr("共 %1 条记录 · %2 字符").arg(count).arg(totalChars));
        }
    }

    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_cardsLayout = nullptr;
    QLabel* m_countBadge = nullptr;
    QLabel* m_footerStats = nullptr;
    QVector<OcrTextCard*> m_cards;
};

// =========================================================================
// OcrResultImageView: Interactive image canvas with Drag-to-Desktop,
// right-click copy/save, and Wheel Zoom.
// =========================================================================
class OcrResultImageView : public QLabel {
    Q_OBJECT
public:
    explicit OcrResultImageView(QWidget* parent = nullptr) : QLabel(parent) {
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setScaledContents(false);
        setMouseTracking(true);
        setCursor(Qt::OpenHandCursor);
    }

    void setScrollArea(QScrollArea* scrollArea) {
        m_scrollArea = scrollArea;
        if (m_scrollArea && m_scrollArea->viewport()) {
            m_scrollArea->viewport()->installEventFilter(this);
            m_scrollArea->viewport()->setMouseTracking(true);
            m_scrollArea->viewport()->setCursor(Qt::OpenHandCursor);
        }
    }

    void setImage(const QImage& image) {
        m_image = image;
        m_fitToViewport = true;
        refreshDisplayPixmap();
        update();
    }

    const QImage& image() const {
        return m_image;
    }

    void setBoxes(const QVector<OcrTextBox>& boxes) {
        m_boxes = boxes;
        m_hoveredBoxIndex = -1;
        update();
    }

    void setViewportSize(const QSize& viewportSize) {
        if (viewportSize.width() > 50 && viewportSize.height() > 50) {
            m_viewportSize = viewportSize;
            if (m_fitToViewport) {
                refreshDisplayPixmap();
                emit scaleChanged(currentScale());
            }
        }
    }

    void setFitToViewport(bool fit) {
        if (m_fitToViewport == fit && fit) {
            refreshDisplayPixmap();
            emit scaleChanged(currentScale());
            return;
        }
        if (!fit) {
            m_manualScale = currentScale();
        }
        m_fitToViewport = fit;
        refreshDisplayPixmap();
        emit scaleChanged(currentScale());
    }

    bool fitToViewport() const {
        return m_fitToViewport;
    }

    void setManualScale(double scale) {
        m_fitToViewport = false;
        m_manualScale = qBound(0.12, scale, 8.0);
        refreshDisplayPixmap();
        emit scaleChanged(m_manualScale);
    }

    double manualScale() const {
        return m_manualScale;
    }

    double currentScale() const {
        if (m_image.isNull() || m_image.width() <= 0 || m_displaySize.width() <= 0) {
            return 1.0;
        }
        return static_cast<double>(m_displaySize.width()) / static_cast<double>(m_image.width());
    }

signals:
    void textCopied(const QString& text);
    void toastRequested(const QString& message);
    void scaleChanged(double newScale);

public slots:
    void zoomIn() {
        zoomWithAnchor(1.25, QPoint(width() / 2, height() / 2), false);
    }

    void zoomOut() {
        zoomWithAnchor(1.0 / 1.25, QPoint(width() / 2, height() / 2), false);
    }

    void resetToActualSize() {
        setManualScale(1.0);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (m_scrollArea && watched == m_scrollArea->viewport()) {
            switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::MiddleButton) {
                    startPan(me->globalPos());
                    m_scrollArea->viewport()->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
                break;
            }
            case QEvent::MouseMove: {
                auto* me = static_cast<QMouseEvent*>(event);
                if (m_isPanning) {
                    processPan(me->globalPos());
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonRelease: {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::MiddleButton) {
                    stopPan();
                    m_scrollArea->viewport()->setCursor(Qt::OpenHandCursor);
                    return true;
                }
                break;
            }
            case QEvent::Wheel: {
                auto* we = static_cast<QWheelEvent*>(event);
                handleWheel(we);
                return true;
            }
            case QEvent::Resize: {
                auto* re = static_cast<QResizeEvent*>(event);
                if (re->size().width() > 50 && re->size().height() > 50) {
                    m_viewportSize = re->size();
                    if (m_fitToViewport) {
                        refreshDisplayPixmap();
                        emit scaleChanged(currentScale());
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        return QLabel::eventFilter(watched, event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event && event->button() == Qt::LeftButton) {
            m_pressPos = event->pos();
            m_globalPressPos = event->globalPos();
            m_pressedBoxIndex = findBoxAt(m_pressPos);
            m_dragThresholdPassed = false;
            event->accept();
            return;
        } else if (event && event->button() == Qt::MiddleButton) {
            startPan(event->globalPos());
            event->accept();
            return;
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!event) return;

        if (event->buttons() & Qt::LeftButton) {
            const QPoint delta = event->globalPos() - m_globalPressPos;
            if (!m_dragThresholdPassed && delta.manhattanLength() >= QApplication::startDragDistance()) {
                m_dragThresholdPassed = true;
                startImageDragOut();
                event->accept();
                return;
            }
        } else if (event->buttons() & Qt::MiddleButton) {
            if (m_isPanning) {
                processPan(event->globalPos());
                event->accept();
                return;
            }
        }

        // Hover handling
        const QPoint pos = event->pos();
        const int newHover = findBoxAt(pos);
        if (newHover != m_hoveredBoxIndex) {
            m_hoveredBoxIndex = newHover;
            update();
        }

        if (m_hoveredBoxIndex >= 0) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::OpenHandCursor);
        }

        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event && event->button() == Qt::LeftButton) {
            if (!m_dragThresholdPassed && m_pressedBoxIndex >= 0) {
                if (mappedRect(m_boxes.at(m_pressedBoxIndex).imageRect).contains(event->pos())) {
                    const QString clickedText = m_boxes.at(m_pressedBoxIndex).text.trimmed();
                    if (!clickedText.isEmpty()) {
                        QApplication::clipboard()->setText(clickedText);
                        emit textCopied(clickedText);
                    }
                }
            }
            m_pressedBoxIndex = -1;
            m_dragThresholdPassed = false;
            setCursor(m_hoveredBoxIndex >= 0 ? Qt::PointingHandCursor : Qt::OpenHandCursor);
            event->accept();
            return;
        } else if (event && event->button() == Qt::MiddleButton) {
            stopPan();
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        if (m_image.isNull()) return;

        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e5e5;"
            "  border-radius: 8px;"
            "  padding: 4px;"
            "}"
            "QMenu::item {"
            "  padding: 6px 20px 6px 12px;"
            "  border-radius: 4px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 13px;"
            "  color: #1f2937;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #f3f4f6;"
            "  color: #0067c0;"
            "}"
        ));

        QAction* actCopy = menu.addAction(tr("复制图片"));
        QAction* actSave = menu.addAction(tr("图片另存为..."));

        QAction* selected = menu.exec(event->globalPos());
        if (selected == actCopy) {
            QApplication::clipboard()->setImage(m_image);
            emit toastRequested(tr("已复制截图原画到剪贴板"));
        } else if (selected == actSave) {
            const QString defaultName = QStringLiteral("LinDesk_OCR_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
            const QString savePath = QFileDialog::getSaveFileName(
                this, tr("保存图片"), defaultName, tr("PNG 图像 (*.png);;JPEG 图像 (*.jpg);;所有文件 (*.*)")
            );
            if (!savePath.isEmpty()) {
                m_image.save(savePath);
                emit toastRequested(tr("图片已成功保存"));
            }
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        if (event) {
            handleWheel(event);
        } else {
            QLabel::wheelEvent(event);
        }
    }

    void paintEvent(QPaintEvent* event) override {
        QLabel::paintEvent(event);
        if (m_image.isNull() || m_displaySize.width() <= 0) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Frame
        painter.setPen(QPen(QColor(0, 0, 0, 30), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        if (m_boxes.isEmpty()) return;

        // OCR detected boxes
        for (int i = 0; i < m_boxes.size(); ++i) {
            const QRectF r = mappedRect(m_boxes.at(i).imageRect);
            if (r.isEmpty()) continue;

            const bool isHovered = (i == m_hoveredBoxIndex);
            if (isHovered) {
                painter.setBrush(QColor(0, 103, 192, 45));
                painter.setPen(QPen(QColor(0, 103, 192, 220), 1.6));
            } else {
                painter.setBrush(QColor(0, 103, 192, 18));
                painter.setPen(QPen(QColor(0, 103, 192, 140), 1.0));
            }
            painter.drawRect(r);

            if (isHovered) {
                painter.setBrush(QColor(0, 103, 192));
                painter.setPen(Qt::NoPen);
                painter.drawRect(QRectF(r.left(), r.top(), 3, 3));
                painter.drawRect(QRectF(r.right() - 2, r.bottom() - 2, 3, 3));
            }
        }
    }

private:
    void startImageDragOut() {
        if (m_image.isNull()) return;

        // Prepare local temp file so Windows Explorer / Desktop can receive file drop directly
        const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/LinDesk/OCR");
        QDir().mkpath(tempDir);
        const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
        const QString tempFilePath = tempDir + QStringLiteral("/LinDesk_OCR_") + timestamp + QStringLiteral(".png");
        m_image.save(tempFilePath, "PNG");

        auto* mimeData = new QMimeData();
        mimeData->setImageData(m_image);
        mimeData->setUrls({ QUrl::fromLocalFile(tempFilePath) });

        auto* drag = new QDrag(this);
        drag->setMimeData(mimeData);

        // Preview thumbnail
        const QImage thumb = m_image.scaled(180, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        drag->setPixmap(QPixmap::fromImage(thumb));
        drag->setHotSpot(QPoint(thumb.width() / 2, thumb.height() / 2));
        drag->exec(Qt::CopyAction);
    }

    int findBoxAt(const QPoint& pos) const {
        for (int i = m_boxes.size() - 1; i >= 0; --i) {
            if (mappedRect(m_boxes.at(i).imageRect).contains(pos)) {
                return i;
            }
        }
        return -1;
    }

    void startPan(const QPoint& globalPos) {
        m_panGlobalPos = globalPos;
        m_isPanning = true;
        if (m_scrollArea) {
            m_startScrollH = m_scrollArea->horizontalScrollBar()->value();
            m_startScrollV = m_scrollArea->verticalScrollBar()->value();
        }
    }

    void processPan(const QPoint& globalPos) {
        if (!m_scrollArea) return;
        const QPoint delta = globalPos - m_panGlobalPos;
        m_scrollArea->horizontalScrollBar()->setValue(m_startScrollH - delta.x());
        m_scrollArea->verticalScrollBar()->setValue(m_startScrollV - delta.y());
    }

    void stopPan() {
        m_isPanning = false;
    }

    void handleWheel(QWheelEvent* event) {
        const int degrees = event->angleDelta().y();
        if (degrees == 0) return;

        const double factor = (degrees > 0) ? 1.15 : (1.0 / 1.15);
        zoomWithAnchor(factor, event->globalPos(), true);
        event->accept();
    }

    void zoomWithAnchor(double factor, const QPoint& anchorPos, bool isGlobal) {
        if (m_image.isNull() || !m_scrollArea) return;

        const double oldScale = currentScale();
        const double targetScale = qBound(0.12, oldScale * factor, 8.0);
        if (qAbs(targetScale - oldScale) < 0.002) return;

        const QPoint globalAnchor = isGlobal ? anchorPos : mapToGlobal(anchorPos);
        const QPoint imgPos = mapFromGlobal(globalAnchor);
        const double normX = (m_displaySize.width() > 0)
            ? (static_cast<double>(imgPos.x()) / m_displaySize.width())
            : 0.5;
        const double normY = (m_displaySize.height() > 0)
            ? (static_cast<double>(imgPos.y()) / m_displaySize.height())
            : 0.5;

        const QPoint vpPos = m_scrollArea->viewport()->mapFromGlobal(globalAnchor);

        setManualScale(targetScale);

        if (m_displaySize.width() > m_scrollArea->viewport()->width() ||
            m_displaySize.height() > m_scrollArea->viewport()->height()) {
            const int targetX = qRound(normX * m_displaySize.width()) - vpPos.x();
            const int targetY = qRound(normY * m_displaySize.height()) - vpPos.y();
            m_scrollArea->horizontalScrollBar()->setValue(targetX);
            m_scrollArea->verticalScrollBar()->setValue(targetY);
        }
    }

    void refreshDisplayPixmap() {
        if (m_image.isNull()) {
            setPixmap(QPixmap());
            m_displaySize = QSize(0, 0);
            return;
        }

        QSize effectiveVp = m_viewportSize;
        if (!effectiveVp.isValid() || effectiveVp.width() <= 50 || effectiveVp.height() <= 50) {
            if (m_scrollArea && m_scrollArea->viewport() &&
                m_scrollArea->viewport()->width() > 50 && m_scrollArea->viewport()->height() > 50) {
                effectiveVp = m_scrollArea->viewport()->size();
            } else {
                effectiveVp = QSize(620, 560);
            }
            m_viewportSize = effectiveVp;
        }

        QSize targetSize = m_image.size();
        if (m_fitToViewport && effectiveVp.width() > 50 && effectiveVp.height() > 50) {
            const int maxW = qMax(1, effectiveVp.width() - 24);
            const int maxH = qMax(1, effectiveVp.height() - 24);
            const double sx = static_cast<double>(maxW) / static_cast<double>(m_image.width());
            const double sy = static_cast<double>(maxH) / static_cast<double>(m_image.height());
            double scale = qMin(sx, sy);
            if (scale > 2.0) {
                scale = 2.0;
            }
            m_manualScale = scale;

            targetSize = QSize(
                qMax(1, static_cast<int>(m_image.width() * scale)),
                qMax(1, static_cast<int>(m_image.height() * scale))
            );
        } else {
            targetSize = QSize(
                qMax(1, static_cast<int>(m_image.width() * m_manualScale)),
                qMax(1, static_cast<int>(m_image.height() * m_manualScale))
            );
        }

        const QImage scaled = m_image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_displaySize = scaled.size();
        setPixmap(QPixmap::fromImage(scaled));
        resize(m_displaySize);
        setMinimumSize(m_displaySize);
        setMaximumSize(m_displaySize);
    }

    QRectF mappedRect(const QRectF& imageRect) const {
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
    QScrollArea* m_scrollArea = nullptr;
    bool m_fitToViewport = true;
    double m_manualScale = 1.0;

    int m_hoveredBoxIndex = -1;
    int m_pressedBoxIndex = -1;
    QPoint m_pressPos;
    QPoint m_globalPressPos;
    bool m_dragThresholdPassed = false;

    QPoint m_panGlobalPos;
    bool m_isPanning = false;
    int m_startScrollH = 0;
    int m_startScrollV = 0;
};

// =========================================================================
// OcrImageContainer: Left Card holding ScrollArea + Floating Zoom Bar
// =========================================================================
class OcrImageContainer : public QWidget {
    Q_OBJECT
public:
    explicit OcrImageContainer(QWidget* parent = nullptr) : QWidget(parent) {}

    void setFloatingBar(QWidget* bar) {
        m_floatingBar = bar;
        if (m_floatingBar) {
            m_floatingBar->setParent(this);
            m_floatingBar->raise();
        }
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (m_floatingBar) {
            m_floatingBar->adjustSize();
            const int bx = (width() - m_floatingBar->width()) / 2;
            const int by = height() - m_floatingBar->height() - 14;
            m_floatingBar->move(qMax(8, bx), qMax(8, by));
            m_floatingBar->raise();
        }
    }

private:
    QWidget* m_floatingBar = nullptr;
};

// =========================================================================
// OcrResultDialog: WinUI 3 Fluent frameless window with independent draggable
// record cards, single-click copy, drag-to-merge, and drag-to-desktop.
// =========================================================================
class OcrResultDialog : public FluentBaseWidget {
    Q_OBJECT
public:
    explicit OcrResultDialog(QWidget* parent = nullptr)
        : FluentBaseWidget(parent)
        , m_imageView(new OcrResultImageView(this))
        , m_recordList(new OcrRecordListWidget(this))
    {
        setAttribute(Qt::WA_DeleteOnClose, false);
        setObjectName(QStringLiteral("OcrResultWindow"));
        setWindowTitle(tr("文字识别结果"));
        resize(1080, 680);
        setMinimumSize(780, 500);

        // WinUI 3 Fluent base window setup: Enable resize border, Minimize & Close buttons
        setResizable(true);
        setWindowBtnMode(All);
        if (m_maxBtn) {
            m_maxBtn->setVisible(false); // Retain Minimize and Close only
        }

        initCustomTitleBar();
        initUI();
        initStyles();
    }

    void setRecognitionResult(const QImage& originalImage, const QString& fullText, const QVector<OcrTextBox>& boxes) {
        m_originalImage = originalImage;
        m_imageView->setImage(originalImage);
        m_imageView->setBoxes(boxes);

        if (m_imageScrollArea) {
            m_imageView->setViewportSize(m_imageScrollArea->viewport()->size());
        }

        // Populate record cards
        QStringList lines = fullText.split(QStringLiteral("\n"), QString::SkipEmptyParts);
        if (lines.isEmpty() && !boxes.isEmpty()) {
            for (const auto& box : boxes) {
                if (!box.text.trimmed().isEmpty()) {
                    lines.append(box.text.trimmed());
                }
            }
        }
        m_recordList->setRecords(lines);
        updateHeaderStats();

        if (QScreen* screen = QApplication::primaryScreen()) {
            const QRect scrGeo = screen->availableGeometry();
            const int x = scrGeo.x() + (scrGeo.width() - width()) / 2;
            const int y = scrGeo.y() + (scrGeo.height() - height()) / 2;
            move(x, y);
        }

        show();
        raise();
        activateWindow();

        QTimer::singleShot(0, this, [this]() {
            fitImageToCurrentViewport();
        });
        QTimer::singleShot(60, this, [this]() {
            fitImageToCurrentViewport();
        });
    }

    void fitImageToCurrentViewport() {
        if (!m_imageView || !m_imageScrollArea || m_originalImage.isNull()) return;
        const QSize vp = m_imageScrollArea->viewport()->size();
        if (vp.width() > 50 && vp.height() > 50) {
            m_imageView->setViewportSize(vp);
            m_imageView->setFitToViewport(true);
            if (m_scaleLabelBtn) {
                m_scaleLabelBtn->setText(QStringLiteral("%1%").arg(qRound(m_imageView->currentScale() * 100)));
            }
        }
    }

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        QTimer::singleShot(0, this, [this]() {
            fitImageToCurrentViewport();
        });
        QTimer::singleShot(60, this, [this]() {
            fitImageToCurrentViewport();
        });
    }

    void setResult(const QImage& source, const QString& text, const QVector<OcrTextBox>& boxes) {
        setRecognitionResult(source, text, boxes);
    }

    void setError(const QImage& source, const QString& errorMessage) {
        setRecognitionResult(source, errorMessage, {});
        if (m_headerStatsLabel) {
            m_headerStatsLabel->setText(tr("识别失败: %1").arg(errorMessage));
        }
    }

    void showCopyToast(const QString& message = QString()) {
        if (!m_copyToast) return;
        m_copyToast->setText(message.isEmpty() ? tr("已复制到剪贴板") : message);
        m_copyToast->adjustSize();
        m_copyToast->move((width() - m_copyToast->width()) / 2, 42);
        m_copyToast->show();
        m_copyToast->raise();
        m_copyToastTimer->start(1500);
    }

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override {
#ifdef Q_OS_WIN
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            const HWND hwnd = reinterpret_cast<HWND>(winId());
            const LONG nativeX = GET_X_LPARAM(msg->lParam);
            const LONG nativeY = GET_Y_LPARAM(msg->lParam);
            RECT nativeRect{};
            GetWindowRect(hwnd, &nativeRect);
            const UINT dpi = GetDpiForWindow(hwnd);
            const qreal scale = dpi > 0 ? static_cast<qreal>(dpi) / 96.0 : qMax<qreal>(1.0, devicePixelRatioF());
            const int nativeBorder = qMax(4, qRound(8 * scale));
            const QPoint pos(qRound((nativeX - nativeRect.left) / scale),
                             qRound((nativeY - nativeRect.top) / scale));

            // Resizing handles on window edges
            if (!isMaximized()) {
                const bool left = nativeX >= nativeRect.left && nativeX < nativeRect.left + nativeBorder;
                const bool right = nativeX <= nativeRect.right && nativeX >= nativeRect.right - nativeBorder;
                const bool top = nativeY >= nativeRect.top && nativeY < nativeRect.top + nativeBorder;
                const bool bottom = nativeY <= nativeRect.bottom && nativeY >= nativeRect.bottom - nativeBorder;

                if (left && top) { *result = HTTOPLEFT; return true; }
                if (right && top) { *result = HTTOPRIGHT; return true; }
                if (left && bottom) { *result = HTBOTTOMLEFT; return true; }
                if (right && bottom) { *result = HTBOTTOMRIGHT; return true; }
                if (left) { *result = HTLEFT; return true; }
                if (right) { *result = HTRIGHT; return true; }
                if (top) { *result = HTTOP; return true; }
                if (bottom) { *result = HTBOTTOM; return true; }
            }

            // ONLY TitleBar area (height <= 32px) returns HTCAPTION for window dragging
            if (pos.y() >= 0 && pos.y() <= 32) {
                QWidget* child = childAt(pos);
                if (child && (qobject_cast<QPushButton*>(child) || child->inherits("QPushButton") || child->inherits("QAbstractButton"))) {
                    return false; // HTCLIENT for minimize/close buttons
                }
                *result = HTCAPTION;
                return true;
            }

            // Central workspace (y > 32): ALWAYS return HTCLIENT so cards, splitter, canvas get all mouse drag events!
            *result = HTCLIENT;
            return true;
        }
#endif
        return FluentBaseWidget::nativeEvent(eventType, message, result);
    }

    void mousePressEvent(QMouseEvent* event) override {
        // Only initiate window drag if left button pressed strictly inside title bar (pos.y() <= 32)
        if (event->button() == Qt::LeftButton && event->pos().y() <= 32) {
            m_windowDragging = true;
            m_windowDragPos = event->globalPos() - frameGeometry().topLeft();
            event->accept();
            return;
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_windowDragging && (event->buttons() & Qt::LeftButton) && !isMaximized()) {
            move(event->globalPos() - m_windowDragPos);
            event->accept();
            return;
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        m_windowDragging = false;
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override {
        FluentBaseWidget::resizeEvent(event);
        if (m_copyToast && m_copyToast->isVisible()) {
            m_copyToast->move((width() - m_copyToast->width()) / 2, 42);
        }
    }

private:
    void initCustomTitleBar() {
        // Integrate title and pill stats badge on the left side of the title bar
        auto* titleWidget = new QWidget(m_btnContainer);
        auto* titleLayout = new QHBoxLayout(titleWidget);
        titleLayout->setContentsMargins(14, 0, 0, 0);
        titleLayout->setSpacing(8);

        auto* iconLabel = new QLabel(QString::fromUtf8("\xEE\xAF\x84"), titleWidget); // Segoe MDL2 OCR Icon
        iconLabel->setObjectName(QStringLiteral("ocrTitleIcon"));
        titleLayout->addWidget(iconLabel);

        m_titleLabel = new QLabel(tr("文字识别"), titleWidget);
        m_titleLabel->setObjectName(QStringLiteral("ocrTitleLabel"));
        titleLayout->addWidget(m_titleLabel);

        m_headerStatsLabel = new QLabel(tr("就绪"), titleWidget);
        m_headerStatsLabel->setObjectName(QStringLiteral("ocrHeaderStatsBadge"));
        titleLayout->addWidget(m_headerStatsLabel);

        titleLayout->addStretch(1);

        m_btnLayout->insertWidget(0, titleWidget);
    }

    void initUI() {
        QVBoxLayout* rootLayout = centralLayout();
        rootLayout->setContentsMargins(16, 4, 16, 16);
        rootLayout->setSpacing(10);

        // --- Main Splitter ---
        auto* contentSplitter = new QSplitter(Qt::Horizontal, container());
        contentSplitter->setObjectName(QStringLiteral("OcrContentSplitter"));
        contentSplitter->setChildrenCollapsible(false);
        contentSplitter->setHandleWidth(8);

        // Left Container: Canvas & Floating Zoom Bar
        m_imageContainer = new OcrImageContainer(contentSplitter);
        m_imageContainer->setObjectName(QStringLiteral("imageContainerCard"));

        auto* imageContainerLayout = new QVBoxLayout(m_imageContainer);
        imageContainerLayout->setContentsMargins(0, 0, 0, 0);
        imageContainerLayout->setSpacing(0);

        m_imageScrollArea = new QScrollArea(m_imageContainer);
        m_imageScrollArea->setObjectName(QStringLiteral("transparentScrollArea"));
        m_imageScrollArea->setWidgetResizable(false);
        m_imageScrollArea->setAlignment(Qt::AlignCenter);
        m_imageScrollArea->setWidget(m_imageView);
        m_imageView->setScrollArea(m_imageScrollArea);
        imageContainerLayout->addWidget(m_imageScrollArea);

        // Floating Zoom Pill
        m_floatingZoomBar = new QWidget(m_imageContainer);
        m_floatingZoomBar->setObjectName(QStringLiteral("zoomFloatingPill"));
        auto* zoomBarLayout = new QHBoxLayout(m_floatingZoomBar);
        zoomBarLayout->setContentsMargins(6, 2, 6, 2);
        zoomBarLayout->setSpacing(4);

        m_zoomOutButton = new QPushButton(QStringLiteral("−"), m_floatingZoomBar);
        m_zoomOutButton->setObjectName(QStringLiteral("zoomFloatingBtn"));
        m_zoomOutButton->setToolTip(tr("缩小"));

        m_scaleLabelBtn = new QPushButton(QStringLiteral("100%"), m_floatingZoomBar);
        m_scaleLabelBtn->setObjectName(QStringLiteral("zoomFloatingBtn"));
        m_scaleLabelBtn->setToolTip(tr("点击重置为 100% 原始大小"));

        m_fitButton = new QPushButton(tr("适应"), m_floatingZoomBar);
        m_fitButton->setObjectName(QStringLiteral("zoomFloatingBtn"));
        m_fitButton->setToolTip(tr("适应视窗大小"));

        m_zoomInButton = new QPushButton(QStringLiteral("+"), m_floatingZoomBar);
        m_zoomInButton->setObjectName(QStringLiteral("zoomFloatingBtn"));
        m_zoomInButton->setToolTip(tr("放大"));

        zoomBarLayout->addWidget(m_zoomOutButton);
        zoomBarLayout->addWidget(m_scaleLabelBtn);
        zoomBarLayout->addWidget(m_fitButton);
        zoomBarLayout->addWidget(m_zoomInButton);

        m_imageContainer->setFloatingBar(m_floatingZoomBar);
        contentSplitter->addWidget(m_imageContainer);

        // Right Container: Draggable Record Cards List
        contentSplitter->addWidget(m_recordList);
        contentSplitter->setStretchFactor(0, 6);
        contentSplitter->setStretchFactor(1, 4);
        contentSplitter->setSizes({640, 420});

        rootLayout->addWidget(contentSplitter, 1);

        // Floating Toast Pill
        m_copyToast = new QLabel(tr("已复制到剪贴板"), this);
        m_copyToast->setObjectName(QStringLiteral("fluentToast"));
        m_copyToast->setAlignment(Qt::AlignCenter);
        m_copyToast->hide();

        m_copyToastTimer = new QTimer(this);
        m_copyToastTimer->setSingleShot(true);
        connect(m_copyToastTimer, &QTimer::timeout, this, [this]() {
            if (m_copyToast) m_copyToast->hide();
        });

        // Event Connections
        connect(m_recordList, &OcrRecordListWidget::toastRequested, this, &OcrResultDialog::showCopyToast);
        connect(m_recordList, &OcrRecordListWidget::recordsChanged, this, &OcrResultDialog::updateHeaderStats);

        connect(m_imageView, &OcrResultImageView::textCopied, this, [this](const QString&) {
            showCopyToast(tr("已复制所选区域文本"));
        });

        connect(m_imageView, &OcrResultImageView::toastRequested, this, &OcrResultDialog::showCopyToast);

        connect(m_imageView, &OcrResultImageView::scaleChanged, this, [this](double scale) {
            if (m_scaleLabelBtn) {
                m_scaleLabelBtn->setText(QStringLiteral("%1%").arg(qRound(scale * 100)));
            }
        });

        connect(m_fitButton, &QPushButton::clicked, this, [this]() {
            m_imageView->setFitToViewport(true);
            m_imageView->setViewportSize(m_imageScrollArea->viewport()->size());
        });

        connect(m_scaleLabelBtn, &QPushButton::clicked, this, [this]() {
            m_imageView->resetToActualSize();
        });

        connect(m_zoomInButton, &QPushButton::clicked, m_imageView, &OcrResultImageView::zoomIn);
        connect(m_zoomOutButton, &QPushButton::clicked, m_imageView, &OcrResultImageView::zoomOut);
    }

    void updateHeaderStats() {
        if (!m_headerStatsLabel || !m_recordList) return;
        const int count = m_recordList->recordCount();
        const int chars = m_recordList->fullText().length();
        m_headerStatsLabel->setText(tr("%1 条记录 · %2 字符").arg(count).arg(chars));
    }

    void initStyles() {
        container()->setStyleSheet(QStringLiteral(
            "QWidget#fluentBaseContainer {"
            "  background-color: #f3f3f3;"
            "  border: 1px solid rgba(0, 0, 0, 0.12);"
            "  border-radius: 8px;"
            "}"
        ));

        setStyleSheet(QStringLiteral(
            "#ocrTitleIcon {"
            "  font-family: 'Segoe MDL2 Assets';"
            "  font-size: 13px;"
            "  color: #0067c0;"
            "}"
            "#ocrTitleLabel {"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 12px;"
            "  font-weight: 600;"
            "  color: #18181b;"
            "}"
            "#ocrHeaderStatsBadge {"
            "  background-color: #e4e4e7;"
            "  color: #52525b;"
            "  border-radius: 9px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 11px;"
            "  padding: 1px 8px;"
            "}"
            "#imageContainerCard {"
            "  background-color: #f7f8fa;"
            "  border: 1px solid #e5e5e5;"
            "  border-radius: 8px;"
            "}"
            "#transparentScrollArea {"
            "  background: transparent;"
            "  border: none;"
            "}"
            "/* WinUI 3 Fluent Design Modern Scrollbars */"
            "QScrollBar:vertical {"
            "  border: none;"
            "  background: transparent;"
            "  width: 6px;"
            "  margin: 3px 1px 3px 0;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: rgba(0, 0, 0, 0.22);"
            "  border-radius: 3px;"
            "  min-height: 28px;"
            "}"
            "QScrollBar::handle:vertical:hover {"
            "  background: rgba(0, 0, 0, 0.42);"
            "}"
            "QScrollBar::handle:vertical:pressed {"
            "  background: rgba(0, 0, 0, 0.60);"
            "}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
            "  height: 0px;"
            "  background: none;"
            "  border: none;"
            "}"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
            "  background: none;"
            "}"
            "QScrollBar:horizontal {"
            "  border: none;"
            "  background: transparent;"
            "  height: 6px;"
            "  margin: 0 3px 1px 3px;"
            "}"
            "QScrollBar::handle:horizontal {"
            "  background: rgba(0, 0, 0, 0.22);"
            "  border-radius: 3px;"
            "  min-width: 28px;"
            "}"
            "QScrollBar::handle:horizontal:hover {"
            "  background: rgba(0, 0, 0, 0.42);"
            "}"
            "QScrollBar::handle:horizontal:pressed {"
            "  background: rgba(0, 0, 0, 0.60);"
            "}"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
            "  width: 0px;"
            "  background: none;"
            "  border: none;"
            "}"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
            "  background: none;"
            "}"
            "QAbstractScrollArea::corner {"
            "  background: transparent;"
            "  border: none;"
            "}"
            "#zoomFloatingPill {"
            "  background-color: rgba(255, 255, 255, 0.94);"
            "  border: 1px solid rgba(0, 0, 0, 0.10);"
            "  border-radius: 16px;"
            "  padding: 2px 6px;"
            "}"
            "#zoomFloatingBtn {"
            "  background: transparent;"
            "  border: none;"
            "  border-radius: 12px;"
            "  color: #27272a;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 12px;"
            "  font-weight: 500;"
            "  padding: 3px 10px;"
            "  min-height: 22px;"
            "}"
            "#zoomFloatingBtn:hover {"
            "  background-color: rgba(0, 0, 0, 0.06);"
            "  color: #000000;"
            "}"
            "#zoomFloatingBtn:pressed {"
            "  background-color: rgba(0, 0, 0, 0.12);"
            "}"
            "#fluentToast {"
            "  background-color: rgba(24, 24, 27, 0.90);"
            "  color: #fafafa;"
            "  border: 1px solid rgba(255, 255, 255, 0.12);"
            "  border-radius: 14px;"
            "  padding: 6px 18px;"
            "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "  font-size: 12px;"
            "  font-weight: 500;"
            "}"
        ));
    }

    QImage m_originalImage;
    OcrResultImageView* m_imageView = nullptr;
    OcrImageContainer* m_imageContainer = nullptr;
    QScrollArea* m_imageScrollArea = nullptr;
    QWidget* m_floatingZoomBar = nullptr;
    OcrRecordListWidget* m_recordList = nullptr;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_headerStatsLabel = nullptr;
    QPushButton* m_fitButton = nullptr;
    QPushButton* m_scaleLabelBtn = nullptr;
    QPushButton* m_zoomInButton = nullptr;
    QPushButton* m_zoomOutButton = nullptr;
    QLabel* m_copyToast = nullptr;
    QTimer* m_copyToastTimer = nullptr;
    bool m_windowDragging = false;
    QPoint m_windowDragPos;
};
