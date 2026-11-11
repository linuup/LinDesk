#include "desktop/widgets/linwidget/WidgetManager_Internal.h"
#include "desktop/widgets/linwidget/WinMfVideoEngine.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QImageWriter>
#include <QPainter>
#include <QPointer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wrl/client.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::ComPtr;
#endif

namespace LinWidget {

#ifdef Q_OS_WIN
namespace {

struct ComAndMfScope {
    bool comInitialized = false;
    bool mfInitialized = false;

    ComAndMfScope()
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized = true;
        }
        hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (SUCCEEDED(hr)) {
            mfInitialized = true;
        }
    }

    ~ComAndMfScope()
    {
        if (mfInitialized) {
            MFShutdown();
        }
        if (comInitialized) {
            CoUninitialize();
        }
    }
};

static bool queryVideoFormatGeometry(IMFSourceReader* reader,
                                     DWORD streamIndex,
                                     UINT32* outWidth,
                                     UINT32* outHeight,
                                     LONG* outStride)
{
    if (!reader) {
        return false;
    }
    ComPtr<IMFMediaType> currentType;
    if (FAILED(reader->GetCurrentMediaType(streamIndex, &currentType)) || !currentType) {
        return false;
    }
    UINT32 w = 0;
    UINT32 h = 0;
    if (FAILED(MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0) {
        return false;
    }

    LONG stride = static_cast<LONG>(w * 4);
    UINT32 rawStride = 0;
    if (SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride))) {
        stride = static_cast<LONG>(static_cast<INT32>(rawStride));
    } else {
        LONG calcStride = 0;
        if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &calcStride))) {
            stride = calcStride;
        }
    }

    // Respect minimum display aperture if the H.264 decoder padded height (e.g. 1088 -> 1080)
    MFVideoArea aperture = {};
    UINT32 blobSize = 0;
    if (SUCCEEDED(currentType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&aperture), sizeof(aperture), &blobSize))) {
        if (aperture.Area.cx > 0 && aperture.Area.cy > 0 &&
            static_cast<UINT32>(aperture.Area.cx) <= w &&
            static_cast<UINT32>(aperture.Area.cy) <= h) {
            w = static_cast<UINT32>(aperture.Area.cx);
            h = static_cast<UINT32>(aperture.Area.cy);
        }
    }

    if (outWidth) *outWidth = w;
    if (outHeight) *outHeight = h;
    if (outStride) *outStride = stride;
    return true;
}

static ComPtr<IMFSourceReader> createVideoSourceReader(const QString& filePath,
                                                       bool enableAudio,
                                                       UINT32* outWidth,
                                                       UINT32* outHeight,
                                                       LONG* outStride,
                                                       DWORD* outVideoStreamIndex = nullptr,
                                                       DWORD* outAudioStreamIndex = nullptr,
                                                       UINT32* outAudioSampleRate = nullptr,
                                                       UINT32* outAudioChannels = nullptr,
                                                       UINT32* outAudioBits = nullptr,
                                                       bool* outAudioIsFloat = nullptr)
{
    ComPtr<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(&attrs, 2))) {
        return nullptr;
    }
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    const std::wstring wPath = QDir::toNativeSeparators(filePath).toStdWString();
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(wPath.c_str(), attrs.Get(), &reader)) || !reader) {
        return nullptr;
    }

    DWORD videoStreamIdx = (DWORD)-1;
    DWORD audioStreamIdx = (DWORD)-1;

    for (DWORD i = 0; i < 32; ++i) {
        ComPtr<IMFMediaType> nativeType;
        if (FAILED(reader->GetNativeMediaType(i, 0, &nativeType)) || !nativeType) {
            break;
        }
        GUID majorType = GUID_NULL;
        if (SUCCEEDED(nativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType))) {
            if (majorType == MFMediaType_Video && videoStreamIdx == (DWORD)-1) {
                videoStreamIdx = i;
            } else if (majorType == MFMediaType_Audio && audioStreamIdx == (DWORD)-1) {
                audioStreamIdx = i;
            }
        }
    }

    if (videoStreamIdx == (DWORD)-1) {
        videoStreamIdx = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    }

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader->SetStreamSelection(videoStreamIdx, TRUE))) {
        return nullptr;
    }

    ComPtr<IMFMediaType> videoOutType;
    if (FAILED(MFCreateMediaType(&videoOutType))) {
        return nullptr;
    }
    videoOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(videoStreamIdx, nullptr, videoOutType.Get()))) {
        return nullptr;
    }

    queryVideoFormatGeometry(reader.Get(), videoStreamIdx, outWidth, outHeight, outStride);

    if (enableAudio && audioStreamIdx != (DWORD)-1 && outAudioSampleRate && outAudioChannels && outAudioBits && outAudioIsFloat) {
        *outAudioSampleRate = 0;
        *outAudioChannels = 0;
        *outAudioBits = 0;
        *outAudioIsFloat = false;

        if (SUCCEEDED(reader->SetStreamSelection(audioStreamIdx, TRUE))) {
            ComPtr<IMFMediaType> pcmType;
            if (SUCCEEDED(MFCreateMediaType(&pcmType))) {
                pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                HRESULT hrAudio = reader->SetCurrentMediaType(audioStreamIdx, nullptr, pcmType.Get());
                if (FAILED(hrAudio)) {
                    pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
                    hrAudio = reader->SetCurrentMediaType(audioStreamIdx, nullptr, pcmType.Get());
                }
                if (SUCCEEDED(hrAudio)) {
                    ComPtr<IMFMediaType> actualAudio;
                    if (SUCCEEDED(reader->GetCurrentMediaType(audioStreamIdx, &actualAudio)) && actualAudio) {
                        GUID sub = GUID_NULL;
                        actualAudio->GetGUID(MF_MT_SUBTYPE, &sub);
                        *outAudioIsFloat = (sub == MFAudioFormat_Float);
                        *outAudioSampleRate = MFGetAttributeUINT32(actualAudio.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
                        *outAudioChannels = MFGetAttributeUINT32(actualAudio.Get(), MF_MT_AUDIO_NUM_CHANNELS, 2);
                        *outAudioBits = MFGetAttributeUINT32(actualAudio.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, *outAudioIsFloat ? 32 : 16);
                    }
                } else {
                    reader->SetStreamSelection(audioStreamIdx, FALSE);
                    audioStreamIdx = (DWORD)-1;
                }
            }
        } else {
            audioStreamIdx = (DWORD)-1;
        }
    }

    if (outVideoStreamIndex) *outVideoStreamIndex = videoStreamIdx;
    if (outAudioStreamIndex) *outAudioStreamIndex = audioStreamIdx;

    return reader;
}

