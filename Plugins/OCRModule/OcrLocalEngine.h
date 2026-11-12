#pragma once

#include <QObject>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QEventLoop>
#include <QCoreApplication>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QVector>
#include <QRectF>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <queue>

#include <onnxruntime_cxx_api.h>
#include "OcrResultDialog.h"

class OcrLocalEngine;

/**
 * @brief Dedicated Native C++ ONNX Runtime worker backend living on a persistent QThread.
 * Executes Det, connected component analysis, unclip geometry, and Rec + CTC decode directly in memory.
 * Zero external Python processes, zero disk file I/O, zero cross-process piping overhead.
 */
class OcrWorkerBackend : public QObject {
public:
    explicit OcrWorkerBackend(QObject* parent = nullptr)
        : QObject(parent), m_env(ORT_LOGGING_LEVEL_WARNING, "LinDeskNativeOcr") {}

    ~OcrWorkerBackend() override {
        unloadModels();
    }

    void configure(const QString& modelsDir, int threads, bool useCls = false) {
        const int clampedThreads = qBound(1, threads, 16);
        if (m_modelsDir != modelsDir || m_numThreads != clampedThreads || m_useCls != useCls) {
            m_modelsDir = modelsDir;
            m_numThreads = clampedThreads;
            m_useCls = useCls;
            unloadModels();
        }
    }

    void unloadModels() {
        m_detSession.reset();
        m_clsSession.reset();
        m_recSession.reset();
        m_dictChars.clear();
        m_modelsLoaded = false;
    }

    void performRecognitionSync(const QImage& image, void* resultPtr);

private:
    bool ensureModelsLoaded(const QString& modelsDir, QString& outError);

    Ort::Env m_env;
    std::unique_ptr<Ort::Session> m_detSession;
    std::unique_ptr<Ort::Session> m_clsSession;
    std::unique_ptr<Ort::Session> m_recSession;
    std::vector<QString> m_dictChars;
    QString m_modelsDir;
    int m_numThreads = 4;
    bool m_useCls = false;
    bool m_modelsLoaded = false;
};

class OcrLocalEngine : public QObject {
    Q_OBJECT
public:
    static constexpr int MinThreads = 1;
    static constexpr int MaxThreads = 16;
    static constexpr int DefaultThreads = 4;

    struct LocalResult {
        QString text;
        QVector<OcrTextBox> boxes;
        QString errorMessage;
        bool ok = false;
    };

    explicit OcrLocalEngine(QObject* parent = nullptr) : QObject(parent) {
        m_backend = new OcrWorkerBackend();
        m_backend->moveToThread(&m_workerThread);
        connect(&m_workerThread, &QThread::finished, m_backend, &QObject::deleteLater);
        m_workerThread.start();
    }

    ~OcrLocalEngine() override {
        if (m_workerThread.isRunning()) {
            QMetaObject::invokeMethod(m_backend, [backend = m_backend]() {
                backend->unloadModels();
            }, Qt::BlockingQueuedConnection);
            m_workerThread.quit();
            m_workerThread.wait(3000);
        }
    }

    static QString findWorkerExecutable() {
        return QStringLiteral("native");
    }

    static QString defaultModelsDir() {
        QStringList candidates;
        const QString appDir = QCoreApplication::applicationDirPath();
        candidates << appDir + QStringLiteral("/plugins/models")
                   << appDir + QStringLiteral("/models")
                   << appDir + QStringLiteral("/resources/models")
                   << QStringLiteral("D:/Desktop/LinDesk/Plugins/OCRModule/models")
                   << QStringLiteral("D:/Desktop/LinDesk/build/Release/plugins/models")
                   << QStringLiteral("D:/Desktop/LinDesk/build/Debug/plugins/models");

        for (const QString& path : candidates) {
            if (isModelDirValid(path)) {
                return QDir::toNativeSeparators(path);
            }
        }
        return QString();
    }

