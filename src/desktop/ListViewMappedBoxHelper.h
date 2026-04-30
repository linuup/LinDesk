#pragma once

#include <QString>
#include <QStringList>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QPointer>
#include <QObject>
#include <QThread>
#include <QTimer>

#include "desktop/ListView.h"
#include "box/LinBox.h"
#include "box/BoxManager.h"
#include "services/DesktopItemLoader.h"
#include "monitor/LinDirectoryMonitor.h"

class LinBox;
class ListView;

class ListViewMappedBoxHelper
{
public:
    static inline bool mappedEntryHasHiddenAttribute(const QString& path)
    {
        if (path.isEmpty()) {
            return false;
        }

        const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
        const DWORD attributes = GetFileAttributesW(nativePath.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN);
    }

    static inline QStringList mappedFolderEntryPaths(const QString& folderPath, bool showHiddenFiles = true)
    {
        const QString normalizedFolderPath = QDir::toNativeSeparators(folderPath);
        if (normalizedFolderPath.isEmpty()) {
            return {};
        }

        QDir dir(normalizedFolderPath);
        if (!dir.exists()) {
            return {};
        }

        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

        QStringList paths;
        const QSet<QString> blockedFolderNames = {
            QStringLiteral("$RECYCLE.BIN"),
            QStringLiteral("System Volume Information"),
            QStringLiteral("desktop.ini")
        };
        for (const QFileInfo& entry : entries) {
            if (blockedFolderNames.contains(entry.fileName())) {
                continue;
            }
            if (!showHiddenFiles && mappedEntryHasHiddenAttribute(entry.absoluteFilePath())) {
                continue;
            }
            paths.append(QDir::toNativeSeparators(entry.absoluteFilePath()));
        }
        return paths;
    }

    static inline void clearMappedFolderItemsForBox(ListView& view, LinBox* box)
    {
        if (!box) {
            return;
        }

        const QStringList previousPaths = view.m_mappedBoxFilePaths.value(box->boxId());
        if (previousPaths.isEmpty()) {
            return;
        }

        const QVector<DesktopRootItemState> currentStates = box->itemStates();
        for (const DesktopRootItemState& state : currentStates) {
            const QString normalizedPath = QDir::toNativeSeparators(state.model.filePath);
            if (!previousPaths.contains(normalizedPath)) {
                continue;
            }

            view.m_selectedBoxPaths.removeOne(normalizedPath);
            if (view.m_focusedBoxPath == normalizedPath) {
                view.m_focusedBoxPath.clear();
            }
            if (view.m_anchorBoxPath == normalizedPath) {
                view.m_anchorBoxPath.clear();
            }
            box->removeItemState(normalizedPath, false);
        }

        view.m_mappedBoxFilePaths.remove(box->boxId());
    }

