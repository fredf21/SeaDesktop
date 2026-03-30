#include "mainwindow.h"
#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QProcess>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QProcess* backend = new QProcess(&a);

    // Logs stdout du backend → console Qt Creator
    QObject::connect(backend, &QProcess::readyReadStandardOutput, [backend]() {
        qDebug() << "[backend]" << backend->readAllStandardOutput();
    });

    // Logs stderr du backend → console Qt Creator
    QObject::connect(backend, &QProcess::readyReadStandardError, [backend]() {
        qWarning() << "[backend err]" << backend->readAllStandardError();
    });

    // Log quand le backend démarre
    QObject::connect(backend, &QProcess::started, []() {
        qDebug() << "[backend] started successfully";
    });

    // Log si le backend plante
    QObject::connect(backend, &QProcess::errorOccurred, [](QProcess::ProcessError err) {
        qCritical() << "[backend] error:" << err;
    });

    // Log quand le backend se termine
    QObject::connect(backend, &QProcess::finished, [](int code, QProcess::ExitStatus status) {
        qDebug() << "[backend] finished, exit code:" << code
                 << (status == QProcess::CrashExit ? "(crashed)" : "(normal)");
    });

    backend->start(
        "../Backend_Seastar/backend_seastar",
        {"--smp", "1", "--memory", "512M"}
        );

    if (!backend->waitForStarted(3000)) {
        qCritical() << "[backend] failed to start:" << backend->errorString();
    }

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "SeaUI_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    MainWindow w;
    w.show();

    int result = a.exec();

    backend->terminate();
    backend->waitForFinished();

    return result;
}