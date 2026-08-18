#include "../include/TtsManager.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QNetworkProxy>

TtsManager::TtsManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_audioBuffer(this)
    , m_enabled(true)
    , m_initialized(false)
    , m_isPlaying(false)
    , m_dockerProcess(nullptr)
    , m_containerStarted(false)
    , m_healthCheckTimer(new QTimer(this))
    , m_healthAttempts(0)
{
    m_networkManager->setProxy(QNetworkProxy::NoProxy);

    // Connect manager's finished signal to our handler
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &TtsManager::onTtsReplyFinished);

    // Extra lambda to confirm the manager signal is emitted
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, [this](QNetworkReply* reply) {
                qDebug() << "[TTS] Manager finished signal received (lambda).";
                // We don't process here; the slot will handle it.
            });

    m_healthCheckTimer->setInterval(2000);
    connect(m_healthCheckTimer, &QTimer::timeout, this, &TtsManager::checkServerHealth);
}

TtsManager::~TtsManager()
{
    setEnabled(false);
    stopAndClear();
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    cleanupAudioSink();
    stopDockerContainer();
}

bool TtsManager::initialize(const QString &serverUrl, bool autoStart)
{
    if (m_initialized)
        return true;

    if (autoStart) {
        if (!checkDockerAvailable()) {
            emit errorOccurred("Docker is not available or user lacks permissions.");
            return false;
        }
        startDockerContainer();
        m_initialized = true;
        return true;
    } else {
        if (serverUrl.isEmpty())
            return false;
        m_serverUrl = QUrl(serverUrl);
        if (!m_serverUrl.isValid()) {
            qWarning() << "[TTS] Invalid server URL:" << serverUrl;
            return false;
        }
        m_initialized = true;
        return true;
    }
}

bool TtsManager::checkDockerAvailable()
{
    QProcess check;
    check.start("docker", QStringList() << "info");
    check.waitForFinished(2000);
    if (check.exitCode() != 0) {
        qWarning() << "[TTS] Docker not available or not running.";
        return false;
    }
    return true;
}

void TtsManager::startDockerContainer()
{
    QProcess check;
    QStringList args;
    args << "ps" << "--filter" << "ancestor=" + QString::fromUtf8(DOCKER_IMAGE)
         << "--format" << "{{.ID}}";
    check.start("docker", args);
    check.waitForFinished(2000);
    QString output = QString::fromUtf8(check.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        m_containerId = output;
        m_containerStarted = true;
        qDebug() << "[TTS] Docker container already running with ID:" << m_containerId;
        m_serverUrl = QUrl(QString("http://127.0.0.1:%1").arg(HOST_PORT));
        m_healthAttempts = 0;
        m_healthCheckTimer->start();
        return;
    }

    QProcess inspect;
    inspect.start("docker", QStringList() << "image" << "inspect" << QString::fromUtf8(DOCKER_IMAGE));
    inspect.waitForFinished(2000);
    if (inspect.exitCode() != 0) {
        pullDockerImage();
    } else {
        runDockerContainer();
    }
}

void TtsManager::pullDockerImage()
{
    qDebug() << "[TTS] Pulling Docker image:" << DOCKER_IMAGE;
    if (m_dockerProcess) {
        m_dockerProcess->deleteLater();
    }
    m_dockerProcess = new QProcess(this);
    connect(m_dockerProcess, &QProcess::finished, this, &TtsManager::onDockerPullFinished);
    connect(m_dockerProcess, &QProcess::readyReadStandardOutput, this, &TtsManager::onDockerOutputReady);
    connect(m_dockerProcess, &QProcess::readyReadStandardError, this, &TtsManager::onDockerErrorReady);

    m_dockerProcess->start("docker", QStringList() << "pull" << QString::fromUtf8(DOCKER_IMAGE));
}

void TtsManager::onDockerPullFinished(int exitCode, QProcess::ExitStatus status)
{
    if (exitCode != 0 || status != QProcess::NormalExit) {
        emit errorOccurred("Failed to pull Docker image. Check network and permissions.");
        return;
    }
    qDebug() << "[TTS] Docker image pulled successfully.";
    runDockerContainer();
}