static QImage copySampleToQImage(IMFSample* sample, UINT32 width, UINT32 height, LONG defaultStride)
{
    if (!sample || width == 0 || height == 0) {
        return QImage();
    }

    const size_t rowBytes = static_cast<size_t>(width) * 4;

    // 1. Try IMF2DBuffer directly from the sample's primary buffer
    ComPtr<IMFMediaBuffer> primaryBuffer;
    if (SUCCEEDED(sample->GetBufferByIndex(0, &primaryBuffer)) && primaryBuffer) {
        ComPtr<IMF2DBuffer> buf2D;
        if (SUCCEEDED(primaryBuffer.As(&buf2D)) && buf2D) {
            BYTE* scanline0 = nullptr;
            LONG pitch = 0;
            if (SUCCEEDED(buf2D->Lock2D(&scanline0, &pitch)) && scanline0) {
                QImage img(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
                for (UINT32 y = 0; y < height; ++y) {
                    const BYTE* src = scanline0 + static_cast<ptrdiff_t>(y) * pitch;
                    const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src);
                    uint32_t* dst32 = reinterpret_cast<uint32_t*>(img.scanLine(static_cast<int>(y)));
                    for (UINT32 x = 0; x < width; ++x) {
                        dst32[x] = src32[x] | 0xFF000000u;
                    }
                }
                buf2D->Unlock2D();
                return img;
            }
        }
    }

    // 2. Fall back to contiguous buffer
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) {
        return QImage();
    }

    ComPtr<IMF2DBuffer> buffer2D;
    if (SUCCEEDED(buffer.As(&buffer2D)) && buffer2D) {
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch)) && scanline0) {
            QImage img(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
            for (UINT32 y = 0; y < height; ++y) {
                const BYTE* src = scanline0 + static_cast<ptrdiff_t>(y) * pitch;
                const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src);
                uint32_t* dst32 = reinterpret_cast<uint32_t*>(img.scanLine(static_cast<int>(y)));
                for (UINT32 x = 0; x < width; ++x) {
                    dst32[x] = src32[x] | 0xFF000000u;
                }
            }
            buffer2D->Unlock2D();
            return img;
        }
    }

    // 3. Fall back to 1D Lock with macroblock/GPU alignment detection
    BYTE* rawData = nullptr;
    DWORD maxLen = 0;
    DWORD curLen = 0;
    if (SUCCEEDED(buffer->Lock(&rawData, &maxLen, &curLen)) && rawData && curLen > 0) {
        LONG actualStride = qAbs(defaultStride);

        // Media Foundation decoders frequently pad rows to 16/32-byte or macroblock alignment
        const UINT32 aligned16H = (height + 15) & ~15;
        const UINT32 aligned32H = (height + 31) & ~31;
        if (aligned16H > 0 && (curLen >= aligned16H * rowBytes) && (curLen % aligned16H == 0)) {
            actualStride = static_cast<LONG>(curLen / aligned16H);
        } else if (aligned32H > 0 && (curLen >= aligned32H * rowBytes) && (curLen % aligned32H == 0)) {
            actualStride = static_cast<LONG>(curLen / aligned32H);
        } else if (height > 0 && (curLen >= height * rowBytes) && (curLen % height == 0)) {
            actualStride = static_cast<LONG>(curLen / height);
        }

        if (actualStride < static_cast<LONG>(rowBytes)) {
            actualStride = static_cast<LONG>(rowBytes);
        }

        QImage img(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
        for (UINT32 y = 0; y < height; ++y) {
            const UINT32 srcY = (defaultStride < 0) ? (height - 1 - y) : y;
            const size_t offset = static_cast<size_t>(srcY) * static_cast<size_t>(actualStride);
            if (offset + rowBytes <= static_cast<size_t>(curLen)) {
                const uint32_t* src32 = reinterpret_cast<const uint32_t*>(rawData + offset);
                uint32_t* dst32 = reinterpret_cast<uint32_t*>(img.scanLine(static_cast<int>(y)));
                for (UINT32 x = 0; x < width; ++x) {
                    dst32[x] = src32[x] | 0xFF000000u;
                }
            }
        }
        buffer->Unlock();
        return img;
    }

    return QImage();
}

static bool seekReaderTo100ns(IMFSourceReader* reader, LONGLONG pos100ns)
{
    if (!reader) return false;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = pos100ns;
    const HRESULT hr = reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    return SUCCEEDED(hr);
}

