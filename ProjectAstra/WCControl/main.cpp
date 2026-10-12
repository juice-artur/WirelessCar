#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WCControl"));
    QCoreApplication::setApplicationName(QStringLiteral("WCControl"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("WCControl", "Main");

    return QGuiApplication::exec();
}
