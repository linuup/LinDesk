#include "desktop/DesktopSnakeGame.h"

#include "app/AppFonts.h"
#include "desktop/ListView.h"

#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace {

constexpr int kNormalMoveIntervalMs = 220;
constexpr int kAcceleratedMoveIntervalMs = 85;
constexpr int kMinimumNormalMoveIntervalMs = 150;
constexpr int kRenderIntervalMs = 16;
constexpr int kCollisionFlashIntervalMs = 140;
constexpr int kEatEffectDurationMs = 360;

QString gameText(const char* text)
{
    return QCoreApplication::translate("DesktopSnakeGame", text);
}

QRect hostUsableRect(QWidget* host)
{
    if (!host) {
        return QRect();
    }

    QScreen* activeScreen = host->screen();
    if (!activeScreen && host->windowHandle()) {
        activeScreen = host->windowHandle()->screen();
    }
    if (!activeScreen) {
        activeScreen = QApplication::primaryScreen();
    }
    if (!activeScreen) {
        return host->rect();
    }

    const QRect globalHostRect(host->mapToGlobal(QPoint(0, 0)), host->size());
    const QRect usableGlobalRect = activeScreen->availableGeometry().intersected(globalHostRect);
    if (!usableGlobalRect.isValid() || usableGlobalRect.isEmpty()) {
        return host->rect();
    }

    const QRect usableLocalRect(usableGlobalRect.topLeft() - globalHostRect.topLeft(), usableGlobalRect.size());
    return usableLocalRect.intersected(host->rect());
}

class DesktopSnakeGameWidget : public QWidget
{
public:
    explicit DesktopSnakeGameWidget(const QVector<DesktopSnakeGameIcon>& icons, QWidget* parent)
        : QWidget(parent)
        , m_icons(icons)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_StyledBackground, false);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::StrongFocus);

        m_timer.setInterval(kNormalMoveIntervalMs);
        connect(&m_timer, &QTimer::timeout, this, [this]() { advance(); });
        m_renderTimer.setInterval(kRenderIntervalMs);
        connect(&m_renderTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
        m_collisionFlashTimer.setInterval(kCollisionFlashIntervalMs);
        connect(&m_collisionFlashTimer, &QTimer::timeout, this, [this]() {
            m_collisionFlashVisible = !m_collisionFlashVisible;
            update();
        });

        m_restartButton = new QPushButton(gameText("Restart"), this);
        m_closeButton = new QPushButton(gameText("Close"), this);
        styleGameButton(m_restartButton, true);
        styleGameButton(m_closeButton, false);

        connect(m_restartButton, &QPushButton::clicked, this, [this]() { resetGame(); });
        connect(m_closeButton, &QPushButton::clicked, this, [this]() { close(); });

        resetGame();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        drawIconCell(painter, m_food, m_foodIconIndex, true);

        const qreal progress = animationProgress();
        for (int i = m_snake.size() - 1; i >= 0; --i) {
            const QPoint from = i < m_segmentStartCells.size() ? m_segmentStartCells.at(i) : m_snake.at(i);
            const int iconIndex = i < m_snakeIconIndices.size() ? m_snakeIconIndices.at(i) : 0;
            drawMovingIconCell(painter, from, m_snake.at(i), iconIndex, progress, i == 0);
        }

        drawEatEffect(painter);
        drawScore(painter);

        if (m_roundState != RoundState::Running) {
            painter.fillRect(rect(), QColor(4, 8, 14, 118));
            drawCollisionMarker(painter);
            painter.setPen(Qt::white);
            QFont titleFont = AppFonts::uiFont(26);
            titleFont.setWeight(QFont::Bold);
            painter.setFont(titleFont);
            painter.drawText(rect().adjusted(0, -56, 0, 0), Qt::AlignCenter,
                m_roundState == RoundState::Won ? gameText("You Win") : gameText("Game Over"));

            QFont scoreFont = AppFonts::uiFont(13);
            scoreFont.setWeight(QFont::DemiBold);
            painter.setFont(scoreFont);
            painter.setPen(QColor(226, 232, 240));
            painter.drawText(rect().adjusted(0, -16, 0, 0), Qt::AlignCenter, gameText("Score %1").arg(m_score));
        }
    }

    void resizeEvent(QResizeEvent*) override
    {
        updateBoardMetrics();
        positionGameOverButtons();
        if (!m_initialized) {
            resetGame();
        }
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            close();
            event->accept();
            return;
        }

        if (m_roundState != RoundState::Running) {
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
                resetGame();
                event->accept();
                return;
            }
            QWidget::keyPressEvent(event);
            return;
        }

        const QPoint nextDirection = directionForKey(event->key());
        if (nextDirection.isNull()) {
            QWidget::keyPressEvent(event);
            return;
        }

        if (!event->isAutoRepeat()) {
            m_heldDirectionKeys.insert(event->key());
        }

        queueDirection(nextDirection);
        event->accept();
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        if (directionForKey(event->key()).isNull()) {
            QWidget::keyReleaseEvent(event);
            return;
        }

        if (!event->isAutoRepeat()) {
            m_heldDirectionKeys.remove(event->key());
        }
        event->accept();
    }

    void closeEvent(QCloseEvent* event) override
    {
        releaseGameResources();
        QWidget::closeEvent(event);
    }