static QImage composeVideoFrameForViewport(const QImage& rawFrame,
                                           const QSize& targetSize,
                                           const QString& scaling,
                                           int blurRadius,
                                           int dimmingPercent)
{
    if (rawFrame.isNull() || !targetSize.isValid() || targetSize.width() <= 0 || targetSize.height() <= 0) {
        return rawFrame;
    }

    QImage working = rawFrame;
    // Fast multi-scale blur approximation when blurRadius > 0
    if (blurRadius > 0) {
        const int factor = qBound(2, 1 + blurRadius / 2, 16);
        const QSize smallSize(qMax(16, working.width() / factor), qMax(9, working.height() / factor));
        working = working.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .scaled(working.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QImage canvas(targetSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QString mode = scaling.trimmed().toLower();
    if (mode == QStringLiteral("fill")) {
        p.drawImage(QRect(QPoint(0, 0), targetSize), working);
    } else if (mode == QStringLiteral("contain")) {
        const QSize scaled = working.size().scaled(targetSize, Qt::KeepAspectRatio);
        const QPoint topLeft((targetSize.width() - scaled.width()) / 2,
                             (targetSize.height() - scaled.height()) / 2);
        p.drawImage(QRect(topLeft, scaled), working);
    } else {
        // Default: cover
        const QSize scaled = working.size().scaled(targetSize, Qt::KeepAspectRatioByExpanding);
        const QPoint topLeft((targetSize.width() - scaled.width()) / 2,
                             (targetSize.height() - scaled.height()) / 2);
        p.drawImage(QRect(topLeft, scaled), working);
    }

    if (dimmingPercent > 0) {
        const int alpha = qBound(0, (dimmingPercent * 255) / 100, 235);
        p.fillRect(QRect(QPoint(0, 0), targetSize), QColor(0, 0, 0, alpha));
    }
    p.end();
    return canvas;
}

class WinMmAudioOut {
public:
    WinMmAudioOut() = default;
    ~WinMmAudioOut() { close(); }

    bool open(UINT32 sampleRate, UINT32 channels)
    {
        close();
        if (sampleRate == 0 || channels == 0) {
            return false;
        }
        m_channels = qBound<UINT32>(1, channels, 2);
        m_sampleRate = sampleRate;

        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = static_cast<WORD>(m_channels);
        wfx.nSamplesPerSec = sampleRate;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = static_cast<WORD>((wfx.nChannels * wfx.wBitsPerSample) / 8);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        if (waveOutOpen(&m_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            m_hWaveOut = nullptr;
            return false;
        }

        m_buffers.resize(32);
        for (auto& slot : m_buffers) {
            slot.storage.resize(65536);
            memset(&slot.hdr, 0, sizeof(WAVEHDR));
            slot.hdr.lpData = slot.storage.data();
            slot.prepared = false;
        }
        return true;
    }

    void close()
    {
        if (m_hWaveOut) {
            waveOutReset(m_hWaveOut);
            for (auto& slot : m_buffers) {
                if (slot.prepared) {
                    waveOutUnprepareHeader(m_hWaveOut, &slot.hdr, sizeof(WAVEHDR));
                    slot.prepared = false;
                }
            }
            waveOutClose(m_hWaveOut);
            m_hWaveOut = nullptr;
        }
        m_buffers.clear();
    }

    void reset()
    {
        if (m_hWaveOut) {
            waveOutReset(m_hWaveOut);
            for (auto& slot : m_buffers) {
                if (slot.prepared) {
                    waveOutUnprepareHeader(m_hWaveOut, &slot.hdr, sizeof(WAVEHDR));
                    slot.prepared = false;
                }
            }
        }
    }

    void writeSample(IMFSample* sample, UINT32 srcChannels, UINT32 srcBits, bool srcIsFloat, int volumePercent, bool muted)
    {
        if (!m_hWaveOut || !sample || m_buffers.empty()) {
            return;
        }
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || !buf) {
            return;
        }
        BYTE* raw = nullptr;
        DWORD maxLen = 0;
        DWORD curLen = 0;
        if (FAILED(buf->Lock(&raw, &maxLen, &curLen)) || !raw || curLen == 0) {
            return;
        }

        const float gain = muted ? 0.0f : qBound(0.0f, static_cast<float>(volumePercent) / 100.0f, 1.0f);
        if (gain <= 0.0001f) {
            buf->Unlock();
            return;
        }

        // Find a free or completed buffer slot
        BufferSlot* chosen = nullptr;
        for (auto& slot : m_buffers) {
            if (!slot.prepared || (slot.hdr.dwFlags & WHDR_DONE)) {
                if (slot.prepared) {
                    waveOutUnprepareHeader(m_hWaveOut, &slot.hdr, sizeof(WAVEHDR));
                    slot.prepared = false;
                }
                chosen = &slot;
                break;
            }
        }
        if (!chosen) {
            buf->Unlock();
            return;
        }

        size_t neededBytes = 0;
        if (srcIsFloat && srcBits == 32) {
            const size_t totalFloats = curLen / sizeof(float);
            const size_t frames = srcChannels > 0 ? (totalFloats / srcChannels) : 0;
            neededBytes = frames * m_channels * sizeof(int16_t);
            if (neededBytes > chosen->storage.size()) {
                chosen->storage.resize(neededBytes);
            }
            const float* srcF = reinterpret_cast<const float*>(raw);
            int16_t* dstS = reinterpret_cast<int16_t*>(chosen->storage.data());
            for (size_t f = 0; f < frames; ++f) {
                for (UINT32 c = 0; c < m_channels; ++c) {
                    const UINT32 srcC = (c < srcChannels) ? c : 0;
                    const float v = qBound(-1.0f, srcF[f * srcChannels + srcC] * gain, 1.0f);
                    dstS[f * m_channels + c] = static_cast<int16_t>(v * 32767.0f);
                }
            }
        } else if (!srcIsFloat && srcBits == 16) {
            const size_t totalShorts = curLen / sizeof(int16_t);
            const size_t frames = srcChannels > 0 ? (totalShorts / srcChannels) : 0;
            neededBytes = frames * m_channels * sizeof(int16_t);
            if (neededBytes > chosen->storage.size()) {
                chosen->storage.resize(neededBytes);
            }
            const int16_t* srcS = reinterpret_cast<const int16_t*>(raw);
            int16_t* dstS = reinterpret_cast<int16_t*>(chosen->storage.data());
            for (size_t f = 0; f < frames; ++f) {
                for (UINT32 c = 0; c < m_channels; ++c) {
                    const UINT32 srcC = (c < srcChannels) ? c : 0;
                    const int32_t scaled = static_cast<int32_t>(static_cast<float>(srcS[f * srcChannels + srcC]) * gain);
                    dstS[f * m_channels + c] = static_cast<int16_t>(qBound(-32768, scaled, 32767));
                }
            }
        }
        buf->Unlock();

        if (neededBytes == 0) {
            return;
        }

        memset(&chosen->hdr, 0, sizeof(WAVEHDR));
        chosen->hdr.lpData = chosen->storage.data();
        chosen->hdr.dwBufferLength = static_cast<DWORD>(neededBytes);
        if (waveOutPrepareHeader(m_hWaveOut, &chosen->hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
            chosen->prepared = true;
            waveOutWrite(m_hWaveOut, &chosen->hdr, sizeof(WAVEHDR));
        }
    }

private:
    struct BufferSlot {
        std::vector<char> storage;
        WAVEHDR hdr = {};
        bool prepared = false;
    };
    HWAVEOUT m_hWaveOut = nullptr;
    UINT32 m_sampleRate = 44100;
    UINT32 m_channels = 2;
    std::vector<BufferSlot> m_buffers;
};

} // namespace
#endif

struct WinMfVideoEngine::PreviewSession {
    QMutex mutex;
#ifdef Q_OS_WIN
    ComPtr<IMFSourceReader> reader;
    DWORD videoStreamIndex = 0;
    UINT32 width = 0;
    UINT32 height = 0;
    LONG stride = 0;
#endif
    qint64 lastDecodeMs = 0;
    QByteArray cachedJpeg;
};

WinMfVideoEngine& WinMfVideoEngine::instance()
{
    static WinMfVideoEngine s_engine;
    return s_engine;
}

WinMfVideoEngine::WinMfVideoEngine() = default;

WinMfVideoEngine::~WinMfVideoEngine()
{
    stopDesktopVideo();
    stopPreviewVideo();
}

QImage WinMfVideoEngine::extractThumbnail(const QString& videoFilePath, const QSize& targetSize)
{
#ifdef Q_OS_WIN
    ComAndMfScope mfScope;
    UINT32 width = 0;
    UINT32 height = 0;
    LONG stride = 0;
    DWORD videoStreamIndex = 0;
    ComPtr<IMFSourceReader> reader = createVideoSourceReader(videoFilePath, false, &width, &height, &stride, &videoStreamIndex);
    if (!reader) {
        return QImage();
    }

    // Try seeking to 1.0s first to avoid fade-in black frames; fall back to 0.0s if needed
    seekReaderTo100ns(reader.Get(), 10000000LL);

    QImage decoded;
    for (int pass = 0; pass < 2 && decoded.isNull(); ++pass) {
        if (pass == 1) {
            seekReaderTo100ns(reader.Get(), 0);
        }
        for (int attempt = 0; attempt < 20; ++attempt) {
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            const HRESULT hr = reader->ReadSample(
                videoStreamIndex, 0, &streamIndex, &flags, &timestamp, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                break;
            }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                queryVideoFormatGeometry(reader.Get(), videoStreamIndex, &width, &height, &stride);
            }
            if (sample && width > 0 && height > 0) {
                decoded = copySampleToQImage(sample.Get(), width, height, stride);
                if (!decoded.isNull()) {
                    break;
                }
            }
        }
    }

    if (decoded.isNull()) {
        return QImage();
    }

    if (targetSize.isValid() && targetSize.width() > 0 && targetSize.height() > 0) {
        const QImage scaled = decoded.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QRect crop((scaled.width() - targetSize.width()) / 2,
                         (scaled.height() - targetSize.height()) / 2,
                         targetSize.width(),
                         targetSize.height());
        return scaled.copy(crop);
    }
    return decoded;
#else
    Q_UNUSED(videoFilePath);
    Q_UNUSED(targetSize);
    return QImage();
#endif
}

void WinMfVideoEngine::stopPreviewVideo()
{
    m_previewStopRequested.store(true);
    if (m_previewThread.joinable()) {
        m_previewThread.join();
    }
    QMutexLocker lock(&m_previewMutex);
    m_previewRunning.store(false);
    m_previewVideoPath.clear();
    m_latestPreviewRawFrame = QImage();
    m_latestPreviewJpeg.clear();
}

void WinMfVideoEngine::startPreviewVideo(const QString& videoFilePath, int targetFps)
{
    QFileInfo fi(videoFilePath);
    if (!fi.exists() || !fi.isFile()) {
        return;
    }
    const QString canonical = fi.canonicalFilePath();
    {
        QMutexLocker lock(&m_previewMutex);
        if (m_previewRunning.load() && m_previewVideoPath.compare(canonical, Qt::CaseInsensitive) == 0) {
            return;
        }
    }

    stopPreviewVideo();

    {
        QMutexLocker lock(&m_previewMutex);
        m_previewVideoPath = canonical;
        m_previewStopRequested.store(false);
        m_previewRunning.store(true);
        m_lastPreviewRequestTimeMs.store(QDateTime::currentMSecsSinceEpoch());
    }

#ifdef Q_OS_WIN
    m_previewThread = std::thread([this, canonical, targetFps]() {
        ComAndMfScope mfScope;
        UINT32 width = 0;
        UINT32 height = 0;
        LONG stride = 0;
        DWORD videoStreamIdx = (DWORD)-1;
        DWORD audioStreamIdx = (DWORD)-1;

        ComPtr<IMFSourceReader> reader = createVideoSourceReader(
            canonical,
            false,
            &width,
            &height,
            &stride,
            &videoStreamIdx,
            &audioStreamIdx);

        if (!reader || videoStreamIdx == (DWORD)-1) {
            QMutexLocker lock(&m_previewMutex);
            m_previewRunning.store(false);
            return;
        }

        const int fps = qBound(10, targetFps, 60);
        const int frameIntervalMs = 1000 / fps;
        const QSize previewMax(960, 540);

        while (!m_previewStopRequested.load()) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs - m_lastPreviewRequestTimeMs.load() > 4000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp100ns = 0;
            ComPtr<IMFSample> sample;

            const HRESULT hr = reader->ReadSample(
                videoStreamIdx, 0, &streamIndex, &flags, &timestamp100ns, &sample);

            if (FAILED(hr)) {
                break;
            }

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                seekReaderTo100ns(reader.Get(), 0);
                continue;
            }

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                queryVideoFormatGeometry(reader.Get(), videoStreamIdx, &width, &height, &stride);
            }

            if (!sample) {
                continue;
            }

            const QImage rawImg = copySampleToQImage(sample.Get(), width, height, stride);
            if (!rawImg.isNull()) {
                const QImage scaled = rawImg.scaled(previewMax, Qt::KeepAspectRatio, Qt::FastTransformation);
                QByteArray jpeg;
                QBuffer buf(&jpeg);
                buf.open(QIODevice::WriteOnly);
                scaled.save(&buf, "JPG", 78);

                {
                    QMutexLocker lock(&m_previewMutex);
                    m_latestPreviewRawFrame = rawImg;
                    m_latestPreviewJpeg = jpeg;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(frameIntervalMs));
        }

        QMutexLocker lock(&m_previewMutex);
        m_previewRunning.store(false);
    });
#endif
}

