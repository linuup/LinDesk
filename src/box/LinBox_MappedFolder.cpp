#include "box/LinBox_Internal.h"

// ============================================================================
// LinBox Mapped Folder Implementation
// ============================================================================

void LinBox::refreshMappedFolderState()
{
    if (m_mappedFolderPath.isEmpty()) {
        m_mappedFolderState = MappedFolderState::Normal;
        return;
    }

    const QFileInfo info(m_mappedFolderPath);
    if (!info.exists() || !info.isDir()) {
        m_mappedFolderState = MappedFolderState::Missing;
        return;
    }

    if (!info.isReadable()) {
        m_mappedFolderState = MappedFolderState::NoAccess;
        return;
    }

    m_mappedFolderState = MappedFolderState::Normal;
}

QString LinBox::displayTitleText() const
{
    if (!isFolderMapped()) {
        return m_title;
    }
    const QString mappedText = mappedFolderDisplayText();
    if (m_mappedFolderState == MappedFolderState::Missing) {
        return tr("[Missing] %1").arg(mappedText);
    }
    if (m_mappedFolderState == MappedFolderState::NoAccess) {
        return tr("[No Access] %1").arg(mappedText);
    }
    return mappedText;
}

void LinBox::setMappedFolderPath(const QString& path)
{
    m_mappedFolderPath = normalizeMappedFolderPath(path);
    refreshMappedFolderState();
    if (m_titleEdit) {
        m_titleEdit->setText(displayTitleText());
    }
    if (m_titleBar) {
        m_titleBar->update();
    }
    update();
    if (m_contentArea) {
        m_contentArea->update();
    }
    emit geometryChanged(this);
}

void LinBox::clearMappedFolderPath()
{
    m_mappedFolderPath.clear();
    m_mappedFolderMonitor.reset();
    refreshMappedFolderState();
    if (m_titleEdit) {
        m_titleEdit->setText(m_title);
    }
    if (m_titleBar) {
        m_titleBar->update();
    }
    update();
    if (m_contentArea) {
        m_contentArea->update();
    }
    emit geometryChanged(this);
}

void LinBox::setMappedFolderMonitor(LinDirectoryMonitor* monitor)
{
    m_mappedFolderMonitor.reset(monitor);
}

void LinBox::setMappedFolderLoading(bool loading)
{
    if (m_isMappedFolderLoading == loading) {
        return;
    }
    m_isMappedFolderLoading = loading;
    if (m_titleEdit) {
        m_titleEdit->setText(displayTitleText());
    }
    if (m_titleBar) {
        m_titleBar->update();
    }
    update();
}


QString LinBox::mappedFolderDisplayText() const
{
    if (!m_title.trimmed().isEmpty()) {
        return m_title.trimmed();
    }
    return QDir::toNativeSeparators(m_mappedFolderPath);
}

QString LinBox::sanitizedTitleText(const QString& title) const
{
    QString normalized = title;
    normalized.replace('\n', ' ');
    normalized.replace('\r', ' ');
    normalized = normalized.simplified();
    if (normalized.length() > m_maxCustomTitleLength) {
        normalized = normalized.left(m_maxCustomTitleLength);
    }
    return normalized;
}

QRect LinBox::mappedTitleIconRect() const
{
    if (!isFolderMapped() || !m_titleBar) {
        return QRect();
    }

    const int iconSize = scaledPx(this, 20);
    const int iconY = (m_titleBar->height() - iconSize) / 2;
    const QFont titleFont = AppFonts::uiFont(9, QFont::Bold);
    const QFontMetrics fm(titleFont);
    const QString title = displayTitleText();
    const int textWidth = fm.horizontalAdvance(title);
    const int totalWidth = iconSize + scaledPx(this, 6) + textWidth;
    const int minTitleLeft = scaledPx(this, kBaseTitleLayoutLeft + kBaseTitleButtonSize + 1);
    int startX = (m_titleBar->width() - totalWidth) / 2;
    startX = qMax(minTitleLeft, startX);
    return QRect(startX, iconY, iconSize, iconSize);
}

QRect LinBox::mappedTitleTextRect() const
{
    if (!m_titleBar) {
        return QRect();
    }

    const int titleSideInset = scaledPx(this, kBaseTitleLayoutLeft + kBaseTitleButtonSize + 1);
    QRect textRect = m_titleBar->rect().adjusted(titleSideInset, 0, -titleSideInset, 0);
    if (!isFolderMapped()) {
        return textRect;
    }

    const QRect iconRect = mappedTitleIconRect();
    if (!iconRect.isNull()) {
        textRect = m_titleBar->rect().adjusted(iconRect.right() + scaledPx(this, 7), 0, -titleSideInset, 0);
    }
    return textRect;
}

bool LinBox::openMappedFolderInExplorer()
{
    if (!isFolderMapped() || m_mappedFolderPath.isEmpty()) {
        return false;
    }

    const QFileInfo mappedInfo(m_mappedFolderPath);
    if (!mappedInfo.exists() || !mappedInfo.isDir()) {
        return false;
    }

    return QDesktopServices::openUrl(QUrl::fromLocalFile(mappedInfo.absoluteFilePath()));
}

