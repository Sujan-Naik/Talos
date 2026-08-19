#include <QApplication>
#include <QWebEngineView>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--no-sandbox "
            "--disable-gpu-sandbox "
            "--disable-gpu-compositing "
            "--disable-seccomp-filter-sandbox");
    qputenv("QTWEBENGINE_REMOTE_DEBUGGING", "9223");

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    int fakeArgc = argc + 1;
    char** fakeArgv = new char*[fakeArgc];
    for (int i = 0; i < argc; ++i) {
        fakeArgv[i] = argv[i];
    }
    char noSandboxFlag[] = "--no-sandbox";
    fakeArgv[argc] = noSandboxFlag;

    QApplication app(fakeArgc, fakeArgv);

    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    QWebEngineSettings *settings = profile->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    MainWindow w;
    w.show();

    int result = app.exec();
    delete[] fakeArgv;
    return result;
}