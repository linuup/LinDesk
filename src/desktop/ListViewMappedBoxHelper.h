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
#include <QDateTime>

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

    static inline void syncBoxVisibility(ListView* view, LinBox* box, bool raiseIfVisible = false)
    {
        if (!box) {
            return;
        }
        const bool show = view ? view->desktopIconsVisible() : true;
        if (show) {
            if (!box->isVisible()) {
                box->show();
            }
            if (raiseIfVisible) {
                box->raise();
            }
        } else {
            if (box->isVisible()) {
                box->hide();
            }
        }
    }

    static inline void loadMappedFolderItemsForBox(ListView& view, LinBox* box)
    {
        if (!box || !box->isFolderMapped()) {
            return;
        }

        static quint64 s_nextMappedLoadRequestId = 1;
        box->setMappedFolderPath(box->mappedFolderPath());
        box->setMappedFolderLoading(true);

        const QFileInfo mappedInfo(box->mappedFolderPath());
        if (!mappedInfo.exists() || !mappedInfo.isDir() || !mappedInfo.isReadable()) {
            if (LinDirectoryMonitor* oldMonitor = box->mappedFolderMonitor()) {
                oldMonitor->stopListening();
                box->setMappedFolderMonitor(nullptr);
            }
            clearMappedFolderItemsForBox(view, box);
            view.m_mappedBoxFilePaths.remove(box->boxId());
            box->setMappedFolderLoading(false);
            box->refreshLayout();
            syncBoxVisibility(&view, box, true);
            box->update();
            return;
        }

        LinDirectoryMonitor* monitor = box->mappedFolderMonitor();
        const QString monitoredPath = box->property("_monitoredPath").toString();
        if (!monitor || monitoredPath != box->mappedFolderPath()) {
            if (monitor) {
                monitor->stopListening();
            }
            monitor = new LinDirectoryMonitor(box->mappedFolderPath(), false, box);
            box->setProperty("_monitoredPath", box->mappedFolderPath());
            QPointer<ListView> monitorViewPtr(&view);
            QPointer<LinBox> monitorBoxPtr(box);
            QObject::connect(monitor, &LinDirectoryMonitor::fileSystemChanged, &view, [monitorViewPtr, monitorBoxPtr](const FileSystemEventInfo&) {
                if (!monitorViewPtr || !monitorBoxPtr) {
                    return;
                }
                if (monitorBoxPtr->isMappedFolderLoading()) {
                    return;
                }

                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const qint64 lastReload = monitorBoxPtr->property("_lastMappedReloadTime").toLongLong();
                if (now - lastReload < 2000) {
                    return;
                }

                QTimer* timer = monitorBoxPtr->findChild<QTimer*>("_mappedReloadTimer");
                if (!timer) {
                    timer = new QTimer(monitorBoxPtr.data());
                    timer->setObjectName("_mappedReloadTimer");
                    timer->setSingleShot(true);
                    QObject::connect(timer, &QTimer::timeout, monitorBoxPtr.data(), [monitorViewPtr, monitorBoxPtr]() {
                        if (monitorViewPtr && monitorBoxPtr && !monitorBoxPtr->isMappedFolderLoading()) {
                            monitorBoxPtr->setProperty("_lastMappedReloadTime", QDateTime::currentMSecsSinceEpoch());
                            ListViewMappedBoxHelper::loadMappedFolderItemsForBox(*monitorViewPtr, monitorBoxPtr.data());
                        }
                    });
                }
                timer->start(800);
            }, Qt::UniqueConnection);
            monitor->startListening();
            box->setMappedFolderMonitor(monitor);
        }

        const QString boxId = box->boxId();
        const QString mappedFolderPath = box->mappedFolderPath();
        const bool showHiddenFiles = view.m_showHiddenFiles;
        const quint64 requestId = s_nextMappedLoadRequestId++;
        view.m_mappedBoxLoadRequestIds[boxId] = requestId;

        QPointer<ListView> viewPtr(&view);
        QPointer<LinBox> boxPtr(box);

        QThread* workerThread = QThread::create([mappedFolderPath, showHiddenFiles, viewPtr, boxPtr, boxId, requestId]() {
            const QStringList paths = mappedFolderEntryPaths(mappedFolderPath, showHiddenFiles);
            if (!viewPtr || !boxPtr) {
                return;
            }
            QMetaObject::invokeMethod(
                viewPtr.data(),
                [viewPtr, boxPtr, boxId, requestId, paths]() {
                    if (!viewPtr || !boxPtr) {
                        return;
                    }
                    if (viewPtr->m_mappedBoxLoadRequestIds.value(boxId) != requestId) {
                        syncBoxVisibility(viewPtr.data(), boxPtr.data(), false);
                        boxPtr->update();
                        return;
                    }
                    if (!boxPtr->isFolderMapped()) {
                        syncBoxVisibility(viewPtr.data(), boxPtr.data(), false);
                        boxPtr->update();
                        return;
                    }

                    if (paths.isEmpty()) {
                        clearMappedFolderItemsForBox(*viewPtr, boxPtr.data());
                        viewPtr->m_mappedBoxFilePaths[boxId] = paths;
                        boxPtr->setMappedFolderLoading(false);
                        boxPtr->refreshLayout();
                        syncBoxVisibility(viewPtr.data(), boxPtr.data(), true);
                        boxPtr->update();
                        if (viewPtr) {
                            viewPtr->update(boxPtr->geometry().adjusted(-16, -16, 16, 16));
                        }
                        if (boxPtr->glassEffect()) {
                            boxPtr->updateBlurCache();
                        }
                        return;
                    }

                    DesktopItemInitContext context = viewPtr->currentDesktopItemInitContext();
                    const QSize baseSize(viewPtr->m_iconWidth, viewPtr->m_iconHeight);
                    auto pathsPtr = std::make_shared<QStringList>(paths);
                    auto nextIndex = std::make_shared<int>(0);
                    auto accumulatedStates = std::make_shared<QVector<DesktopRootItemState>>();
                    auto processChunk = std::make_shared<std::function<void()>>();
                    auto loader = std::make_shared<DesktopItemLoader>();

                    *processChunk = [viewPtr, boxPtr, boxId, requestId, context, baseSize, pathsPtr, nextIndex, accumulatedStates, processChunk, loader]() mutable {
                        if (!viewPtr || !boxPtr) {
                            return;
                        }
                        if (viewPtr->m_mappedBoxLoadRequestIds.value(boxId) != requestId) {
                            syncBoxVisibility(viewPtr.data(), boxPtr.data(), false);
                            boxPtr->update();
                            return;
                        }
                        if (!boxPtr->isFolderMapped()) {
                            syncBoxVisibility(viewPtr.data(), boxPtr.data(), false);
                            boxPtr->update();
                            return;
                        }

                        const int chunkSize = 64;
                        const int end = qMin(*nextIndex + chunkSize, pathsPtr->size());
                        for (int i = *nextIndex; i < end; ++i) {
                            const QString& path = pathsPtr->at(i);
                            DesktopRootItemState state;
                            initDesktopRootItemModel(state.model, loader->loadItem(path), context);
                            state.model.layoutMode = DesktopLayoutMode::IconMode;
                            state.model.baseSize = baseSize;
                            state.model.effectiveSize = state.model.baseSize;
                            state.model.boxId = boxId;
                            state.model.visible = true;
                            state.model.position = QPoint(0, 0);
                            accumulatedStates->append(state);
                        }
                        *nextIndex = end;

                        if (*nextIndex < pathsPtr->size()) {
                            LinBox* timerContext = boxPtr.data();
                            if (!timerContext) {
                                return;
                            }
                            QTimer::singleShot(0, timerContext, [processChunk]() {
                                if (processChunk && *processChunk) {
                                    (*processChunk)();
                                }
                            });
                        } else {
                            clearMappedFolderItemsForBox(*viewPtr, boxPtr.data());
                            for (const DesktopRootItemState& state : *accumulatedStates) {
                                boxPtr->addItemState(state, false);
                            }
                            viewPtr->m_mappedBoxFilePaths[boxId] = *pathsPtr;

                            boxPtr->setMappedFolderLoading(false);
                            boxPtr->refreshLayout();
                            syncBoxVisibility(viewPtr.data(), boxPtr.data(), true);
                            boxPtr->update();
                            if (viewPtr) {
                                viewPtr->update(boxPtr->geometry().adjusted(-16, -16, 16, 16));
                            }
                            if (boxPtr->glassEffect()) {
                                boxPtr->updateBlurCache();
                            }
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