private:
    enum class RoundState {
        Running,
        GameOver,
        Won
    };

    void styleGameButton(QPushButton* button, bool primary)
    {
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(primary ? QSize(112, 40) : QSize(92, 40));
        button->setFont(AppFonts::uiFont(11));
        button->setStyleSheet(primary
            ? QStringLiteral("QPushButton { background: #2563eb; color: white; border: none; border-radius: 8px; font-weight: 700; } QPushButton:hover { background: #1d4ed8; }")
            : QStringLiteral("QPushButton { background: rgba(255,255,255,220); color: #111827; border: none; border-radius: 8px; font-weight: 700; } QPushButton:hover { background: white; }"));
        button->hide();
    }

    void releaseGameResources()
    {
        m_timer.stop();
        m_renderTimer.stop();
        m_collisionFlashTimer.stop();
        m_tickProgressTimer.invalidate();
        m_eatEffectTimer.invalidate();
        m_heldDirectionKeys.clear();
        m_pendingDirections.clear();
    }

    void updateBoardMetrics()
    {
        const int base = qMin(width() / 22, height() / 16);
        const int nextCellSize = qBound(34, base, 56);
        if (m_cellSize != nextCellSize || m_scaledIcons.isEmpty()) {
            m_cellSize = nextCellSize;
            rebuildScaledIcons();
        }
        m_cols = qMax(8, width() / m_cellSize);
        m_rows = qMax(6, height() / m_cellSize);
    }

    QRect boardRect() const
    {
        const QSize boardSize(m_cols * m_cellSize, m_rows * m_cellSize);
        return QRect(QPoint((width() - boardSize.width()) / 2, (height() - boardSize.height()) / 2), boardSize);
    }

    QRect cellRect(const QPoint& cell) const
    {
        const QRect board = boardRect();
        return QRect(board.left() + cell.x() * m_cellSize, board.top() + cell.y() * m_cellSize, m_cellSize, m_cellSize);
    }

    QRectF cellRect(const QPointF& cell) const
    {
        const QRect board = boardRect();
        return QRectF(board.left() + cell.x() * m_cellSize, board.top() + cell.y() * m_cellSize, m_cellSize, m_cellSize);
    }

    void drawIconCell(QPainter& painter, const QPoint& cell, int iconIndex, bool food)
    {
        drawIconCell(painter, QPointF(cell), iconIndex, food);
    }

    void drawIconCell(QPainter& painter, const QPointF& cell, int iconIndex, bool food)
    {
        if (m_scaledIcons.isEmpty()) {
            return;
        }

        const QRectF cellArea = cellRect(cell).adjusted(5, 5, -5, -5);
        if (food || m_icons.size() == 1) {
            painter.save();
            painter.setPen(QPen(food ? QColor(250, 204, 21) : QColor(45, 212, 191), 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(cellArea.adjusted(-2, -2, 2, 2), 10, 10);
            painter.restore();
        }

        const QPixmap& pixmap = m_scaledIcons.at(qBound(0, iconIndex, m_scaledIcons.size() - 1));
        const QPointF pixmapTopLeft(
            cellArea.center().x() - pixmap.width() / 2.0,
            cellArea.center().y() - pixmap.height() / 2.0);
        painter.drawPixmap(pixmapTopLeft, pixmap);
    }

    void drawMovingIconCell(QPainter& painter, const QPoint& from, const QPoint& to, int iconIndex, qreal progress, bool head)
    {
        if (m_icons.isEmpty()) {
            return;
        }

        const QPointF fromCell(from);
        QPointF toCell(to);
        if (head && progress > 0.72 && !m_pendingDirections.isEmpty()) {
            const qreal bendProgress = (progress - 0.72) / 0.28;
            toCell += QPointF(m_pendingDirections.first()) * qMin<qreal>(0.28, bendProgress * 0.28);
        }
        const int dx = to.x() - from.x();
        const int dy = to.y() - from.y();

        if (qAbs(dx) > 1) {
            const qreal exitX = dx < 0 ? m_cols : -1;
            const qreal enterX = dx < 0 ? -1 : m_cols;
            drawIconCell(painter, QPointF(from.x() + (exitX - from.x()) * progress, from.y()), iconIndex, false);
            drawIconCell(painter, QPointF(enterX + (to.x() - enterX) * progress, to.y()), iconIndex, false);
            return;
        }

        if (qAbs(dy) > 1) {
            const qreal exitY = dy < 0 ? m_rows : -1;
            const qreal enterY = dy < 0 ? -1 : m_rows;
            drawIconCell(painter, QPointF(from.x(), from.y() + (exitY - from.y()) * progress), iconIndex, false);
            drawIconCell(painter, QPointF(to.x(), enterY + (to.y() - enterY) * progress), iconIndex, false);
            return;
        }

        drawIconCell(painter, fromCell + (toCell - fromCell) * progress, iconIndex, false);
    }

    qreal animationProgress() const
    {
        if (m_roundState != RoundState::Running || !m_tickProgressTimer.isValid()) {
            return 1.0;
        }

        return qBound(0.0, static_cast<qreal>(m_tickProgressTimer.elapsed()) / qMax(1, m_tickIntervalMs), 1.0);
    }

    void drawScore(QPainter& painter)
    {
        painter.save();
        QFont scoreFont = AppFonts::uiFont(12);
        scoreFont.setWeight(QFont::DemiBold);
        painter.setFont(scoreFont);
        painter.setPen(QColor(226, 232, 240));
        painter.drawText(rect().adjusted(0, 10, 0, 0), Qt::AlignHCenter | Qt::AlignTop, gameText("Score %1").arg(m_score));
        painter.restore();
    }

    void drawCollisionMarker(QPainter& painter)
    {
        if (!m_collisionFlashVisible || m_collisionCell.x() < 0 || m_collisionCell.y() < 0) {
            return;
        }

        painter.save();
        const QRect markerRect = cellRect(m_collisionCell).adjusted(2, 2, -2, -2);
        painter.setPen(QPen(QColor(248, 113, 113), 4));
        painter.setBrush(QColor(248, 113, 113, 80));
        painter.drawRoundedRect(markerRect, 10, 10);
        painter.restore();
    }

    void drawEatEffect(QPainter& painter)
    {
        if (m_eatEffectCell.x() < 0 || m_eatEffectCell.y() < 0 || !m_eatEffectTimer.isValid()) {
            return;
        }

        const qint64 elapsed = m_eatEffectTimer.elapsed();
        if (elapsed >= kEatEffectDurationMs) {
            return;
        }

        const qreal progress = qBound(0.0, static_cast<qreal>(elapsed) / kEatEffectDurationMs, 1.0);
        const QRectF baseRect = cellRect(m_eatEffectCell).adjusted(5, 5, -5, -5);
        const qreal primarySpread = 6.0 + progress * 24.0;
        const qreal secondarySpread = qMax(0.0, (progress - 0.18) * 30.0);
        const int alpha = qBound(0, static_cast<int>((1.0 - progress) * 245), 245);
        const int secondaryAlpha = qBound(0, static_cast<int>((1.0 - progress) * 170), 170);

        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(250, 204, 21, alpha / 4));
        painter.drawEllipse(baseRect.adjusted(-primarySpread * 0.5, -primarySpread * 0.5, primarySpread * 0.5, primarySpread * 0.5));

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(250, 204, 21, alpha), 5));
        painter.drawEllipse(baseRect.adjusted(-primarySpread, -primarySpread, primarySpread, primarySpread));

        painter.setPen(QPen(QColor(45, 212, 191, secondaryAlpha), 3));
        painter.drawEllipse(baseRect.adjusted(-secondarySpread, -secondarySpread, secondarySpread, secondarySpread));

        QFont popFont = AppFonts::uiFont(13);
        popFont.setWeight(QFont::Bold);
        painter.setFont(popFont);
        painter.setPen(QColor(255, 255, 255, alpha));
        const QRectF textRect = baseRect.translated(0, -progress * 28.0 - 8.0).adjusted(-12, -10, 12, 10);
        const QString eatText = m_eatEffectName.isEmpty()
            ? QStringLiteral("+1")
            : QStringLiteral("%1 +1").arg(m_eatEffectName);
        painter.drawText(textRect.adjusted(-44, 0, 44, 0), Qt::AlignCenter, eatText);
        painter.restore();
    }

    void resetGame()
    {
        updateBoardMetrics();
        m_initialized = true;
        m_roundState = RoundState::Running;
        m_score = 0;
        m_collisionCell = QPoint(-1, -1);
        m_eatEffectCell = QPoint(-1, -1);
        m_eatEffectName.clear();
        m_eatEffectTimer.invalidate();
        m_collisionFlashVisible = false;
        m_collisionFlashTimer.stop();
        m_heldDirectionKeys.clear();
        m_pendingDirections.clear();
        updateMoveInterval();
        m_direction = QPoint(1, 0);
        m_nextDirection = m_direction;
        m_snake.clear();
        m_segmentStartCells.clear();
        m_snakeIconIndices.clear();

        const QPoint center(m_cols / 2, m_rows / 2);
        m_snake.append(center);
        m_snake.append(center + QPoint(-1, 0));
        m_snake.append(center + QPoint(-2, 0));
        m_segmentStartCells = m_snake;
        for (int i = 0; i < m_snake.size(); ++i) {
            m_snakeIconIndices.append(iconIndexForSequence(i));
        }
        m_nextFoodIconSequence = m_snake.size();

        placeFood();
        m_restartButton->hide();
        m_closeButton->hide();
        m_tickIntervalMs = m_timer.interval();
        m_timer.start();
        m_renderTimer.start();
        m_tickProgressTimer.restart();
        setFocus(Qt::OtherFocusReason);
        update();
    }

    void advance()
    {
        if (m_roundState != RoundState::Running || m_snake.isEmpty()) {
            return;
        }

        consumeQueuedDirection();
        m_direction = m_nextDirection;
        const QVector<QPoint> oldSnake = m_snake;
        const QPoint nextHead = wrappedCell(m_snake.first() + m_direction);
        const bool grows = (nextHead == m_food);
        const int collisionCheckCount = grows ? m_snake.size() : qMax(0, m_snake.size() - 1);
        for (int i = 0; i < collisionCheckCount; ++i) {
            if (m_snake.at(i) != nextHead) {
                continue;
            }
            finishRound(RoundState::GameOver, nextHead);
            return;
        }

        m_snake.prepend(nextHead);
        const int eatenIconIndex = m_foodIconIndex;
        if (grows) {
            m_snakeIconIndices.append(eatenIconIndex);
            normalizeSnakeIconIndices();
            startEatEffect(nextHead, eatenIconIndex);
            ++m_score;
            if (m_snake.size() >= m_cols * m_rows) {
                rebuildSegmentStartCells(oldSnake);
                finishRound(RoundState::Won);
                return;
            }
            placeFood();
        } else {
            m_snake.removeLast();
            normalizeSnakeIconIndices();
        }
        rebuildSegmentStartCells(oldSnake);
        updateMoveInterval();
        m_tickIntervalMs = m_timer.interval();
        m_tickProgressTimer.restart();
        update();
    }

    bool placeFood()
    {
        QVector<QPoint> freeCells;
        freeCells.reserve(m_cols * m_rows);
        for (int y = 0; y < m_rows; ++y) {
            for (int x = 0; x < m_cols; ++x) {
                const QPoint cell(x, y);
                if (!m_snake.contains(cell)) {
                    freeCells.append(cell);
                }
            }
        }

        if (freeCells.isEmpty()) {
            finishRound(RoundState::Won);
            return false;
        }

        const int index = QRandomGenerator::global()->bounded(freeCells.size());
        m_food = freeCells.at(index);
        m_foodIconIndex = iconIndexForSequence(m_nextFoodIconSequence++);
        return true;
    }

    QPoint wrappedCell(const QPoint& cell) const
    {
        const int x = (cell.x() + m_cols) % m_cols;
        const int y = (cell.y() + m_rows) % m_rows;
        return QPoint(x, y);
    }

    void finishRound(RoundState state, const QPoint& collisionCell = QPoint(-1, -1))
    {
        m_roundState = state;
        m_collisionCell = collisionCell;
        m_eatEffectCell = QPoint(-1, -1);
        m_eatEffectName.clear();
        m_eatEffectTimer.invalidate();
        m_collisionFlashVisible = (state == RoundState::GameOver && collisionCell.x() >= 0 && collisionCell.y() >= 0);
        m_heldDirectionKeys.clear();
        m_pendingDirections.clear();
        updateMoveInterval();
        m_timer.stop();
        m_renderTimer.stop();
        if (m_collisionFlashVisible) {
            m_collisionFlashTimer.start();
        } else {
            m_collisionFlashTimer.stop();
        }
        positionGameOverButtons();
        m_restartButton->show();
        m_closeButton->show();
        m_restartButton->raise();
        m_closeButton->raise();
        update();
    }

    void positionGameOverButtons()
    {
        const int gap = 12;
        const int totalWidth = m_restartButton->width() + gap + m_closeButton->width();
        const int y = height() / 2 + 38;
        m_restartButton->move((width() - totalWidth) / 2, y);
        m_closeButton->move(m_restartButton->geometry().right() + gap + 1, y);
    }

    void rebuildSegmentStartCells(const QVector<QPoint>& oldSnake)
    {
        m_segmentStartCells.clear();
        m_segmentStartCells.reserve(m_snake.size());
        for (int i = 0; i < m_snake.size(); ++i) {
            if (oldSnake.isEmpty()) {
                m_segmentStartCells.append(m_snake.at(i));
            } else if (i < oldSnake.size()) {
                m_segmentStartCells.append(oldSnake.at(i));
            } else {
                m_segmentStartCells.append(m_snake.at(i));
            }
        }
    }

    QPoint directionForKey(int key) const
    {
        switch (key) {
        case Qt::Key_Left:
        case Qt::Key_A:
            return QPoint(-1, 0);
        case Qt::Key_Right:
        case Qt::Key_D:
            return QPoint(1, 0);
        case Qt::Key_Up:
        case Qt::Key_W:
            return QPoint(0, -1);
        case Qt::Key_Down:
        case Qt::Key_S:
            return QPoint(0, 1);
        default:
            return QPoint();
        }
    }

    void updateMoveInterval()
    {
        m_timer.setInterval(currentMoveInterval());
    }

    void rebuildScaledIcons()
    {
        m_scaledIcons.clear();
        const QSize iconSize(qMax(1, m_cellSize - 10), qMax(1, m_cellSize - 10));
        m_scaledIcons.reserve(m_icons.size());
        for (const DesktopSnakeGameIcon& icon : m_icons) {
            if (icon.pixmap.isNull()) {
                continue;
            }
            m_scaledIcons.append(icon.pixmap.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    void normalizeSnakeIconIndices()
    {
        while (m_snakeIconIndices.size() > m_snake.size()) {
            m_snakeIconIndices.removeLast();
        }
        while (m_snakeIconIndices.size() < m_snake.size()) {
            m_snakeIconIndices.append(iconIndexForSequence(m_snakeIconIndices.size()));
        }
    }

    void startEatEffect(const QPoint& cell, int iconIndex)
    {
        m_eatEffectCell = cell;
        m_eatEffectName = iconName(iconIndex);
        m_eatEffectTimer.restart();
    }

    int iconIndexForSequence(int sequence) const
    {
        return m_icons.isEmpty() ? 0 : sequence % m_icons.size();
    }

    QString iconName(int iconIndex) const
    {
        if (m_icons.isEmpty()) {
            return QString();
        }
        const int index = qBound(0, iconIndex, m_icons.size() - 1);
        return m_icons.at(index).displayName;
    }

    int currentMoveInterval() const
    {
        if (!m_heldDirectionKeys.isEmpty()) {
            return kAcceleratedMoveIntervalMs;
        }

        const int scoreReduction = qMin(70, m_score * 4);
        return qMax(kMinimumNormalMoveIntervalMs, kNormalMoveIntervalMs - scoreReduction);
    }

    void queueDirection(const QPoint& direction)
    {
        const QPoint reference = m_pendingDirections.isEmpty() ? m_nextDirection : m_pendingDirections.last();
        if (direction + reference == QPoint(0, 0) && m_snake.size() > 1) {
            return;
        }
        if (!m_pendingDirections.isEmpty() && m_pendingDirections.last() == direction) {
            return;
        }

        m_pendingDirections.append(direction);
        while (m_pendingDirections.size() > 2) {
            m_pendingDirections.removeFirst();
        }
        if (m_pendingDirections.size() == 1 && !(direction + m_direction == QPoint(0, 0) && m_snake.size() > 1)) {
            m_nextDirection = direction;
        }
    }

    void consumeQueuedDirection()
    {
        while (!m_pendingDirections.isEmpty()) {
            const QPoint direction = m_pendingDirections.takeFirst();
            if (direction + m_direction == QPoint(0, 0) && m_snake.size() > 1) {
                continue;
            }
            m_nextDirection = direction;
            return;
        }
    }

    QVector<DesktopSnakeGameIcon> m_icons;
    QVector<QPixmap> m_scaledIcons;
    QVector<QPoint> m_snake;
    QVector<QPoint> m_segmentStartCells;
    QVector<int> m_snakeIconIndices;
    QVector<QPoint> m_pendingDirections;
    QSet<int> m_heldDirectionKeys;
    QPoint m_food;
    int m_foodIconIndex = 0;
    int m_nextFoodIconSequence = 0;
    QPoint m_collisionCell = QPoint(-1, -1);
    QPoint m_eatEffectCell = QPoint(-1, -1);
    QString m_eatEffectName;
    QPoint m_direction = QPoint(1, 0);
    QPoint m_nextDirection = QPoint(1, 0);
    QTimer m_timer;
    QTimer m_renderTimer;
    QTimer m_collisionFlashTimer;
    QElapsedTimer m_tickProgressTimer;
    QElapsedTimer m_eatEffectTimer;
    QPushButton* m_restartButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    int m_score = 0;
    int m_cellSize = 44;
    int m_cols = 12;
    int m_rows = 10;
    int m_tickIntervalMs = kNormalMoveIntervalMs;
    bool m_initialized = false;
    bool m_collisionFlashVisible = false;
    RoundState m_roundState = RoundState::Running;
};

} // namespace

DesktopSnakeGameController::DesktopSnakeGameController(ListView* host)
    : QObject(host)
    , m_host(host)
{
    if (m_host) {
        m_host->installEventFilter(this);
    }
}

DesktopSnakeGameController::~DesktopSnakeGameController()
{
    if (m_host) {
        m_host->removeEventFilter(this);
    }
}

void DesktopSnakeGameController::onDesktopIconsVisibilityChanged()
{
    if (!m_host || m_host->desktopIconsVisible()) {
        resetTrigger();
        hideStartPrompt();
        closeGame();
    }
}

bool DesktopSnakeGameController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_host || !m_host) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Resize) {
        updatePromptGeometry();
        updateGameGeometry();
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        return handleHostKeyPress(static_cast<QKeyEvent*>(event));
    }

    return QObject::eventFilter(watched, event);
}