    static bool isModelDirValid(const QString& dirPath) {
        if (dirPath.isEmpty()) return false;
        QDir dir(dirPath);
        if (!dir.exists()) return false;

        bool hasDet = dir.exists(QStringLiteral("PP-OCRv6_det_small.onnx")) ||
                      dir.exists(QStringLiteral("det.onnx")) ||
                      dir.exists(QStringLiteral("ch_PP-OCRv4_det_infer.onnx")) ||
                      dir.exists(QStringLiteral("ch_PP-OCRv3_det_infer.onnx"));

        bool hasRec = dir.exists(QStringLiteral("PP-OCRv6_rec_small.onnx")) ||
                      dir.exists(QStringLiteral("rec.onnx")) ||
                      dir.exists(QStringLiteral("ch_PP-OCRv4_rec_infer.onnx")) ||
                      dir.exists(QStringLiteral("ch_PP-OCRv3_rec_infer.onnx"));

        bool hasDict = dir.exists(QStringLiteral("ppocrv6_dict.txt")) ||
                       dir.exists(QStringLiteral("dict.txt")) ||
                       dir.exists(QStringLiteral("ppocr_keys_v1.txt"));

        return hasDet && hasRec && hasDict;
    }

    void configure(const QString& modelsDir, int numThreads = DefaultThreads, bool useCls = false) {
        m_modelsDir = modelsDir;
        m_numThreads = qBound(MinThreads, numThreads, MaxThreads);
        m_useCls = useCls;
        if (m_workerThread.isRunning()) {
            QMetaObject::invokeMethod(m_backend, [backend = m_backend, modelsDir, threads = m_numThreads, useCls]() {
                backend->configure(modelsDir, threads, useCls);
            });
        }
    }

    void recognizeAsync(const QImage& source, std::function<void(const LocalResult&)> onFinished) {
        if (source.isNull()) {
            LocalResult err;
            err.ok = false;
            err.errorMessage = QStringLiteral("Recognition source image is null.");
            if (onFinished) onFinished(err);
            return;
        }

        const QImage copyImage = source.copy();
        if (m_workerThread.isRunning()) {
            QMetaObject::invokeMethod(m_backend, [this, backend = m_backend, copyImage, onFinished]() {
                LocalResult res;
                backend->performRecognitionSync(copyImage, &res);
                QMetaObject::invokeMethod(this, [onFinished, res]() {
                    if (onFinished) {
                        onFinished(res);
                    }
                });
            });
        } else {
            LocalResult err;
            err.ok = false;
            err.errorMessage = QStringLiteral("OCR worker thread is not running.");
            if (onFinished) onFinished(err);
        }
    }

    LocalResult recognizeSync(const QImage& source) {
        if (source.isNull()) {
            LocalResult err;
            err.ok = false;
            err.errorMessage = QStringLiteral("Recognition source image is null.");
            return err;
        }

        LocalResult result;
        QEventLoop loop;
        recognizeAsync(source, [&loop, &result](const LocalResult& r) {
            result = r;
            loop.quit();
        });
        loop.exec();
        return result;
    }

private:
    QThread m_workerThread;
    OcrWorkerBackend* m_backend = nullptr;
    QString m_modelsDir;
    int m_numThreads = 4;
    bool m_useCls = false;
};

// =========================================================================
// OcrWorkerBackend Implementation
// =========================================================================