void TtsManager::runDockerContainer()
{
    qDebug() << "[TTS] Starting Docker container...";
    if (m_dockerProcess) {
        m_dockerProcess->deleteLater();
    }
    m_dockerProcess = new QProcess(this);
    connect(m_dockerProcess, &QProcess::finished, this, &TtsManager::onDockerRunFinished);
    connect(m_dockerProcess, &QProcess::readyReadStandardOutput, this, &TtsManager::onDockerOutputReady);
    connect(m_dockerProcess, &QProcess::readyReadStandardError, this, &TtsManager::onDockerErrorReady);

    QStringList args;
    args << "run" << "--rm" << "-d"
         << "--device=/dev/kfd" << "--device=/dev/dri"
         << "-e" << "HSA_OVERRIDE_GFX_VERSION=" + QString::fromUtf8(GFX_VERSION)
         << "-p" << QString("%1:%2").arg(HOST_PORT).arg(CONTAINER_PORT)
         << QString::fromUtf8(DOCKER_IMAGE);

    m_dockerProcess->start("docker", args);
    m_containerStarted = true;
}

void TtsManager::onDockerRunFinished(int exitCode, QProcess::ExitStatus status)
{
    if (exitCode != 0 || status != QProcess::NormalExit) {
        emit errorOccurred("Failed to start Docker container.");
        return;
    }
    if (m_containerId.isEmpty()) {
        QString output = QString::fromUtf8(m_dockerProcess->readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            m_containerId = output;
        }
    }

    if (!m_containerId.isEmpty()) {
        qDebug() << "[TTS] Container started with ID:" << m_containerId;
    } else {
        qWarning() << "[TTS] Could not get container ID.";
    }

    m_serverUrl = QUrl(QString("http://127.0.0.1:%1").arg(HOST_PORT));
    m_healthAttempts = 0;
    m_healthCheckTimer->start();
}

void TtsManager::onDockerOutputReady()
{
    if (!m_dockerProcess)
        return;
    QByteArray data = m_dockerProcess->readAllStandardOutput();
    if (data.isEmpty())
        return;

    if (m_containerId.isEmpty() && m_containerStarted) {
        QString line = QString::fromUtf8(data).trimmed();
        if (line.length() == 64 && QRegularExpression("^[a-f0-9]{64}$").match(line).hasMatch()) {
            m_containerId = line;
            qDebug() << "[TTS] Captured container ID:" << m_containerId;
        }
    }

    qDebug() << "[Docker stdout]" << data;
}

void TtsManager::onDockerErrorReady()
{
    if (m_dockerProcess) {
        QByteArray data = m_dockerProcess->readAllStandardError();
        qWarning() << "[Docker stderr]" << data;
    }
}

void TtsManager::checkServerHealth()
{
    if (!m_initialized) {
        m_serverUrl = QUrl(QString("http://127.0.0.1:%1").arg(HOST_PORT));
    }

    QNetworkRequest request(m_serverUrl.toString() + "/health");
    request.setHeader(QNetworkRequest::UserAgentHeader, "TalosApp/1.0");
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200) {
                m_healthCheckTimer->stop();
                emit serverReady();
                qDebug() << "[TTS] Server is ready.";
                reply->deleteLater();
                return;
            }
        }
        m_healthAttempts++;
        if (m_healthAttempts >= MAX_HEALTH_ATTEMPTS) {
            m_healthCheckTimer->stop();
            emit errorOccurred("TTS server did not become ready within timeout.");
        }
        reply->deleteLater();
    });
}

void TtsManager::stopDockerContainer()
{
    if (!m_containerStarted || m_containerId.isEmpty())
        return;
    QProcess stop;
    stop.start("docker", QStringList() << "stop" << m_containerId);
    stop.waitForFinished(3000);
    m_containerStarted = false;
    m_containerId.clear();
    qDebug() << "[TTS] Docker container stopped.";
}