bool DesktopSnakeGameController::handleHostKeyPress(QKeyEvent* event)
{
    if (!m_host || m_host->desktopIconsVisible()) {
        resetTrigger();
        hideStartPrompt();
        closeGame();
        return false;
    }

    if (event->key() == Qt::Key_Escape) {
        hideStartPrompt();
        resetTrigger();
        return false;
    }

    if (m_startPrompt || m_gameWidget) {
        return false;
    }

    const Qt::KeyboardModifiers blockedModifiers = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    if (event->modifiers() & blockedModifiers) {
        return false;
    }

    const QString text = event->text().toLower();
    if (text.size() != 1 || !text.at(0).isLetter()) {
        return false;
    }

    m_typedBuffer.append(text);
    while (m_typedBuffer.size() > 4) {
        m_typedBuffer.remove(0, 1);
    }

    if (m_typedBuffer.endsWith(QStringLiteral("game"))) {
        showStartPrompt();
        resetTrigger();
        event->accept();
        return true;
    }

    return false;
}

void DesktopSnakeGameController::resetTrigger()
{
    m_typedBuffer.clear();
}

void DesktopSnakeGameController::showStartPrompt()
{
    if (!m_host || m_host->desktopIconsVisible() || m_gameWidget) {
        return;
    }

    if (!m_startPrompt) {
        QWidget* prompt = new QWidget(m_host);
        prompt->setAttribute(Qt::WA_DeleteOnClose);
        prompt->setAttribute(Qt::WA_TranslucentBackground);
        prompt->setAutoFillBackground(false);
        prompt->setStyleSheet(QStringLiteral("QWidget { background: rgba(15, 23, 42, 156); border-radius: 16px; }"));

        QPushButton* button = new QPushButton(gameText("Start"), prompt);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(142, 44);
        button->setFont(AppFonts::uiFont(13));
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: #2563eb; color: white; border: none; border-radius: 10px; font-weight: 800; font-size: 16px; padding: 10px 28px; }"
            "QPushButton:hover { background: #1d4ed8; }"));
        button->move((196 - button->width()) / 2, (82 - button->height()) / 2);

        connect(button, &QPushButton::clicked, this, [this]() { startGame(); });

        m_startPrompt = prompt;
        m_startButton = button;
    }

    updatePromptGeometry();
    m_startPrompt->show();
    m_startPrompt->raise();
}