QByteArray WinMfVideoEngine::getPreviewFrameJpeg(const QString& videoFilePath, double playbackRate, const QSize& maxSize)
{
    Q_UNUSED(playbackRate);
    if (videoFilePath.isEmpty()) {
        return QByteArray();
    }

    QFileInfo fi(videoFilePath);
    if (!fi.exists() || !fi.isFile()) {
        return QByteArray();
    }
    const QString canonicalPath = fi.canonicalFilePath();

    // 1. If currently decoding for desktop wallpaper, return latest desktop frame
    if (m_desktopRunning.load()) {
        QImage desktopFrame;
        {
            QMutexLocker lock(&m_desktopMutex);
            if (QFileInfo(m_desktopVideoPath).canonicalFilePath().compare(canonicalPath, Qt::CaseInsensitive) == 0 &&
                !m_latestDesktopRawFrame.isNull()) {
                desktopFrame = m_latestDesktopRawFrame;
            }
        }
        if (!desktopFrame.isNull()) {
            const QImage scaled = desktopFrame.scaled(maxSize, Qt::KeepAspectRatio, Qt::FastTransformation);
            QByteArray jpegBytes;
            QBuffer buf(&jpegBytes);
            buf.open(QIODevice::WriteOnly);
            scaled.save(&buf, "JPG", 82);
            return jpegBytes;
        }
    }

    // 2. Active preview decoding: return the latest decoded preview frame from memory (0ms latency!)
    m_lastPreviewRequestTimeMs.store(QDateTime::currentMSecsSinceEpoch());
    {
        QMutexLocker lock(&m_previewMutex);
        if (m_previewRunning.load() && m_previewVideoPath.compare(canonicalPath, Qt::CaseInsensitive) == 0 &&
            !m_latestPreviewJpeg.isEmpty()) {
            return m_latestPreviewJpeg;
        }
    }

    // Launch background preview thread if not already running for this video
    startPreviewVideo(canonicalPath, 30);

    // 3. Fallback for the initial tick: return existing static preview thumbnail
    const QDir parentDir = fi.dir();
    const QStringList candidateThumbnails = {
        parentDir.filePath(QStringLiteral("preview.png")),
        parentDir.filePath(QStringLiteral("preview.jpg")),
        parentDir.filePath(QStringLiteral("preview.webp"))
    };
    for (const QString& thumbPath : candidateThumbnails) {
        if (QFileInfo::exists(thumbPath)) {
            QFile thumbFile(thumbPath);
            if (thumbFile.open(QIODevice::ReadOnly)) {
                return thumbFile.readAll();
            }
        }
    }

    // 4. One-time fallback: extract once and save preview.png if missing
    const QString savedThumbPath = parentDir.filePath(QStringLiteral("preview.png"));
    QImage extracted = extractThumbnail(canonicalPath, maxSize);
    if (!extracted.isNull()) {
        extracted.save(savedThumbPath, "PNG");
        QByteArray jpegBytes;
        QBuffer buf(&jpegBytes);
        buf.open(QIODevice::WriteOnly);
        extracted.save(&buf, "JPG", 82);
        return jpegBytes;
    }

    return QByteArray();
}

