#include "../include/TtsManager.h"
#include <sherpa-onnx/c-api/c-api.h>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>

// --- TtsWorker ---

TtsWorker::TtsWorker(QObject *parent) : QObject(parent) {}

TtsWorker::~TtsWorker() {
    QMutexLocker locker(&m_ttsMutex);
    if (m_tts) {
        SherpaOnnxDestroyOfflineTts(m_tts);
        m_tts = nullptr;
    }
}
bool TtsWorker::initModel(const QString &modelPath, const QString &tokensPath,
                          const QString &voicesPath, const QString &dataDir)
{
    QMutexLocker locker(&m_ttsMutex);

    if (m_tts) {
        SherpaOnnxDestroyOfflineTts(m_tts);
        m_tts = nullptr;
    }

    // Validate files exist
    if (!QFile::exists(modelPath) || !QFile::exists(tokensPath) ||
        !QFile::exists(voicesPath) || !QDir(dataDir).exists()) {
        qWarning() << "[TTS] Model files not found or invalid:";
        qWarning() << "  Model:" << modelPath << "exists:" << QFile::exists(modelPath);
        qWarning() << "  Tokens:" << tokensPath << "exists:" << QFile::exists(tokensPath);
        qWarning() << "  Voices:" << voicesPath << "exists:" << QFile::exists(voicesPath);
        qWarning() << "  Data dir:" << dataDir << "isDir:" << QDir(dataDir).exists();
        return false;
    }

    // Keep QByteArray objects alive
    QByteArray modelBytes  = modelPath.toUtf8();
    QByteArray tokensBytes = tokensPath.toUtf8();
    QByteArray voicesBytes = voicesPath.toUtf8();
    QByteArray dataBytes   = dataDir.toUtf8();

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    config.model.kokoro.model     = modelBytes.constData();
    config.model.kokoro.voices    = voicesBytes.constData();
    config.model.kokoro.tokens    = tokensBytes.constData();
    config.model.kokoro.data_dir  = dataBytes.constData();

    config.model.num_threads = 2;
    config.model.debug = 0;
    config.model.provider = "cpu";
    config.max_num_sentences = 1;

    qDebug() << "[TTS] Creating Kokoro TTS engine...";
    m_tts = SherpaOnnxCreateOfflineTts(&config);

    if (!m_tts) {
        qWarning() << "[TTS] Failed to initialize model";
        return false;
    }

    m_sampleRate = SherpaOnnxOfflineTtsSampleRate(m_tts);
    if (m_sampleRate <= 0) {
        qWarning() << "[TTS] Invalid sample rate:" << m_sampleRate;
        SherpaOnnxDestroyOfflineTts(m_tts);
        m_tts = nullptr;
        return false;
    }

    qDebug() << "[TTS] Model loaded. Sample rate:" << m_sampleRate;
    return true;
}

void TtsWorker::synthesizeSentence(const QString &text, int sid) {
    QMutexLocker locker(&m_ttsMutex);

    if (!m_tts || text.trimmed().isEmpty()) {
        return;
    }

    QByteArray textBytes = text.toUtf8();
    qDebug() << "[TTS] Synthesizing text (length:" << text.length() << ", speaker ID:" << sid << ")";

    const SherpaOnnxGeneratedAudio *audio = SherpaOnnxOfflineTtsGenerate(
        m_tts, textBytes.constData(), sid, 1.0f);

    if (!audio || audio->n <= 0) {
        if (audio) {
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        }
        emit synthesisError("Failed to generate audio for text: " + text.left(50));
        return;
    }

    QByteArray pcmData;
    pcmData.resize(audio->n * sizeof(int16_t));
    int16_t *outPtr = reinterpret_cast<int16_t *>(pcmData.data());

    for (int32_t i = 0; i < audio->n; ++i) {
        float sample = audio->samples[i];
        sample = qBound(-1.0f, sample, 1.0f);
        outPtr[i] = static_cast<int16_t>(sample * 32767.0f);
    }

    int sampleRate = audio->sample_rate;
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    locker.unlock();
    emit audioGenerated(pcmData, sampleRate, sid);
}

// --- TtsManager ---

TtsManager::TtsManager(QObject *parent) : QObject(parent) {
    m_worker = new TtsWorker();
    m_worker->moveToThread(&m_workerThread);

    connect(this, &TtsManager::requestSynthesis,
            m_worker, &TtsWorker::synthesizeSentence,
            Qt::QueuedConnection);

    connect(m_worker, &TtsWorker::audioGenerated,
            this, &TtsManager::handleAudioGenerated,
            Qt::QueuedConnection);

    connect(m_worker, &TtsWorker::synthesisError,
            this, &TtsManager::handleSynthesisError,
            Qt::QueuedConnection);

    m_workerThread.start();
}

TtsManager::~TtsManager() {
    m_enabled = false;

    stopAndClear();

    // Safely exit worker thread without calling terminate()
    m_workerThread.quit();
    if (!m_workerThread.wait(3000)) {
        qWarning() << "[TTS] Worker thread failed to exit cleanly";
    }

    // Clean up worker instance explicitly after thread stops
    delete m_worker;
    m_worker = nullptr;
}