void DesktopSnakeGameController::hideStartPrompt()
{
    if (m_startPrompt) {
        m_startPrompt->close();
        m_startPrompt = nullptr;
        m_startButton = nullptr;
    }
}

void DesktopSnakeGameController::startGame()
{
    if (!m_host || m_host->desktopIconsVisible()) {
        hideStartPrompt();
        return;
    }

    const QVector<DesktopSnakeGameIcon> icons = m_host->desktopSnakeIcons();
    if (icons.isEmpty()) {
        hideStartPrompt();
        m_host->showTransientTip(gameText("No desktop icons available for the game."), 2400);
        return;
    }

    hideStartPrompt();
    closeGame();

    DesktopSnakeGameWidget* game = new DesktopSnakeGameWidget(icons, m_host);
    m_gameWidget = game;
    connect(game, &QObject::destroyed, this, [this, game]() {
        if (m_gameWidget == game) {
            m_gameWidget = nullptr;
        }
    });
    updateGameGeometry();
    game->show();
    game->raise();
    game->setFocus(Qt::OtherFocusReason);
}

void DesktopSnakeGameController::closeGame()
{
    if (m_gameWidget) {
        m_gameWidget->close();
        m_gameWidget = nullptr;
    }
}

void DesktopSnakeGameController::updatePromptGeometry()
{
    if (!m_host || !m_startPrompt) {
        return;
    }

    const QSize promptSize(196, 82);
    const QRect hostRect = hostUsableRect(m_host);
    m_startPrompt->setGeometry(
        hostRect.center().x() - promptSize.width() / 2,
        hostRect.center().y() - promptSize.height() / 2,
        promptSize.width(),
        promptSize.height());
}

void DesktopSnakeGameController::updateGameGeometry()
{
    if (!m_host || !m_gameWidget) {
        return;
    }

    m_gameWidget->setGeometry(hostUsableRect(m_host));
}
