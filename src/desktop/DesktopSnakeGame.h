#pragma once

#include <QObject>
#include <QPointer>
#include <QPixmap>
#include <QString>
#include <QVector>

class ListView;
class QKeyEvent;
class QPushButton;
class QWidget;

struct DesktopSnakeGameIcon
{
    QPixmap pixmap;
    QString displayName;
};

class DesktopSnakeGameController : public QObject
{
public:
    explicit DesktopSnakeGameController(ListView* host);
    ~DesktopSnakeGameController() override;

    void onDesktopIconsVisibilityChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleHostKeyPress(QKeyEvent* event);
    void resetTrigger();
    void showStartPrompt();
    void hideStartPrompt();
    void startGame();
    void closeGame();
    void updatePromptGeometry();
    void updateGameGeometry();

    ListView* m_host = nullptr;
    QString m_typedBuffer;
    QPointer<QWidget> m_startPrompt;
    QPointer<QPushButton> m_startButton;
    QPointer<QWidget> m_gameWidget;
};