// ------------------------------------------------------------
// TTS Request and Playback
// ------------------------------------------------------------
void TtsManager::setEnabled(bool enabled)
{
    m_enabled = enabled;
    qDebug() << "[TTS] setEnabled =" << enabled;
    if (!enabled) {
        stopAndClear();
    }
}

void TtsManager::enqueueSentence(const QString &sentence, int speakerId)
{
    qDebug() << "[TTS] enqueueSentence called: text length =" << sentence.length()
             << ", enabled =" << (bool)m_enabled
             << ", initialized =" << (bool)m_initialized;
    if (!m_enabled || !m_initialized || sentence.trimmed().isEmpty()) {
        qDebug() << "[TTS] enqueueSentence aborted.";
        return;
    }
    requestSynthesis(sentence, speakerId);
}

void TtsManager::requestSynthesis(const QString &text, int speakerId)
{
    qDebug() << "[TTS] requestSynthesis: text '" << text.left(50) << "...'";

    QString fullUrl = m_serverUrl.toString() + "/v1/audio/speech";
    qDebug() << "[TTS] Full URL:" << fullUrl;

    QJsonObject payload;
    payload["model"] = "kokoro";
    payload["input"] = text;

    QString voice = QString::fromUtf8(DEFAULT_VOICE);
    if (speakerId == 1) voice = "af_heart";
    else if (speakerId == 2) voice = "af_nicole";
    payload["voice"] = voice;
    payload["response_format"] = "pcm";

    QByteArray jsonData = QJsonDocument(payload).toJson();
    qDebug() << "[TTS] JSON payload:" << jsonData;

    QNetworkRequest request(m_serverUrl);
    request.setUrl(fullUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "TalosApp/1.0");

    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(10000);

    QNetworkReply *reply = m_networkManager->post(request, jsonData);
    reply->setProperty("speakerId", speakerId);
    reply->setProperty("text", text);

    // Direct reply signal – used as a fallback if manager signal fails
    connect(reply, &QNetworkReply::finished, this, [this, reply, timeoutTimer]() {
        qDebug() << "[TTS] Direct reply finished signal received.";
        timeoutTimer->stop();
        timeoutTimer->deleteLater();
        // If the manager signal didn't trigger, process here.
        // But we will also let the manager signal do it.
        // We can call processReply(reply) directly if needed.
        processReply(reply);
    });

    connect(timeoutTimer, &QTimer::timeout, this, [this, reply, timeoutTimer]() {
        if (reply && reply->isRunning()) {
            qWarning() << "[TTS] Request timed out, aborting.";
            reply->abort();
            emit errorOccurred("TTS request timed out after 10 seconds.");
        }
        timeoutTimer->deleteLater();
    });
    timeoutTimer->start();

    qDebug() << "[TTS] POST request sent to" << fullUrl;
}

void TtsManager::onTtsReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        qWarning() << "[TTS] onTtsReplyFinished called with null sender.";
        return;
    }
    qDebug() << "[TTS] Reply received (manager slot).";
    processReply(reply);
}

// Centralised reply processing
void TtsManager::processReply(QNetworkReply *reply)
{
    if (!reply) return;

    qDebug() << "[TTS] Processing reply. Error? " << (reply->error() != QNetworkReply::NoError);
    qDebug() << "[TTS] HTTP status code:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        QString err = QString("TTS request failed: %1").arg(reply->errorString());
        qWarning() << "[TTS]" << err;
        emit errorOccurred(err);
        reply->deleteLater();
        return;
    }

    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus != 200) {
        QString err = QString("TTS server returned HTTP %1").arg(httpStatus);
        QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            err += ": " + QString::fromUtf8(body);
        }
        qWarning() << "[TTS]" << err;
        emit errorOccurred(err);
        reply->deleteLater();
        return;
    }

    int speakerId = reply->property("speakerId").toInt();
    QByteArray audioData = reply->readAll();
    qDebug() << "[TTS] Audio data size:" << audioData.size() << "bytes";

    if (audioData.isEmpty()) {
        qWarning() << "[TTS] Empty audio response";
        emit errorOccurred("Empty audio data from TTS server");
        reply->deleteLater();
        return;
    }

    const int sampleRate = 24000;

    AudioChunk chunk;
    chunk.data = audioData;
    chunk.sampleRate = sampleRate;
    chunk.speakerId = speakerId;
    chunk.timestamp = QDateTime::currentMSecsSinceEpoch();

    {
        QMutexLocker locker(&m_queueMutex);
        m_audioQueue.enqueue(chunk);
        qDebug() << "[TTS] Audio chunk enqueued. Queue size:" << m_audioQueue.size();
    }

    emit sentenceQueued(speakerId);

    if (!m_isPlaying) {
        qDebug() << "[TTS] Starting playback from processReply";
        playNextInQueue();
    }

    reply->deleteLater();
}