inline bool OcrWorkerBackend::ensureModelsLoaded(const QString& modelsDir, QString& outError) {
    if (m_modelsLoaded && m_detSession && m_recSession && !m_dictChars.empty()) {
        return true;
    }

    QDir dir(modelsDir);
    if (!dir.exists()) {
        outError = QStringLiteral("Models directory does not exist: ") + modelsDir;
        return false;
    }

    QString detPath;
    const QStringList detCandidates = {
        QStringLiteral("PP-OCRv6_det_small.onnx"),
        QStringLiteral("det.onnx"),
        QStringLiteral("ch_PP-OCRv4_det_infer.onnx"),
        QStringLiteral("ch_PP-OCRv3_det_infer.onnx")
    };
    for (const auto& f : detCandidates) {
        if (dir.exists(f)) { detPath = dir.filePath(f); break; }
    }

    QString recPath;
    const QStringList recCandidates = {
        QStringLiteral("PP-OCRv6_rec_small.onnx"),
        QStringLiteral("rec.onnx"),
        QStringLiteral("ch_PP-OCRv4_rec_infer.onnx"),
        QStringLiteral("ch_PP-OCRv3_rec_infer.onnx")
    };
    for (const auto& f : recCandidates) {
        if (dir.exists(f)) { recPath = dir.filePath(f); break; }
    }

    QString dictPath;
    const QStringList dictCandidates = {
        QStringLiteral("ppocrv6_dict.txt"),
        QStringLiteral("dict.txt"),
        QStringLiteral("ppocr_keys_v1.txt")
    };
    for (const auto& f : dictCandidates) {
        if (dir.exists(f)) { dictPath = dir.filePath(f); break; }
    }

    if (detPath.isEmpty() || recPath.isEmpty() || dictPath.isEmpty()) {
        outError = QStringLiteral("Required model files (det, rec, dict) not found in: ") + modelsDir;
        return false;
    }

    try {
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(m_numThreads);
        sessionOptions.SetInterOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        const std::wstring detPathW = detPath.toStdWString();
        const std::wstring recPathW = recPath.toStdWString();

        m_detSession = std::make_unique<Ort::Session>(m_env, detPathW.c_str(), sessionOptions);
        m_recSession = std::make_unique<Ort::Session>(m_env, recPathW.c_str(), sessionOptions);

        if (m_useCls) {
            QString clsPath;
            const QStringList clsCandidates = {
                QStringLiteral("cls.onnx"),
                QStringLiteral("ch_ppocr_mobile_v2.0_cls_infer.onnx")
            };
            for (const auto& f : clsCandidates) {
                if (dir.exists(f)) { clsPath = dir.filePath(f); break; }
            }
            if (!clsPath.isEmpty()) {
                const std::wstring clsPathW = clsPath.toStdWString();
                m_clsSession = std::make_unique<Ort::Session>(m_env, clsPathW.c_str(), sessionOptions);
            }
        }

        // Load dictionary:
        // PP-OCR CTC blank is index 0; followed by dictionary lines; trailing space is index N+1.
        m_dictChars.clear();
        m_dictChars.push_back(QStringLiteral("blank"));

        QFile dictFile(dictPath);
        if (dictFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&dictFile);
            stream.setCodec("UTF-8");
            while (!stream.atEnd()) {
                QString line = stream.readLine();
                if (!line.isEmpty()) {
                    m_dictChars.push_back(line);
                }
            }
            dictFile.close();
        }
        m_dictChars.push_back(QStringLiteral(" "));

        m_modelsLoaded = true;
        return true;
    } catch (const Ort::Exception& e) {
        outError = QStringLiteral("ONNX Runtime error: ") + QString::fromUtf8(e.what());
        unloadModels();
        return false;
    } catch (const std::exception& e) {
        outError = QStringLiteral("Exception loading models: ") + QString::fromUtf8(e.what());
        unloadModels();
        return false;
    }
}

