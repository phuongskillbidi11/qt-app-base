#include "app_log.h"
#include "platform.h"
#include "single_instance.h"
#include "thememanager.h"

#include "demowindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

namespace {

// Qt's own warnings are worth keeping: "QIODevice::read: device not open" and
// "QNetworkReplyImplPrivate::error: ... called once" are how two real defects in the
// source project first became visible, and neither appeared anywhere else.
void qtMessageHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message) {
    Q_UNUSED(context);
    switch (type) {
    case QtWarningMsg:  AppLog::warn("Qt warning: " + message); break;
    case QtCriticalMsg: AppLog::error("Qt critical: " + message); break;
    case QtFatalMsg:    AppLog::error("Qt fatal: " + message); break;
    default: break;
    }
}

}  // namespace

// The startup sequence every application on this base should follow. The order is not
// arbitrary:
//   1. identity first — QSettings and the log path are derived from it
//   2. log second — so everything after it, including failures, is recorded
//   3. resource check third — so a resource-starved launch leaves evidence before
//      anything else has a chance to fail mysteriously because of it
//   4. crash handler fourth — before any code that could fault
//   5. single-instance guard fifth — before building any window, so a second launch
//      costs nothing and cannot fight the first over shared resources
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("iSoft");
    QCoreApplication::setApplicationName("basedemo");
    QSettings::setDefaultFormat(QSettings::IniFormat);

    AppLog::init();

    const Platform::SystemResources resources =
        Platform::checkSystemResources(QFileInfo(AppLog::filePath()).absolutePath());
    AppLog::info(QString("system resources: RAM %1/%2 MB available, disk %3/%4 MB available")
                     .arg(resources.availableRamBytes / (1024 * 1024))
                     .arg(resources.totalRamBytes / (1024 * 1024))
                     .arg(resources.availableDiskBytes / (1024 * 1024))
                     .arg(resources.totalDiskBytes / (1024 * 1024)));
    if (Platform::isResourceLow(resources)) {
        AppLog::warn("system resources are low -- see the line above for exact numbers");
        QMessageBox::warning(nullptr, "Low system resources",
            QString("Available RAM: %1 MB\nAvailable disk: %2 MB\n\n"
                    "The application may run slowly or fail to save data.")
                .arg(resources.availableRamBytes / (1024 * 1024))
                .arg(resources.availableDiskBytes / (1024 * 1024)));
    }

    qInstallMessageHandler(qtMessageHandler);
    Platform::installCrashHandler();

    SingleInstance guard("basedemo-iSoft-singleinstance");
    if (!guard.isPrimary()) {
        const bool raised = guard.signalPrimaryToRaise();
        AppLog::info(raised
            ? "second instance refused; primary window raised"
            : "second instance refused; primary could not be reached");
        if (!raised) {
            QMessageBox::information(nullptr, "Already running",
                "The demo is already running but its window could not be raised.");
        }
        return 0;
    }

    ThemeManager::apply(ThemeManager::restore());
    DemoWindow window;
    window.show();
    QObject::connect(&guard, &SingleInstance::raiseRequested,
                     &window, &DemoWindow::raiseToFront);

    const int code = app.exec();
    AppLog::info(QString("application exited normally; exit code %1").arg(code));
    return code;
}
