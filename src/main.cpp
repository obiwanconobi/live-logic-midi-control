#include "mainwindow.h"
#include "widgets.h"

#include <QApplication>
#include <QIcon>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Live Logic MIDI Control"));
    QApplication::setApplicationDisplayName(
        QStringLiteral("Live Logic MIDI Control"));
    QApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Live Logic Linux"));
    QApplication::setDesktopFileName(QStringLiteral("live-logic-control"));

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/live-logic-control.png")));
    app.setStyleSheet(appStyleSheet());

    MainWindow window;
    const QStringList arguments = app.arguments();
    window.show();

    // Development helper: render the window to a PNG and exit. Useful for
    // documentation and for checking the UI on headless machines.
    const int screenshotIndex =
        arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        window.resize(1320, 920);
        const QString path = arguments.at(screenshotIndex + 1);
        QTimer::singleShot(700, &app, [&window, path] {
            const bool saved = window.grab().save(path);
            if (!saved)
                std::fprintf(stderr, "Could not save screenshot to %s\n",
                             qPrintable(path));
            QCoreApplication::exit(0);
        });
    }

    return app.exec();
}