void WinMfVideoEngine::startDesktopVideo(const QString& instanceId,
                                         const QString& videoFilePath,
                                         const QRect& viewportRect,
                                         const VideoRenderConfig& initialConfig,
                                         int targetFps,
                                         bool suspended,
                                         std::function<void(const QString&, const QRect&, const QImage&)> onFrameCallback)
{
    stopDesktopVideo();

    {
        QMutexLocker lock(&m_desktopMutex);
        m_desktopInstanceId = instanceId;
        m_desktopVideoPath = videoFilePath;
        m_desktopViewport = viewportRect;
        m_desktopScaling = initialConfig.scaling;
        m_desktopDimming.store(qBound(0, initialConfig.dimming, 85));
        m_desktopBlur.store(qBound(0, initialConfig.blur, 30));
        m_desktopVolume.store(qBound(0, initialConfig.volume, 100));
        m_desktopMuted.store(initialConfig.muted);
        m_desktopPlaybackRate.store(qBound(0.25, initialConfig.playbackRate, 3.0));
        m_desktopFps.store(qBound(1, targetFps, 144));
        m_desktopSuspended.store(suspended);
        m_desktopStopRequested.store(false);
        m_desktopRunning.store(true);
    }

#ifdef Q_OS_WIN
    m_desktopThread = std::thread([this, instanceId, videoFilePath, onFrameCallback]() {
        ComAndMfScope mfScope;

        UINT32 width = 0;
        UINT32 height = 0;
        LONG stride = 0;
        DWORD videoStreamIdx = (DWORD)-1;
        DWORD audioStreamIdx = (DWORD)-1;
        UINT32 audioRate = 0;
        UINT32 audioChannels = 0;
        UINT32 audioBits = 0;
        bool audioIsFloat = false;

        ComPtr<IMFSourceReader> reader = createVideoSourceReader(
            videoFilePath,
            true,
            &width,
            &height,
            &stride,
            &videoStreamIdx,
            &audioStreamIdx,
            &audioRate,
            &audioChannels,
            &audioBits,
            &audioIsFloat);

        if (!reader || videoStreamIdx == (DWORD)-1) {
            m_desktopRunning.store(false);
            return;
        }

        WinMmAudioOut audioOut;
        if (audioRate > 0 && audioChannels > 0 && audioStreamIdx != (DWORD)-1) {
            audioOut.open(audioRate, audioChannels);
        }

        QElapsedTimer wallClock;
        wallClock.start();
        LONGLONG baseVideoPts100ns = -1;
        qint64 baseWallMs = 0;

        while (!m_desktopStopRequested.load()) {
            if (m_desktopSuspended.load()) {
                audioOut.reset();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                baseVideoPts100ns = -1;
                continue;
            }

            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp100ns = 0;
            ComPtr<IMFSample> sample;

            const HRESULT hr = reader->ReadSample(
                MF_SOURCE_READER_ANY_STREAM, 0, &streamIndex, &flags, &timestamp100ns, &sample);

            if (FAILED(hr)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                break;
            }

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                seekReaderTo100ns(reader.Get(), 0);
                audioOut.reset();
                baseVideoPts100ns = -1;
                continue;
            }

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                queryVideoFormatGeometry(reader.Get(), videoStreamIdx, &width, &height, &stride);
            }

            if (!sample) {
                continue;
            }

            if (streamIndex == videoStreamIdx) {
                const QImage rawImg = copySampleToQImage(sample.Get(), width, height, stride);
                if (rawImg.isNull()) {
                    continue;
                }

                QRect currentViewport;
                QString currentScaling;
                {
                    QMutexLocker lock(&m_desktopMutex);
                    m_latestDesktopRawFrame = rawImg;
                    currentViewport = m_desktopViewport;
                    currentScaling = m_desktopScaling;
                }

                const double rate = qBound(0.25, m_desktopPlaybackRate.load(), 3.0);
                if (baseVideoPts100ns < 0 || timestamp100ns < baseVideoPts100ns) {
                    baseVideoPts100ns = timestamp100ns;
                    baseWallMs = wallClock.elapsed();
                } else {
                    const double mediaDeltaMs = static_cast<double>(timestamp100ns - baseVideoPts100ns) / (10000.0 * rate);
                    const qint64 targetWallMs = baseWallMs + static_cast<qint64>(mediaDeltaMs);
                    const qint64 nowWallMs = wallClock.elapsed();
                    qint64 waitMs = targetWallMs - nowWallMs;
                    while (waitMs > 2 && !m_desktopStopRequested.load()) {
                        const qint64 slice = qMin<qint64>(waitMs, 10);
                        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
                        waitMs -= slice;
                    }
                    if (waitMs <= -250 || (targetWallMs - wallClock.elapsed()) >= 500) {
                        // Resync clock after rate change or stall
                        baseVideoPts100ns = timestamp100ns;
                        baseWallMs = wallClock.elapsed();
                    }
                }

                if (m_desktopStopRequested.load()) {
                    break;
                }

                const QSize renderSize = (currentViewport.isValid() && currentViewport.width() > 100)
                    ? currentViewport.size()
                    : QSize(1920, 1080);
                const QImage composed = composeVideoFrameForViewport(
                    rawImg,
                    renderSize,
                    currentScaling,
                    m_desktopBlur.load(),
                    m_desktopDimming.load());

                if (onFrameCallback && !composed.isNull()) {
                    onFrameCallback(instanceId, currentViewport, composed);
                }
            } else if (streamIndex == audioStreamIdx) {
                audioOut.writeSample(
                    sample.Get(),
                    audioChannels,
                    audioBits,
                    audioIsFloat,
                    m_desktopVolume.load(),
                    m_desktopMuted.load());
            }
        }

        audioOut.close();
        m_desktopRunning.store(false);
    });
#endif
}

void WinMfVideoEngine::stopDesktopVideo()
{
    m_desktopStopRequested.store(true);
    if (m_desktopThread.joinable()) {
        m_desktopThread.join();
    }
    QMutexLocker lock(&m_desktopMutex);
    m_desktopRunning.store(false);
    m_desktopInstanceId.clear();
    m_desktopVideoPath.clear();
    m_latestDesktopRawFrame = QImage();
}

bool WinMfVideoEngine::isDesktopVideoRunning() const
{
    return m_desktopRunning.load();
}

QString WinMfVideoEngine::activeDesktopInstanceId() const
{
    QMutexLocker lock(&m_desktopMutex);
    return m_desktopInstanceId;
}

QString WinMfVideoEngine::activeDesktopVideoPath() const
{
    QMutexLocker lock(&m_desktopMutex);
    return m_desktopVideoPath;
}

void WinMfVideoEngine::updateDesktopViewport(const QRect& viewportRect)
{
    QMutexLocker lock(&m_desktopMutex);
    m_desktopViewport = viewportRect;
}

void WinMfVideoEngine::updateDesktopProperty(const QString& key, const QVariant& value)
{
    const QString k = key.trimmed();
    if (k == QStringLiteral("scaling") || k == QStringLiteral("fitMode")) {
        QMutexLocker lock(&m_desktopMutex);
        m_desktopScaling = value.toString();
    } else if (k == QStringLiteral("dimming")) {
        m_desktopDimming.store(qBound(0, value.toInt(), 85));
    } else if (k == QStringLiteral("blur")) {
        m_desktopBlur.store(qBound(0, value.toInt(), 30));
    } else if (k == QStringLiteral("volume")) {
        const int vol = qBound(0, value.toInt(), 100);
        m_desktopVolume.store(vol);
        m_desktopMuted.store(vol <= 0);
    } else if (k == QStringLiteral("muted")) {
        m_desktopMuted.store(value.toBool());
    } else if (k == QStringLiteral("playbackRate")) {
        m_desktopPlaybackRate.store(qBound(0.25, value.toDouble(), 3.0));
    } else if (k == QStringLiteral("reset_defaults")) {
        QMutexLocker lock(&m_desktopMutex);
        m_desktopScaling = QStringLiteral("cover");
        m_desktopDimming.store(15);
        m_desktopBlur.store(0);
        m_desktopVolume.store(80);
        m_desktopMuted.store(false);
        m_desktopPlaybackRate.store(1.0);
    }
}

