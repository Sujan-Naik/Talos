#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <thread>
#include "whisper.h"

class WhisperTranscriber : public QObject {
Q_OBJECT
public:
    explicit WhisperTranscriber(const QString &modelPath, QObject *parent = nullptr) : QObject(parent) {
        whisper_context_params cparams = whisper_context_default_params();
        m_ctx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);
    }

    ~WhisperTranscriber() {
        if (m_ctx) {
            whisper_free(m_ctx);
        }
    }

    bool isLoaded() const { return m_ctx != nullptr; }

    QString transcribe(const std::vector<float> &pcm32f) {
        if (!m_ctx || pcm32f.empty()) {
            return QString();
        }

        whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.print_progress = false;
        params.print_special = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        params.translate = false;
        params.language = "en";
        params.n_threads = 4;

        if (whisper_full(m_ctx, params, pcm32f.data(), pcm32f.size()) != 0) {
            return QString();
        }

        QString resultText;
        int n_segments = whisper_full_n_segments(m_ctx);
        for (int i = 0; i < n_segments; ++i) {
            const char *text = whisper_full_get_segment_text(m_ctx, i);
            resultText += QString::fromUtf8(text);
        }

        return resultText.trimmed();
    }

    void transcribeAsync(const std::vector<float> &pcm32f) {
        std::thread([this, pcm32f]() {
            QString resultText = transcribe(pcm32f);
            emit transcriptionFinished(resultText);
        }).detach();
    }

signals:
    void transcriptionFinished(const QString &text);

private:
    whisper_context *m_ctx = nullptr;
};