QRect LinBox::mappedPlaceholderRect() const
{
    const bool shouldDrawMappedPlaceholder =
        isFolderMapped() &&
        !m_isCollapsed &&
        m_scrollArea &&
        m_scrollArea->isVisible() &&
        (mappedFolderState() != MappedFolderState::Normal || m_itemStates.isEmpty());
    if (!shouldDrawMappedPlaceholder) {
        return QRect();
    }

    const QString resourcePath = mappedFolderPlaceholderResourcePath(mappedFolderState(), m_itemStates.isEmpty());
    if (resourcePath.isEmpty()) {
        return QRect();
    }

    QWidget* viewport = m_scrollArea->viewport();
    const QPoint topLeft = viewport ? viewport->mapTo(const_cast<LinBox*>(this), QPoint(0, 0)) : m_scrollArea->geometry().topLeft();
    const QSize areaSize = viewport ? viewport->size() : m_scrollArea->size();
    QRect placeholderArea(topLeft, areaSize);
    const int placeholderInset = scaledPx(this, kBasePlaceholderInset);
    placeholderArea = placeholderArea.adjusted(placeholderInset, placeholderInset, -placeholderInset, -placeholderInset);
    if (placeholderArea.width() <= 0 || placeholderArea.height() <= 0) {
        return QRect();
    }

    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid()) {
        return QRect();
    }

    QSize targetSize = renderer.defaultSize();
    if (!targetSize.isValid()) {
        targetSize = QSize(scaledPx(this, 220), scaledPx(this, 220));
    }

    const int maxWidth = qMax(scaledPx(this, 96), qRound(placeholderArea.width() * 0.58));
    const int maxHeight = qMax(scaledPx(this, 96), qRound(placeholderArea.height() * 0.58));
    targetSize.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
    return QRect(
        placeholderArea.center().x() - targetSize.width() / 2,
        placeholderArea.center().y() - targetSize.height() / 2,
        targetSize.width(),
        targetSize.height());
}

bool LinBox::tryRefreshMappedFolderFromPlaceholderClick(const QPoint& boxPos)
{
    const QRect placeholderRect = mappedPlaceholderRect();
    if (!placeholderRect.contains(boxPos)) {
        return false;
    }

    if (mappedFolderState() == MappedFolderState::Normal) {
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    lv->reloadMappedFolderBox(this);
    refreshLayout();
    raise();
    update();

    const QString currentMappedPath = mappedFolderDisplayText();
    if (mappedFolderState() == MappedFolderState::Missing) {
        bool shouldRemap = false;
        FluentDialog dlg(this);
        dlg.setDialogTitle(tr("Mapped Folder Missing"));
        dlg.setMessage(tr("The mapped folder \"%1\" is still missing.\n\nDo you want to re map this box to another folder?").arg(currentMappedPath));
        dlg.addActionButton(tr("ReMap Folder"), FluentDialog::ActionType::Primary, [&shouldRemap]() {
            shouldRemap = true;
        });
        dlg.addActionButton(tr("Cancel"), FluentDialog::ActionType::Close);
        dlg.exec();
        if (shouldRemap) {
            remapMappedFolderInteractively();
        }
    } else if (mappedFolderState() == MappedFolderState::NoAccess) {
        bool shouldRemap = false;
        FluentDialog dlg(this);
        dlg.setDialogTitle(tr("Mapped Folder Unavailable"));
        dlg.setMessage(tr("The mapped folder \"%1\" is still unavailable because access is denied.\n\nDo you want to re map this box to another folder?").arg(currentMappedPath));
        dlg.addActionButton(tr("ReMap Folder"), FluentDialog::ActionType::Primary, [&shouldRemap]() {
            shouldRemap = true;
        });
        dlg.addActionButton(tr("Cancel"), FluentDialog::ActionType::Close);
        dlg.exec();
        if (shouldRemap) {
            remapMappedFolderInteractively();
        }
    }

    return true;
}

bool LinBox::remapMappedFolderInteractively()
{
    if (!isFolderMapped()) {
        return false;
    }

    ListView* lv = ListView::instance();
    const QString initialDir = QFileInfo(m_mappedFolderPath).isDir() ? m_mappedFolderPath : QDir::homePath();
    const QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Folder to Map"),
        initialDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedDir.isEmpty()) {
        return false;
    }

    const QString normalizedPath = normalizeMappedFolderPath(selectedDir);
    if (normalizedPath.isEmpty() ||
        QString::compare(normalizedPath, normalizeMappedFolderPath(m_mappedFolderPath), Qt::CaseInsensitive) == 0) {
        return false;
    }

    if (lv && lv->findMappedFolderBox(normalizedPath, this)) {
        lv->showTransientTip(tr("This folder is already on the desktop."), 2600);
        return false;
    }

    setMappedFolderPath(normalizedPath);
    if (lv) {
        lv->reloadMappedFolderBox(this);
        lv->requestSaveBoxes();
    }
    refreshLayout();
    raise();
    update();
    return true;
}

void LinBox::checkMappedFolderStateAndRefresh()
{
    if (!isFolderMapped()) {
        return;
    }

    if (m_mappedFolderState != MappedFolderState::Normal) {
        return;
    }

    const MappedFolderState previousState = m_mappedFolderState;
    refreshMappedFolderState();
    const MappedFolderState currentState = m_mappedFolderState;

    const bool stateChanged = currentState != previousState;
    const bool invalidStateWithStaleItems =
        currentState != MappedFolderState::Normal &&
        !m_itemStates.isEmpty();
    const bool recoveredWithoutMonitor =
        currentState == MappedFolderState::Normal &&
        !mappedFolderMonitor();

    if (!stateChanged && !invalidStateWithStaleItems && !recoveredWithoutMonitor) {
        return;
    }

    if (ListView* lv = ListView::instance()) {
        lv->reloadMappedFolderBox(this);
        refreshLayout();
        raise();
        update();
    } else {
        refreshLayout();
        update();
    }
}