bool TtsManager::initialize(const QString &modelDir) {
    if (!validateModelFiles(modelDir)) {
        emit errorOccurred("Invalid model directory: " + modelDir);
        return false;
    }

    QString modelPath  = modelDir + "/model.onnx";
    QString tokensPath = modelDir + "/tokens.txt";
    QString voicesPath = modelDir + "/voices.bin";
    QString dataDir    = modelDir + "/espeak-ng-data";

    bool initSuccess = false;

    // Invoke slot directly using explicit types to prevent memory corruption
    QMetaObject::invokeMethod(
        m_worker,
        "initModel",
        Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(bool, initSuccess),
        Q_ARG(QString, modelPath),
        Q_ARG(QString, tokensPath),
        Q_ARG(QString, voicesPath),
        Q_ARG(QString, dataDir)
    );

    if (initSuccess) {
        m_defaultSampleRate = m_worker->getSampleRate();
        m_initialized = true;
        qDebug() << "[TTS] Model initialized successfully";
    } else {
        emit errorOccurred("Failed to initialize TTS model");
    }

    return initSuccess;
}

void TtsManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        stopAndClear();
    }
}

void TtsManager::enqueueSentence(const QString &sentence, int speakerId) {
    if (!m_enabled.load() || !m_initialized.load()) {
        return;
    }
    emit requestSynthesis(sentence, speakerId);
}

void TtsManager::stopAndClear() {
    uint64_t newGen = m_generation.fetch_add(1) + 1;

    {
        QMutexLocker locker(&m_queueMutex);
        m_audioQueue.clear();
    }

    cleanupAudioSink();

    {
        QMutexLocker bufferLocker(&m_audioBufferMutex);
        if (m_currentAudioBuffer.isOpen()) {
            m_currentAudioBuffer.close();
        }
    }

    m_isPlaying = false;
    qDebug() << "[TTS] Stopped and cleared (generation:" << newGen << ")";
}

int TtsManager::queueSize() const {
    QMutexLocker locker(&m_queueMutex);
    return m_audioQueue.size();
}

void TtsManager::handleAudioGenerated(const QByteArray &pcmData, int sampleRate, int speakerId) {
    if (!m_enabled.load()) {
        return;
    }

    AudioChunk chunk;
    chunk.data = pcmData;
    chunk.sampleRate = sampleRate;
    chunk.speakerId = speakerId;
    chunk.timestamp = QDateTime::currentMSecsSinceEpoch();

    {
        QMutexLocker locker(&m_queueMutex);
        m_audioQueue.enqueue(chunk);
    }

    if (!m_isPlaying.load()) {
        QMetaObject::invokeMethod(this, &TtsManager::playNextInQueue, Qt::QueuedConnection);
    }
}

void TtsManager::handleSynthesisError(const QString &error) {
    qWarning() << "[TTS] Synthesis error:" << error;
    emit errorOccurred(error);
}

void TtsManager::playNextInQueue() {
    if (!m_enabled.load()) {
        return;
    }

    AudioChunk chunk;
    bool hasChunk = false;

    {
        QMutexLocker locker(&m_queueMutex);
        if (m_audioQueue.isEmpty()) {
            m_isPlaying = false;
            emit playbackFinished();
            return;
        }

        chunk = m_audioQueue.dequeue();
        hasChunk = true;
    }

    if (!hasChunk) {
        m_isPlaying = false;
        return;
    }

    m_isPlaying = true;

    QAudioFormat format;
    format.setSampleRate(chunk.sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    bool needNewSink = !m_audioSink || m_audioSink->format() != format;

    if (needNewSink) {
        cleanupAudioSink();

        const QAudioDevice &defaultDevice = QMediaDevices::defaultAudioOutput();
        if (defaultDevice.isNull()) {
            qWarning() << "[TTS] No audio output device available";
            m_isPlaying = false;
            emit errorOccurred("No audio output device available");
            return;
        }

        m_audioSink = std::make_unique<QAudioSink>(defaultDevice, format);

        connect(m_audioSink.get(), &QAudioSink::stateChanged, this,
                [this](QAudio::State state) {
                    if (state == QAudio::IdleState) {
                        QMetaObject::invokeMethod(this, &TtsManager::playNextInQueue,
                                                Qt::QueuedConnection);
                    }
                }, Qt::QueuedConnection);
    }

    {
        QMutexLocker bufferLocker(&m_audioBufferMutex);
        if (m_currentAudioBuffer.isOpen()) {
            m_currentAudioBuffer.close();
        }
        m_currentAudioBuffer.setData(chunk.data);
        m_currentAudioBuffer.open(QIODevice::ReadOnly);
    }

    if (m_audioSink) {
        m_audioSink->start(&m_currentAudioBuffer);
    }
}

void TtsManager::cleanupAudioSink() {
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->disconnect();
        m_audioSink.reset();
    }
}

bool TtsManager::validateModelFiles(const QString &modelDir) {
    QDir dir(modelDir);
    if (!dir.exists()) {
        qWarning() << "[TTS] Model directory does not exist:" << modelDir;
        return false;
    }

    QStringList requiredFiles = {"model.onnx", "tokens.txt", "voices.bin"};
    for (const QString &file : requiredFiles) {
        QString filePath = dir.filePath(file);
        if (!QFile::exists(filePath)) {
            qWarning() << "[TTS] Missing required model file:" << filePath;
            return false;
        }

        QFileInfo fileInfo(filePath);
        if (fileInfo.size() == 0) {
            qWarning() << "[TTS] Model file is empty:" << filePath;
            return false;
        }
    }

    // Validate espeak-ng-data directory
    QString dataDir = dir.filePath("espeak-ng-data");
    if (!QDir(dataDir).exists()) {
        qWarning() << "[TTS] Missing espeak-ng-data directory:" << dataDir;
        return false;
    }

    return true;
}