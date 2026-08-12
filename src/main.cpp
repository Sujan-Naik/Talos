#include <QApplication>
#include <QWebEngineView>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu-compositing --disable-gpu-sandbox --enable-begin-frame-scheduling");
    qputenv("QTWEBENGINE_REMOTE_DEBUGGING", "9223");
    QApplication app(argc, argv);

    // Allow external resources from CDN
    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    QWebEngineSettings *settings = profile->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    MainWindow w;
    w.show();

    return app.exec();
}