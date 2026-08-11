#pragma once

#include <QObject>
#include <QByteArray>
#include <QBuffer>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioFormat>
#include <vector>

class AudioRecorder : public QObject {
Q_OBJECT
public:
    explicit AudioRecorder(QObject *parent = nullptr) : QObject(parent) {
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioDevice info = QMediaDevices::defaultAudioInput();
        m_audioSource = new QAudioSource(info, format, this);
    }

    void startRecording() {
        m_audioBuffer.clear();
        m_bufferDevice.open(QIODevice::WriteOnly | QIODevice::Truncate);
        m_audioSource->start(&m_bufferDevice);
    }

    std::vector<float> stopRecording() {
        m_audioSource->stop();
        m_bufferDevice.close();

        QByteArray rawData = m_bufferDevice.buffer();
        int sampleCount = rawData.size() / sizeof(int16_t);
        const auto *samples16 = reinterpret_cast<const int16_t *>(rawData.constData());

        std::vector<float> pcm32f(sampleCount);
        for (int i = 0; i < sampleCount; ++i) {
            pcm32f[i] = static_cast<float>(samples16[i]) / 32768.0f;
        }
        return pcm32f;
    }

private:
    QAudioSource *m_audioSource = nullptr;
    QBuffer m_bufferDevice;
    QByteArray m_audioBuffer;
};