// ------------------------------------------------------------
// Playback (unchanged)
// ------------------------------------------------------------
void TtsManager::playNextInQueue()
{
    qDebug() << "[TTS] playNextInQueue called, enabled=" << (bool)m_enabled;
    if (!m_enabled)
        return;

    AudioChunk chunk;
    bool hasChunk = false;

    {
        QMutexLocker locker(&m_queueMutex);
        if (!m_audioQueue.isEmpty()) {
            chunk = m_audioQueue.dequeue();
            hasChunk = true;
            qDebug() << "[TTS] Dequeued chunk, remaining:" << m_audioQueue.size();
        }
    }

    if (!hasChunk) {
        m_isPlaying = false;
        qDebug() << "[TTS] No chunk to play, m_isPlaying = false";
        return;
    }

    m_isPlaying = true;
    qDebug() << "[TTS] Playing chunk, sample rate=" << chunk.sampleRate
             << ", data size=" << chunk.data.size();

    QAudioFormat format;
    format.setSampleRate(chunk.sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    bool needNewSink = !m_audioSink || m_audioSink->format() != format;

    if (needNewSink) {
        cleanupAudioSink();

        const QAudioDevice &defaultDevice = QMediaDevices::defaultAudioOutput();
        if (defaultDevice.isNull()) {
            m_isPlaying = false;
            emit errorOccurred("No audio output device available");
            qWarning() << "[TTS] No audio device available";
            return;
        }

        m_audioSink = std::make_unique<QAudioSink>(defaultDevice, format);
        connect(m_audioSink.get(), &QAudioSink::stateChanged,
                this, &TtsManager::onAudioStateChanged, Qt::QueuedConnection);
        qDebug() << "[TTS] Audio sink created with format" << format.sampleRate();
    }

    {
        QMutexLocker bufferLocker(&m_audioBufferMutex);
        if (m_audioBuffer.isOpen())
            m_audioBuffer.close();
        m_audioBuffer.setData(chunk.data);
        m_audioBuffer.open(QIODevice::ReadOnly);
    }

    if (m_audioSink) {
        m_audioSink->start(&m_audioBuffer);
        qDebug() << "[TTS] Audio sink started";
    }
}

void TtsManager::onAudioStateChanged(QAudio::State state)
{
    qDebug() << "[TTS] Audio state changed:" << state;
    if (!m_audioSink)
        return;

    if (state == QAudio::IdleState ||
        (state == QAudio::StoppedState && m_audioSink->error() != QAudio::NoError)) {
        m_isPlaying = false;
        emit sentenceFinished();
        qDebug() << "[TTS] Sentence finished, playing next";
        playNextInQueue();
    }
}

void TtsManager::cleanupAudioSink()
{
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->disconnect();
        m_audioSink.reset();
    }
}

void TtsManager::stopAndClear()
{
    qDebug() << "[TTS] stopAndClear called";
    {
        QMutexLocker locker(&m_queueMutex);
        m_audioQueue.clear();
    }

    cleanupAudioSink();

    {
        QMutexLocker bufferLocker(&m_audioBufferMutex);
        if (m_audioBuffer.isOpen())
            m_audioBuffer.close();
    }

    m_isPlaying = false;

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    m_healthCheckTimer->stop();
}

int TtsManager::queueSize() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_audioQueue.size();
}