void WinMfVideoEngine::setDesktopSuspended(bool suspended)
{
    m_desktopSuspended.store(suspended);
}

void WinMfVideoEngine::setDesktopFramerate(int fps)
{
    m_desktopFps.store(qBound(1, fps, 144));
}

// ============================================================================
// WidgetManager Dynamic Wallpaper Subsystem Implementation
// ============================================================================

bool WidgetManager::forwardMouseMoveToWallpaper(const QPoint& clientPos)
{
    if (!m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (!osr) {
        return false;
    }

    // 60fps 频率限制 (16ms 间隔)，平滑广播光标位置，避免高频 IPC 洪峰
    static qint64 s_lastMoveMs = 0;
    static QPoint s_lastPos;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (clientPos == s_lastPos || (now - s_lastMoveMs < 16 && (clientPos - s_lastPos).manhattanLength() < 4)) {
        return true;
    }
    s_lastMoveMs = now;
    s_lastPos = clientPos;

    const QPoint localPos = clientPos - osr->geometry().topLeft();
    osr->sendMouseMove(localPos.x(), localPos.y());
    return true;
}

bool WidgetManager::forwardMouseClickToWallpaper(const QPoint& clientPos, Qt::MouseButton button, bool isRelease, int clickCount)
{
    if (!m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (!osr) {
        return false;
    }

    const QPoint localPos = clientPos - osr->geometry().topLeft();
    osr->sendMouseClick(localPos.x(), localPos.y(), button, isRelease, clickCount);
    return true;
}

bool WidgetManager::forwardWheelToWallpaper(const QPoint& clientPos, const QPoint& angleDelta)
{
    if (!m_cefOsrEnabled || m_activeWallpaperId.isEmpty() || !m_activeWallpaperInteractive) {
        return false;
    }
    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (!osr) {
        return false;
    }

    const QPoint localPos = clientPos - osr->geometry().topLeft();
    osr->sendMouseWheel(localPos.x(), localPos.y(), angleDelta.x(), angleDelta.y());
    return true;
}


void WidgetManager::renderActiveImageWallpaper()
{
    if (m_activeWallpaperType != QStringLiteral("image") || m_activeImageRawFrame.isNull() || !m_canvasWidget) {
        return;
    }
    QRect viewportRect;
    if (LinCef::LinWidgetCanvasWidget::hostWidget() && LinCef::LinWidgetCanvasWidget::hostWidget()->rect().isValid()) {
        viewportRect = LinCef::LinWidgetCanvasWidget::hostWidget()->rect();
    } else if (m_canvasWidget && m_canvasWidget->rect().isValid() && m_canvasWidget->rect().width() > 100) {
        viewportRect = m_canvasWidget->rect();
    } else if (QGuiApplication::primaryScreen()) {
        viewportRect = QGuiApplication::primaryScreen()->geometry();
    } else {
        viewportRect = QRect(0, 0, 1920, 1080);
    }
    const QImage composed = composeVideoFrameForViewport(
        m_activeImageRawFrame,
        viewportRect.size(),
        m_activeImageConfig.scaling,
        m_activeImageConfig.blur,
        m_activeImageConfig.dimming);
    m_canvasWidget->updateWidgetFrame(m_activeWallpaperId, viewportRect, composed, {});
}

bool WidgetManager::startWallpaper(const QString& packagePath, bool isDevelopment, bool interactive, QString* errorMessage)
{
    QSettings runtimeSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    if (runtimeSettings.value(QStringLiteral("LinWidgets/RuntimeUninstalled"), false).toBool() || !m_enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("运行支持组件未安装或系统总开关已关闭。");
        }
        return false;
    }

    if (!m_cefOsrEnabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CEF OSR rendering is disabled.");
        }
        return false;
    }

    if (!LinCef::CefManager::instance()->isCefAvailable() || !LinCef::CefManager::instance()->initialize()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CEF OSR runtime is not available.");
        }
        return false;
    }

    bool resolvedIsDev = isDevelopment;
    QString resolvedPackagePath = resolveOptimalPackagePath(packagePath, &resolvedIsDev, true);

    WidgetManifest manifest;
    QJsonObject customObj = findCustomWallpaper(packagePath);
    if (customObj.isEmpty()) {
        customObj = findCustomWallpaper(resolvedPackagePath);
    }
    const bool isCustomRegistryWall = !customObj.isEmpty();

    if (isCustomRegistryWall) {
        manifest.id = customObj.value(QStringLiteral("id")).toString();
        manifest.name = customObj.value(QStringLiteral("name")).toString();
        manifest.role = QStringLiteral("wallpaper");
        manifest.interactive = false;
        resolvedPackagePath = customObj.value(QStringLiteral("sourcePath")).toString();
    } else {
        if (!WidgetPackage::loadManifest(resolvedPackagePath, &manifest, errorMessage)) {
            return false;
        }
        if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Package manifest role is not 'wallpaper'.");
            }
            return false;
        }
    }

    const QString instanceId = QStringLiteral("wallpaper:") + manifest.id;
    if (m_activeWallpaperId == instanceId &&
        (m_osrBrowsers.contains(instanceId) || WinMfVideoEngine::instance().isDesktopVideoRunning() || m_activeWallpaperType == QStringLiteral("image")) &&
        m_activeWallpaperPackagePath == resolvedPackagePath) {
        if (m_activeWallpaperInteractive != interactive) {
            setWallpaperInteractive(interactive);
        }
        return true;
    }

    // 单例管理：先关闭已有活动壁纸（不重置配置）
    if (!m_activeWallpaperId.isEmpty()) {
        stopWallpaper(false);
    }

    QRect viewportRect;
    if (LinCef::LinWidgetCanvasWidget::hostWidget() && LinCef::LinWidgetCanvasWidget::hostWidget()->rect().isValid()) {
        viewportRect = LinCef::LinWidgetCanvasWidget::hostWidget()->rect();
    } else if (m_canvasWidget && m_canvasWidget->rect().isValid() && m_canvasWidget->rect().width() > 100) {
        viewportRect = m_canvasWidget->rect();
    } else if (m_desktopLayerWindow) {
#ifdef Q_OS_WIN
        viewportRect = windowRect((HWND)m_desktopLayerWindow);
#endif
    }
    if (!viewportRect.isValid() || viewportRect.width() <= 100) {
        if (QGuiApplication::primaryScreen()) {
            viewportRect = QGuiApplication::primaryScreen()->geometry();
        } else {
            viewportRect = QRect(0, 0, 1920, 1080);
        }
    }

    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    const QVariant perWallpaperInteractive = settings.value(QStringLiteral("LinWallpapers/%1/Interactive").arg(manifest.id));
    const bool resolvedInteractive = perWallpaperInteractive.isValid()
        ? perWallpaperInteractive.toBool()
        : manifest.interactive;

    m_activeWallpaperId = instanceId;
    m_activeWallpaperPackagePath = resolvedPackagePath;
    m_activeWallpaperIsDevelopment = resolvedIsDev;
    m_activeWallpaperInteractive = resolvedInteractive;

    WidgetInstance instance;
    instance.instanceId = instanceId;
    instance.packagePath = resolvedPackagePath;
    instance.manifest = manifest;
    instance.isDevelopment = resolvedIsDev;
    instance.autoReload = resolvedIsDev;
    instance.grantedPermissions = grantedPermissionsForManifest(manifest, resolvedPackagePath, resolvedIsDev);
    instance.geometry = viewportRect;

    m_instances.insert(instanceId, instance);
    m_runningWidgets.insert(instanceId);
    configureDevelopmentWatcher(instance);

    // Check if this wallpaper is a video or image wallpaper
    QString wallpaperType;
    QString candidateMediaFile;
    QJsonObject mergedCfg;
    if (isCustomRegistryWall) {
        wallpaperType = customObj.value(QStringLiteral("wallpaperType")).toString().trimmed().toLower();
        candidateMediaFile = resolvedPackagePath;
        mergedCfg = customObj.value(QStringLiteral("customConfig")).toObject();
    } else {
        QJsonObject rawManifestObj;
        QFile mf(QDir(resolvedPackagePath).filePath(QStringLiteral("manifest.json")));
        if (mf.open(QIODevice::ReadOnly)) {
            rawManifestObj = QJsonDocument::fromJson(mf.readAll()).object();
            mf.close();
        }
        wallpaperType = rawManifestObj.value(QStringLiteral("wallpaperType")).toString().trimmed().toLower();
        const QString mediaFileName = rawManifestObj.value(QStringLiteral("mediaFile")).toString().trimmed();
        candidateMediaFile = !mediaFileName.isEmpty() ? QDir(resolvedPackagePath).filePath(mediaFileName) : QString();
        mergedCfg = rawManifestObj.value(QStringLiteral("customConfig")).toObject();
    }

    const QString savedPropsJson = settings.value(QStringLiteral("LinWallpapers/%1/Properties").arg(manifest.id)).toString();
    if (!savedPropsJson.isEmpty()) {
        const QJsonObject savedObj = QJsonDocument::fromJson(savedPropsJson.toUtf8()).object();
        for (auto it = savedObj.begin(); it != savedObj.end(); ++it) {
            mergedCfg.insert(it.key(), it.value());
        }
    }

    VideoRenderConfig renderCfg;
    renderCfg.scaling = mergedCfg.value(QStringLiteral("scaling")).toString(QStringLiteral("cover"));
    renderCfg.dimming = mergedCfg.value(QStringLiteral("dimming")).toInt(15);
    renderCfg.blur = mergedCfg.value(QStringLiteral("blur")).toInt(0);
    renderCfg.volume = mergedCfg.value(QStringLiteral("volume")).toInt(80);
    renderCfg.muted = mergedCfg.value(QStringLiteral("muted")).toBool(renderCfg.volume <= 0);
    renderCfg.playbackRate = mergedCfg.value(QStringLiteral("playbackRate")).toDouble(1.0);

    const bool isNativeVideoWallpaper = (wallpaperType == QStringLiteral("video")) &&
        !candidateMediaFile.isEmpty() && QFileInfo::exists(candidateMediaFile);
    const bool isNativeImageWallpaper = (wallpaperType == QStringLiteral("image")) &&
        !candidateMediaFile.isEmpty() && QFileInfo::exists(candidateMediaFile);

    if (isNativeVideoWallpaper) {
        m_activeWallpaperType = QStringLiteral("video");
        m_activeImageRawFrame = QImage();
        QPointer<WidgetManager> weakThis(this);
        WinMfVideoEngine::instance().startDesktopVideo(
            instanceId,
            candidateMediaFile,
            viewportRect,
            renderCfg,
            m_wallpaperFramerate,
            m_wallpaperSuspended,
            [weakThis](const QString& wid, const QRect& geom, const QImage& img) {
                if (!weakThis) {
                    return;
                }
                QMetaObject::invokeMethod(weakThis, [weakThis, wid, geom, img]() {
                    if (weakThis && weakThis->m_canvasWidget && weakThis->m_activeWallpaperId == wid) {
                        weakThis->m_canvasWidget->updateWidgetFrame(wid, geom, img, {});
                    }
                }, Qt::QueuedConnection);
            });
    } else if (isNativeImageWallpaper) {
        // 动静彻底分流：纯静态图片直接由 LinDesk 原生底座直接绘制，彻底脱离 CEF 渲染管线，0 内存/0 CPU/0 GPU 极佳能效
        m_activeWallpaperType = QStringLiteral("image");
        m_activeImageRawFrame = QImage(candidateMediaFile);
        m_activeImageConfig = renderCfg;
        renderActiveImageWallpaper();
    } else {
        m_activeWallpaperType = QStringLiteral("html");
        m_activeImageRawFrame = QImage();
        auto* osr = new LinCef::CefOsrBrowserInstance(instanceId, this);
        osr->setPackagePath(resolvedPackagePath);
        osr->setAllowNetwork(manifest.permissions.contains(QStringLiteral("network")));
        // 继承能效状态机当前的帧率与休眠状态，避免切换壁纸后绕过电源策略
        osr->setFramerate(m_wallpaperFramerate);
        osr->setVisible(!m_wallpaperSuspended);

        connect(osr, &LinCef::CefOsrBrowserInstance::frameReady, this,
                [this](const QString& wid, const QRect& geom, const QImage& img, const QVector<QRect>& dirty) {
                    if (m_canvasWidget) {
                        m_canvasWidget->updateWidgetFrame(wid, geom, img, dirty);
                    }
                });
        connect(osr, &LinCef::CefOsrBrowserInstance::messageReceived, this,
                &WidgetManager::handleRuntimeMessage);
        connect(osr, &LinCef::CefOsrBrowserInstance::browserCrashed, this,
                &WidgetManager::handleBrowserCrashed);
        connect(osr, &LinCef::CefOsrBrowserInstance::consoleMessageReceived, this,
                [](const QString&, int, const QString&, const QString&, int) {});

        // 当壁纸页面首次加载完成时，自动回放持久化保存在 QSettings 中的该壁纸参数
        const QString manifestIdForReplay = manifest.id;
        QTimer::singleShot(350, this, [this, instanceId, manifestIdForReplay]() {
            if (m_activeWallpaperId != instanceId || !m_osrBrowsers.contains(instanceId)) {
                return;
            }
            QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
            const QString savedJson = s.value(QStringLiteral("LinWallpapers/%1/Properties").arg(manifestIdForReplay)).toString();
            if (savedJson.isEmpty()) {
                return;
            }
            const QJsonObject savedProps = QJsonDocument::fromJson(savedJson.toUtf8()).object();
            for (auto it = savedProps.begin(); it != savedProps.end(); ++it) {
                QJsonObject propMsg;
                propMsg.insert(QStringLiteral("type"), QStringLiteral("wallpaper_property_change"));
                propMsg.insert(QStringLiteral("key"), it.key());
                propMsg.insert(QStringLiteral("property"), it.key());
                propMsg.insert(QStringLiteral("value"), it.value());
                sendMessageToWidget(instanceId, propMsg);
            }
        });

        const QString entryFile = manifest.entry.isEmpty() ? QStringLiteral("index.html") : manifest.entry;
        const QString entryUrl = QStringLiteral("linwidget://%1/%2").arg(instanceId, entryFile);
        osr->create(entryUrl, viewportRect);
        m_osrBrowsers.insert(instanceId, osr);
    }

    saveWallpaperConfig(resolvedPackagePath, resolvedIsDev, resolvedInteractive);
    updateDefaultCefPage();

    if (LinCef::LinWidgetCanvasWidget::hostWidget()) {
        LinCef::LinWidgetCanvasWidget::hostWidget()->update();
    }

    emit widgetLaunched(instanceId);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void WidgetManager::stopWallpaper(bool clearConfig)
{
    WinMfVideoEngine::instance().stopDesktopVideo();
    m_activeWallpaperType.clear();
    m_activeImageRawFrame = QImage();
    m_activeImageConfig = VideoRenderConfig();

    if (!m_activeWallpaperId.isEmpty()) {
        const QString oldId = m_activeWallpaperId;
        if (m_canvasWidget) {
            m_canvasWidget->removeWidgetFrame(oldId);
        }
        if (auto* osr = m_osrBrowsers.take(oldId)) {
            osr->close();
            osr->deleteLater();
        }
        m_runningWidgets.remove(oldId);
        stopSystemInfoWatchesForInstance(oldId);
        clearDevelopmentWatcher(oldId);
        m_instances.remove(oldId);
        emit widgetStopped(oldId);
    }

    m_activeWallpaperId.clear();
    m_activeWallpaperPackagePath.clear();
    m_activeWallpaperIsDevelopment = false;
    m_activeWallpaperInteractive = false;

    if (clearConfig) {
        saveWallpaperConfig(QString(), false, false);
    }

    if (LinCef::LinWidgetCanvasWidget::hostWidget()) {
        LinCef::LinWidgetCanvasWidget::hostWidget()->update();
    }

    updateDefaultCefPage();
}

