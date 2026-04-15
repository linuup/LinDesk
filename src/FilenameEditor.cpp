#include "FilenameEditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFileInfo>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QTextOption>
#include <QtMath>

FilenameEditor::FilenameEditor(QWidget* parent)
    : QTextEdit(parent)
{
    setAcceptRichText(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setLineWrapMode(QTextEdit::WidgetWidth);

    document()->setDocumentMargin(0);
    QTextOption textOption = document()->defaultTextOption();
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    textOption.setAlignment(Qt::AlignHCenter);
    document()->setDefaultTextOption(textOption);

    setStyleSheet(
        "QTextEdit {"
        "  background: white;"
        "  color: black;"
        "  border: 1px solid #0078d7;"
        "  padding: 0px;"
        "  margin: 0px;"
        "  selection-background-color: #0078d7;"
        "  selection-color: white;"
        "}"
    );

    // Windows illegal chars: \ / : * ? \" < > |
    QRegularExpression rx("[^\\\\/:*?\"<>|]*");
    auto* validator = new QRegularExpressionValidator(rx, this);
    connect(this, &QTextEdit::textChanged, this, [this, validator]() {
        QString current = toPlainText();
        if (current.size() > 255) {
            current = current.left(255);
        }

        int pos = 0;
        if (validator->validate(current, pos) != QValidator::Acceptable) {
            QString filtered;
            filtered.reserve(current.size());
            for (const QChar ch : current) {
                QString one(ch);
                int singlePos = 0;
                if (validator->validate(one, singlePos) == QValidator::Acceptable) {
                    filtered.append(ch);
                }
            }
            current = filtered.left(255);
        }

        if (current != toPlainText()) {
            const QTextCursor oldCursor = textCursor();
            const int oldPos = oldCursor.position();
            blockSignals(true);
            setPlainText(current);
            applyCenteredFormat();
            QTextCursor cursor = textCursor();
            cursor.setPosition(qMin(oldPos, current.size()));
            setTextCursor(cursor);
            blockSignals(false);
        }

        adjustHeightToContents();
    });
}

void FilenameEditor::setInitialText(const QString& text, const QString& fullFileName)
{
    setPlainText(text);
    applyCenteredFormat();
    m_originalText = text;
    adjustHeightToContents();

    QTextCursor cursor(document());
    QFileInfo info(fullFileName);
    const QString base = info.completeBaseName();
    if (!base.isEmpty() && text.startsWith(base)) {
        cursor.setPosition(0);
        cursor.setPosition(base.length(), QTextCursor::KeepAnchor);
    } else {
        cursor.select(QTextCursor::Document);
    }
    setTextCursor(cursor);
}

QString FilenameEditor::text() const
{
    return toPlainText().replace('\n', "").replace('\r', "");
}

int FilenameEditor::heightForText(const QString& text, const QFont& font, int width, int maxHeight)
{
    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setDefaultFont(font);

    QTextOption textOption = doc.defaultTextOption();
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    textOption.setAlignment(Qt::AlignHCenter);
    doc.setDefaultTextOption(textOption);

    doc.setTextWidth(qMax(1, width - 2));
    doc.setPlainText(text);

    const int frame = 2;
    const int docHeight = qCeil(doc.documentLayout()->documentSize().height());
    const int desiredHeight = docHeight + frame + 2;
    const int minHeight = QFontMetrics(font).lineSpacing() + frame + 2;
    if (maxHeight > 0) {
        return qMax(minHeight, qMin(maxHeight, desiredHeight));
    }
    return qMax(minHeight, desiredHeight);
}

void FilenameEditor::adjustHeightToContents(int maxHeight)
{
    document()->setTextWidth(qMax(1, viewport()->width()));
    resize(width(), heightForText(toPlainText(), font(), width(), maxHeight));
}

void FilenameEditor::applyCenteredFormat()
{
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignHCenter);
    cursor.mergeBlockFormat(format);
    setTextCursor(cursor);
}

void FilenameEditor::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        !(event->modifiers() & Qt::ShiftModifier)) {
        emit commitRequested();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        emit cancelRequested();
        event->accept();
        return;
    }

    QTextEdit::keyPressEvent(event);
}

void FilenameEditor::focusOutEvent(QFocusEvent* event)
{
    QTextEdit::focusOutEvent(event);
    emit commitRequested();
}
