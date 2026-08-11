#pragma once

#include <QObject>
#include <QByteArray>
#include <QBuffer>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>
#include <QTimer>
#include <vector>
#include <cmath>

class AudioRecorder : public QObject {
Q_OBJECT
public:
    explicit AudioRecorder(QObject *parent = nullptr) : QObject(parent) {
        m_format.setSampleRate(16000);
        m_format.setChannelCount(1);
        m_format.setSampleFormat(QAudioFormat::Int16);
    }

    QList<QAudioDevice> availableInputDevices() const {
        return QMediaDevices::audioInputs();
    }

    QAudioDevice currentDevice() const {
        return m_currentDevice;
    }

    void setInputDevice(const QAudioDevice &device) {
        if (device.isNull()) {
            qWarning() << "[Audio] Tried to set null device";
            return;
        }

        // Fully tear down the old source
        if (m_audioSource) {
            m_audioSource->stop();
            delete m_audioSource;
            m_audioSource = nullptr;
        }
        m_audioDevice = nullptr;

        m_currentDevice = device;

        m_format.setSampleRate(16000);
        m_format.setChannelCount(1);
        m_format.setSampleFormat(QAudioFormat::Int16);

        if (!device.isFormatSupported(m_format)) {
            qWarning() << "[Audio] 16 kHz Int16 not supported, using preferred";
            m_format = device.preferredFormat();
            m_format.setChannelCount(1);
            if (m_format.sampleFormat() != QAudioFormat::Int16)
                m_format.setSampleFormat(QAudioFormat::Int16);
        }

        m_audioSource = new QAudioSource(device, m_format, this);
        m_audioSource->setBufferSize(4096);
        m_audioSource->setVolume(1.0);

        qDebug() << "[Audio] Created source for:" << device.description();

        // Small delay sometimes helps on Linux when switching devices
        QTimer::singleShot(100, this, [this]() {
            startListening();
        });
    }

    void startListening() {
        if (!m_audioSource) {
            qWarning() << "[Audio] startListening – no source";
            return;
        }

        if (m_audioSource->state() != QAudio::StoppedState) {
            m_audioSource->stop();
        }

        m_audioDevice = m_audioSource->start();

        qDebug() << "[Audio] after start() state:" << m_audioSource->state()
                 << "error:" << m_audioSource->error()
                 << "device:" << m_currentDevice.description()
                 << "volume:" << m_audioSource->volume();

        if (m_audioDevice) {
            connect(m_audioDevice, &QIODevice::readyRead,
                    this, &AudioRecorder::onAudioDataReady, Qt::UniqueConnection);
            qDebug() << "[Audio] readyRead connected, bytesAvailable:" << m_audioDevice->bytesAvailable();
        } else {
            qWarning() << "[Audio] start() returned null device!";
        }
    }

    void startRecording() {
        startListening();
        startBufferingSpeech();
    }

    std::vector<float> stopRecording() {
        std::vector<float> pcmData = stopBufferingSpeech();
        stopListening();
        return pcmData;
    }



    void stopListening() {
        if (m_audioSource) {
            m_audioSource->stop();
            m_audioDevice = nullptr;
        }
    }

    void startBufferingSpeech() {
        QMutexLocker locker(&m_mutex);
        m_isCapturingSpeech = true;
        m_speechBuffer.clear();
        m_recentSamples.clear();
    }

    std::vector<float> stopBufferingSpeech() {
        QMutexLocker locker(&m_mutex);
        m_isCapturingSpeech = false;

        int sampleCount = m_speechBuffer.size() / sizeof(int16_t);
        const auto *samples16 = reinterpret_cast<const int16_t *>(m_speechBuffer.constData());

        std::vector<float> pcm32f(sampleCount);
        for (int i = 0; i < sampleCount; ++i) {
            pcm32f[i] = static_cast<float>(samples16[i]) / 32768.0f;
        }

        m_speechBuffer.clear();
        m_recentSamples.clear();
        return pcm32f;
    }

    std::vector<float> getRecentSamples(size_t maxSamples = 1600) {
        QMutexLocker locker(&m_mutex);
        if (m_recentSamples.empty()) {
            return {};
        }
        if (m_recentSamples.size() <= maxSamples) {
            return m_recentSamples;
        }
        return std::vector<float>(m_recentSamples.end() - maxSamples, m_recentSamples.end());
    }

signals:
    void audioChunkReady(const std::vector<float>& chunk);

private slots:
    void onAudioDataReady() {
        if (!m_audioDevice) return;

        QByteArray rawData = m_audioDevice->readAll();
        if (rawData.isEmpty()) return;

        QMutexLocker locker(&m_mutex);

        if (m_isCapturingSpeech) {
            m_speechBuffer.append(rawData);
        }

        int sampleCount = rawData.size() / sizeof(int16_t);
        const auto *samples16 = reinterpret_cast<const int16_t *>(rawData.constData());

        int16_t maxAbs16 = 0;
        std::vector<float> chunk(sampleCount);
        for (int i = 0; i < sampleCount; ++i) {
            int16_t s = samples16[i];
            int16_t a = s < 0 ? static_cast<int16_t>(-s) : s;
            if (a > maxAbs16) maxAbs16 = a;

            float floatSample = static_cast<float>(s) / 32768.0f;
            chunk[i] = floatSample;
            m_recentSamples.push_back(floatSample);
        }

        const size_t maxRecentBufferSize = 16000;
        if (m_recentSamples.size() > maxRecentBufferSize) {
            m_recentSamples.erase(m_recentSamples.begin(), m_recentSamples.end() - maxRecentBufferSize);
        }


        emit audioChunkReady(chunk);
    }

private:
    QAudioFormat m_format;
    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QAudioDevice m_currentDevice;
    QByteArray m_speechBuffer;
    std::vector<float> m_recentSamples;
    bool m_isCapturingSpeech = false;
    QMutex m_mutex;
};