void WidgetManager::setWallpaperInteractive(bool interactive)
{
    m_activeWallpaperInteractive = interactive;
    if (!m_activeWallpaperId.isEmpty()) {
        const WidgetInstance instance = m_instances.value(m_activeWallpaperId);
        const QString manifestId = !instance.manifest.id.isEmpty() ? instance.manifest.id : m_activeWallpaperId;
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("LinWallpapers/%1/Interactive").arg(manifestId), interactive);
        settings.sync();
    }
    if (!m_activeWallpaperPackagePath.isEmpty()) {
        saveWallpaperConfig(m_activeWallpaperPackagePath, m_activeWallpaperIsDevelopment, interactive);
    }
}

void WidgetManager::setWallpaperSuspended(bool suspended)
{
    if (m_wallpaperSuspended == suspended) {
        return;
    }
    m_wallpaperSuspended = suspended;
    WinMfVideoEngine::instance().setDesktopSuspended(suspended);
    if (m_activeWallpaperType == QStringLiteral("image")) {
        if (suspended) {
            if (m_canvasWidget && !m_activeWallpaperId.isEmpty()) {
                m_canvasWidget->removeWidgetFrame(m_activeWallpaperId);
                if (LinCef::LinWidgetCanvasWidget::hostWidget()) {
                    LinCef::LinWidgetCanvasWidget::hostWidget()->update();
                }
            }
        } else {
            renderActiveImageWallpaper();
        }
    }
    if (!m_activeWallpaperId.isEmpty()) {
        auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
        if (osr) {
            osr->setVisible(!suspended);
        }
    }
}