inline void OcrWorkerBackend::performRecognitionSync(const QImage& sourceImage, void* resultPtr) {
    auto* result = static_cast<OcrLocalEngine::LocalResult*>(resultPtr);
    if (sourceImage.isNull()) {
        result->ok = false;
        result->errorMessage = QStringLiteral("Source image is null.");
        return;
    }

    QString effectiveModelsDir = m_modelsDir;
    if (effectiveModelsDir.isEmpty() || !OcrLocalEngine::isModelDirValid(effectiveModelsDir)) {
        effectiveModelsDir = OcrLocalEngine::defaultModelsDir();
    }

    QString loadErr;
    if (!ensureModelsLoaded(effectiveModelsDir, loadErr)) {
        result->ok = false;
        result->errorMessage = loadErr;
        return;
    }

    try {
        const int origW = sourceImage.width();
        const int origH = sourceImage.height();
        if (origW <= 0 || origH <= 0) {
            result->ok = false;
            result->errorMessage = QStringLiteral("Invalid image dimensions.");
            return;
        }

        // 1. Det Preprocessing:
        // Resize so width and height are multiples of 32
        // Limit max side to 960 for optimal balance of speed and detection recall
        float scale = 1.0f;
        const int maxSide = std::max(origW, origH);
        if (maxSide > 960) {
            scale = 960.0f / (float)maxSide;
        }
        const int targetW = std::max(32, (int)std::round((origW * scale) / 32.0f) * 32);
        const int targetH = std::max(32, (int)std::round((origH * scale) / 32.0f) * 32);

        const QImage detImg = sourceImage.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const QImage rgbDet = detImg.convertToFormat(QImage::Format_RGB888);

        // PP-OCR uses BGR order and normalization: (pixel / 255.0 - 0.5) / 0.5
        const int hw = targetW * targetH;
        std::vector<float> detTensorData(3 * hw);
        float* pB = detTensorData.data() + 0 * hw;
        float* pG = detTensorData.data() + 1 * hw;
        float* pR = detTensorData.data() + 2 * hw;

        for (int y = 0; y < targetH; ++y) {
            const uchar* line = rgbDet.constScanLine(y);
            const int rowOffset = y * targetW;
            for (int x = 0; x < targetW; ++x) {
                const float rVal = (line[x * 3 + 0] / 255.0f - 0.5f) / 0.5f;
                const float gVal = (line[x * 3 + 1] / 255.0f - 0.5f) / 0.5f;
                const float bVal = (line[x * 3 + 2] / 255.0f - 0.5f) / 0.5f;

                pB[rowOffset + x] = bVal;
                pG[rowOffset + x] = gVal;
                pR[rowOffset + x] = rVal;
            }
        }

        // 2. Det Inference:
        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const int64_t detInputShape[] = { 1, 3, targetH, targetW };
        Ort::Value detInputTensor = Ort::Value::CreateTensor<float>(
            memInfo, detTensorData.data(), detTensorData.size(), detInputShape, 4);

        const char* detInputNames[] = { "x" };
        const char* detOutputNames[] = { "fetch_name_0" };
        auto detOutputs = m_detSession->Run(Ort::RunOptions{nullptr}, detInputNames, &detInputTensor, 1, detOutputNames, 1);

        const float* probMap = detOutputs[0].GetTensorData<float>();

        // 3. DB Post-Processing (Connected Components Labeling & Unclip):
        std::vector<uint8_t> visited(hw, 0);
        struct DetectedBox {
            QRect rect;
            float score = 0.0f;
        };
        std::vector<DetectedBox> candidateBoxes;

        // 8-neighbor directions
        const int dx[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        const int dy[] = { -1, -1, -1, 0, 0, 1, 1, 1 };

        for (int y = 0; y < targetH; ++y) {
            for (int x = 0; x < targetW; ++x) {
                const int idx = y * targetW + x;
                if (visited[idx] || probMap[idx] <= 0.3f) {
                    continue;
                }

                // BFS flood-fill
                std::queue<std::pair<int, int>> q;
                q.push({x, y});
                visited[idx] = 1;

                int minX = x, maxX = x;
                int minY = y, maxY = y;
                int count = 0;
                float probSum = 0.0f;

                while (!q.empty()) {
                    const auto pt = q.front();
                    q.pop();
                    const int curX = pt.first;
                    const int curY = pt.second;
                    const int curIdx = curY * targetW + curX;

                    minX = std::min(minX, curX);
                    maxX = std::max(maxX, curX);
                    minY = std::min(minY, curY);
                    maxY = std::max(maxY, curY);
                    count++;
                    probSum += probMap[curIdx];

                    for (int d = 0; d < 8; ++d) {
                        const int nx = curX + dx[d];
                        const int ny = curY + dy[d];
                        if (nx >= 0 && nx < targetW && ny >= 0 && ny < targetH) {
                            const int nIdx = ny * targetW + nx;
                            if (!visited[nIdx] && probMap[nIdx] > 0.3f) {
                                visited[nIdx] = 1;
                                q.push({nx, ny});
                            }
                        }
                    }
                }

                if (count < 8) continue;
                const float avgScore = probSum / (float)count;
                if (avgScore < 0.5f) continue;

                // Unclip expansion: distance = 1.6 * (w * h) / (2 * (w + h))
                const float bw = maxX - minX + 1.0f;
                const float bh = maxY - minY + 1.0f;
                const float unclipDist = 1.6f * (bw * bh) / (2.0f * (bw + bh));

                const float ux1 = std::max(0.0f, minX - unclipDist);
                const float ux2 = std::min((float)targetW, maxX + unclipDist + 1.0f);
                const float uy1 = std::max(0.0f, minY - unclipDist);
                const float uy2 = std::min((float)targetH, maxY + unclipDist + 1.0f);

                // Map coordinates back to original image space
                const int ox1 = std::clamp((int)std::round(ux1 * origW / (float)targetW), 0, origW - 1);
                const int ox2 = std::clamp((int)std::round(ux2 * origW / (float)targetW), 0, origW - 1);
                const int oy1 = std::clamp((int)std::round(uy1 * origH / (float)targetH), 0, origH - 1);
                const int oy2 = std::clamp((int)std::round(uy2 * origH / (float)targetH), 0, origH - 1);

                const int boxW = ox2 - ox1 + 1;
                const int boxH = oy2 - oy1 + 1;
                if (boxW >= 3 && boxH >= 3) {
                    DetectedBox db;
                    db.rect = QRect(ox1, oy1, boxW, boxH);
                    db.score = avgScore;
                    candidateBoxes.push_back(db);
                }
            }
        }

        // Sort candidate boxes primarily top-to-bottom, secondarily left-to-right
        std::sort(candidateBoxes.begin(), candidateBoxes.end(), [](const DetectedBox& a, const DetectedBox& b) {
            if (std::abs(a.rect.y() - b.rect.y()) < 12) {
                return a.rect.x() < b.rect.x();
            }
            return a.rect.y() < b.rect.y();
        });

        // 4. Rec Inference on each detected text box:
        QStringList fullTextLines;
        QVector<OcrTextBox> resultBoxes;

        const char* recInputNames[] = { "x" };
        const char* recOutputNames[] = { "fetch_name_0" };

        for (const auto& candidate : candidateBoxes) {
            QImage cropImg = sourceImage.copy(candidate.rect);
            if (cropImg.isNull()) continue;

            // Optional: Direction Classifier (Text Orientation Correction)
            if (m_useCls && m_clsSession) {
                const int clsW = 192;
                const int clsH = 48;
                const QImage scaledCls = cropImg.scaled(clsW, clsH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                const QImage rgbCls = scaledCls.convertToFormat(QImage::Format_RGB888);

                const int clsHw = clsW * clsH;
                std::vector<float> clsTensorData(3 * clsHw);
                float* cB = clsTensorData.data() + 0 * clsHw;
                float* cG = clsTensorData.data() + 1 * clsHw;
                float* cR = clsTensorData.data() + 2 * clsHw;

                for (int y = 0; y < clsH; ++y) {
                    const uchar* line = rgbCls.constScanLine(y);
                    const int rowOffset = y * clsW;
                    for (int x = 0; x < clsW; ++x) {
                        cB[rowOffset + x] = (line[x * 3 + 2] / 255.0f - 0.5f) / 0.5f;
                        cG[rowOffset + x] = (line[x * 3 + 1] / 255.0f - 0.5f) / 0.5f;
                        cR[rowOffset + x] = (line[x * 3 + 0] / 255.0f - 0.5f) / 0.5f;
                    }
                }

                const int64_t clsInputShape[] = { 1, 3, clsH, clsW };
                Ort::Value clsInputTensor = Ort::Value::CreateTensor<float>(
                    memInfo, clsTensorData.data(), clsTensorData.size(), clsInputShape, 4);

                const char* clsInputNames[] = { "x" };
                const char* clsOutputNames[] = { "save_infer_model/scale_0.tmp_1" };
                auto clsOutputs = m_clsSession->Run(Ort::RunOptions{nullptr}, clsInputNames, &clsInputTensor, 1, clsOutputNames, 1);
                const float* clsScores = clsOutputs[0].GetTensorData<float>();

                const float exp0 = std::exp(clsScores[0]);
                const float exp1 = std::exp(clsScores[1]);
                const float prob180 = exp1 / (exp0 + exp1);

                if (prob180 > 0.85f) {
                    cropImg = cropImg.transformed(QTransform().rotate(180));
                }
            }

            const int cropW = cropImg.width();
            const int cropH = cropImg.height();
            if (cropW <= 0 || cropH <= 0) continue;

            // Rec target height is fixed to 48
            const int recH = 48;
            int recW = std::max(16, (int)std::round(cropW * 48.0f / (float)cropH));
            recW = std::min(recW, 2048);

            const QImage scaledCrop = cropImg.scaled(recW, recH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            const QImage rgbCrop = scaledCrop.convertToFormat(QImage::Format_RGB888);

            const int recHw = recW * recH;
            std::vector<float> recTensorData(3 * recHw);
            float* rB = recTensorData.data() + 0 * recHw;
            float* rG = recTensorData.data() + 1 * recHw;
            float* rR = recTensorData.data() + 2 * recHw;

            for (int y = 0; y < recH; ++y) {
                const uchar* line = rgbCrop.constScanLine(y);
                const int rowOffset = y * recW;
                for (int x = 0; x < recW; ++x) {
                    const float rVal = (line[x * 3 + 0] / 255.0f - 0.5f) / 0.5f;
                    const float gVal = (line[x * 3 + 1] / 255.0f - 0.5f) / 0.5f;
                    const float bVal = (line[x * 3 + 2] / 255.0f - 0.5f) / 0.5f;

                    rB[rowOffset + x] = bVal;
                    rG[rowOffset + x] = gVal;
                    rR[rowOffset + x] = rVal;
                }
            }

            const int64_t recInputShape[] = { 1, 3, recH, recW };
            Ort::Value recInputTensor = Ort::Value::CreateTensor<float>(
                memInfo, recTensorData.data(), recTensorData.size(), recInputShape, 4);

            auto recOutputs = m_recSession->Run(Ort::RunOptions{nullptr}, recInputNames, &recInputTensor, 1, recOutputNames, 1);

            const auto typeInfo = recOutputs[0].GetTensorTypeAndShapeInfo();
            const auto shape = typeInfo.GetShape();
            const int timeSteps = (int)shape[1];
            const int numClasses = (int)shape[2];
            const float* logits = recOutputs[0].GetTensorData<float>();

            // CTC Greedy Decode
            QString lineStr;
            int prevIdx = -1;
            for (int t = 0; t < timeSteps; ++t) {
                const float* stepLogits = logits + t * numClasses;
                int maxIdx = 0;
                float maxVal = stepLogits[0];
                for (int c = 1; c < numClasses; ++c) {
                    if (stepLogits[c] > maxVal) {
                        maxVal = stepLogits[c];
                        maxIdx = c;
                    }
                }

                if (maxIdx != 0 && maxIdx != prevIdx && maxIdx < (int)m_dictChars.size()) {
                    lineStr.append(m_dictChars[maxIdx]);
                }
                prevIdx = maxIdx;
            }

            lineStr = lineStr.trimmed();
            if (!lineStr.isEmpty()) {
                fullTextLines.append(lineStr);

                OcrTextBox tb;
                tb.text = lineStr;
                tb.imageRect = QRectF(candidate.rect);
                resultBoxes.append(tb);
            }
        }

        result->ok = true;
        result->text = fullTextLines.join(QStringLiteral("\n"));
        result->boxes = resultBoxes;
        result->errorMessage.clear();

    } catch (const Ort::Exception& e) {
        result->ok = false;
        result->errorMessage = QStringLiteral("Native OCR inference error: ") + QString::fromUtf8(e.what());
    } catch (const std::exception& e) {
        result->ok = false;
        result->errorMessage = QStringLiteral("Native OCR runtime error: ") + QString::fromUtf8(e.what());
    }
}