    static inline void loadMappedFolderItemsForBox(ListView& view, LinBox* box)
    {
        if (!box || !box->isFolderMapped()) {
            return;
        }

        static quint64 s_nextMappedLoadRequestId = 1;
        box->setMappedFolderPath(box->mappedFolderPath());
        clearMappedFolderItemsForBox(view, box);

        const QFileInfo mappedInfo(box->mappedFolderPath());
        if (!mappedInfo.exists() || !mappedInfo.isDir() || !mappedInfo.isReadable()) {
            if (LinDirectoryMonitor* oldMonitor = box->mappedFolderMonitor()) {
                oldMonitor->stopListening();
                box->setMappedFolderMonitor(nullptr);
            }
            view.m_mappedBoxFilePaths.remove(box->boxId());
            box->refreshLayout();
            box->raise();
            return;
        }

        if (LinDirectoryMonitor* oldMonitor = box->mappedFolderMonitor()) {
            oldMonitor->stopListening();
        }

        auto* monitor = new LinDirectoryMonitor(box->mappedFolderPath(), false, box);
        QObject::connect(monitor, &LinDirectoryMonitor::fileSystemChanged, &view, [&view, box](const FileSystemEventInfo&) {
            if (!box) {
                return;
            }
            ListViewMappedBoxHelper::loadMappedFolderItemsForBox(view, box);
            box->refreshLayout();
            box->raise();
        }, Qt::UniqueConnection);
        monitor->startListening();
        box->setMappedFolderMonitor(monitor);

        const QString boxId = box->boxId();
        const QString mappedFolderPath = box->mappedFolderPath();
        const bool showHiddenFiles = view.m_showHiddenFiles;
        const quint64 requestId = s_nextMappedLoadRequestId++;
        view.m_mappedBoxLoadRequestIds[boxId] = requestId;

        QPointer<ListView> viewPtr(&view);
        QPointer<LinBox> boxPtr(box);

        QThread* workerThread = QThread::create([mappedFolderPath, showHiddenFiles, viewPtr, boxPtr, boxId, requestId]() {
            const QStringList paths = mappedFolderEntryPaths(mappedFolderPath, showHiddenFiles);
            QMetaObject::invokeMethod(
                viewPtr.data(),
                [viewPtr, boxPtr, boxId, requestId, paths]() {
                    if (!viewPtr || !boxPtr) {
                        return;
                    }
                    if (viewPtr->m_mappedBoxLoadRequestIds.value(boxId) != requestId) {
                        return;
                    }
                    if (!boxPtr->isFolderMapped()) {
                        return;
                    }

                    viewPtr->m_mappedBoxFilePaths[boxId] = paths;
                    if (paths.isEmpty()) {
                        boxPtr->refreshLayout();
                        boxPtr->raise();
                        return;
                    }

                    DesktopItemInitContext context = viewPtr->currentDesktopItemInitContext();
                    const QSize baseSize(viewPtr->m_iconWidth, viewPtr->m_iconHeight);
                    auto pathsPtr = std::make_shared<QStringList>(paths);
                    auto nextIndex = std::make_shared<int>(0);
                    auto processChunk = std::make_shared<std::function<void()>>();

                    *processChunk = [viewPtr, boxPtr, boxId, requestId, context, baseSize, pathsPtr, nextIndex, processChunk]() mutable {
                        if (!viewPtr || !boxPtr) {
                            return;
                        }
                        if (viewPtr->m_mappedBoxLoadRequestIds.value(boxId) != requestId) {
                            return;
                        }

                        const int chunkSize = 24;
                        const int end = qMin(*nextIndex + chunkSize, pathsPtr->size());
                        for (int i = *nextIndex; i < end; ++i) {
                            const QString& path = pathsPtr->at(i);
                            DesktopRootItemState state;
                            initDesktopRootItemModel(state.model, DesktopItemLoader().loadItem(path), context);
                            state.model.layoutMode = DesktopLayoutMode::IconMode;
                            state.model.baseSize = baseSize;
                            state.model.effectiveSize = state.model.baseSize;
                            state.model.boxId = boxId;
                            state.model.visible = true;
                            state.model.position = QPoint(0, 0);
                            boxPtr->addItemState(state, false);
                        }
                        *nextIndex = end;

                        boxPtr->refreshLayout();
                        boxPtr->raise();

                        if (*nextIndex < pathsPtr->size()) {
                            QTimer::singleShot(0, boxPtr.data(), [processChunk]() {
                                (*processChunk)();
                            });
                        }
                    };

                    (*processChunk)();
                },
                Qt::QueuedConnection);
        });
        QObject::connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
        workerThread->start();
    }

    static inline LinBox* findMappedFolderBox(const ListView& view, const QString& folderPath, const LinBox* excludeBox = nullptr)
    {
        Q_UNUSED(view);
        auto normalizeMappedFolderPathForLookupHelper = [](const QString& path) {
            QString normalized = QDir::toNativeSeparators(QDir(path).absolutePath());
            if (normalized.length() == 2 && normalized[1] == ':') {
                normalized += '\\';
            } else if (normalized.length() > 3) {
                while (normalized.endsWith('\\') || normalized.endsWith('/')) {
                    normalized.chop(1);
                }
            }
            return normalized;
        };

        const QString normalizedTarget = normalizeMappedFolderPathForLookupHelper(folderPath);
        if (normalizedTarget.isEmpty()) {
            return nullptr;
        }

        for (const QPointer<LinBox>& boxPtr : BoxManager::instance()->allBoxes()) {
            LinBox* box = boxPtr.data();
            if (!box || box == excludeBox || !box->isFolderMapped()) {
                continue;
            }

            const QString mappedPath = normalizeMappedFolderPathForLookupHelper(box->mappedFolderPath());
            if (QString::compare(mappedPath, normalizedTarget, Qt::CaseInsensitive) == 0) {
                return box;
            }
        }

        return nullptr;
    }
};