void WidgetManager::setWallpaperFramerate(int fps)
{
    m_wallpaperFramerate = qBound(1, fps, 144);
    WinMfVideoEngine::instance().setDesktopFramerate(m_wallpaperFramerate);
    if (!m_activeWallpaperId.isEmpty()) {
        auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
        if (osr) {
            osr->setFramerate(fps);
        }
    }
}

void WidgetManager::syncWallpaperWindowLayer()
{
    if (m_activeWallpaperId.isEmpty()) {
        return;
    }
    QRect viewportRect;
    if (LinCef::LinWidgetCanvasWidget::hostWidget() && LinCef::LinWidgetCanvasWidget::hostWidget()->rect().isValid()) {
        viewportRect = LinCef::LinWidgetCanvasWidget::hostWidget()->rect();
    } else if (m_canvasWidget && m_canvasWidget->rect().isValid() && m_canvasWidget->rect().width() > 100) {
        viewportRect = m_canvasWidget->rect();
    } else if (m_desktopLayerWindow) {
#ifdef Q_OS_WIN
        viewportRect = windowRect((HWND)m_desktopLayerWindow);
#endif
    }
    if (!viewportRect.isValid() || viewportRect.width() <= 100) {
        if (QGuiApplication::primaryScreen()) {
            viewportRect = QGuiApplication::primaryScreen()->geometry();
        } else {
            viewportRect = QRect(0, 0, 1920, 1080);
        }
    }

    WinMfVideoEngine::instance().updateDesktopViewport(viewportRect);
    if (m_activeWallpaperType == QStringLiteral("image")) {
        renderActiveImageWallpaper();
    }

    auto* osr = m_osrBrowsers.value(m_activeWallpaperId, nullptr);
    if (osr && viewportRect.isValid() && osr->geometry() != viewportRect) {
        osr->setGeometry(viewportRect);
    }
    if (viewportRect.isValid() && m_instances.contains(m_activeWallpaperId)) {
        m_instances[m_activeWallpaperId].geometry = viewportRect;
    }
}

void WidgetManager::loadWallpaperConfig()
{
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    const bool enabled = settings.value(QStringLiteral("LinWallpapers/Enabled"), false).toBool();
    if (!enabled) {
        return;
    }
    QString packagePath = settings.value(QStringLiteral("LinWallpapers/PackagePath")).toString();
    bool isDev = settings.value(QStringLiteral("LinWallpapers/IsDevelopment"), false).toBool();
    const bool interactive = settings.value(QStringLiteral("LinWallpapers/Interactive"), false).toBool();

    packagePath = resolveOptimalPackagePath(packagePath, &isDev, true);

    if (!packagePath.isEmpty() && QFileInfo::exists(packagePath)) {
        startWallpaper(packagePath, isDev, interactive, nullptr);
    }
}

void WidgetManager::saveWallpaperConfig(const QString& packagePath, bool isDevelopment, bool interactive)
{
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    if (packagePath.isEmpty()) {
        settings.setValue(QStringLiteral("LinWallpapers/Enabled"), false);
        settings.remove(QStringLiteral("LinWallpapers/PackagePath"));
        settings.remove(QStringLiteral("LinWallpapers/IsDevelopment"));
        settings.remove(QStringLiteral("LinWallpapers/Interactive"));
    } else {
        settings.setValue(QStringLiteral("LinWallpapers/Enabled"), true);
        settings.setValue(QStringLiteral("LinWallpapers/PackagePath"), packagePath);
        settings.setValue(QStringLiteral("LinWallpapers/IsDevelopment"), isDevelopment);
        settings.setValue(QStringLiteral("LinWallpapers/Interactive"), interactive);
    }
    settings.sync();
}

} // namespace LinWidget
