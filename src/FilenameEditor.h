#pragma once
#include <QTextEdit>
#include <QKeyEvent>
#include <QFocusEvent>

class FilenameEditor : public QTextEdit {
    Q_OBJECT

public:
    explicit FilenameEditor(QWidget* parent = nullptr);

    void setInitialText(const QString& text, const QString& fullFileName);
    QString text() const;
    void adjustHeightToContents(int maxHeight = -1);
    static int heightForText(const QString& text, const QFont& font, int width, int maxHeight = -1);

signals:
    void commitRequested();
    void cancelRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void applyCenteredFormat();
    QString m_originalText;
};
