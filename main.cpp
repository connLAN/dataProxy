#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>
#include "logger/st_logger.h"
#include "proxyobject.h"

STMsgLogger::st_logger g_logger;
void stMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	g_logger.MessageOutput(type,context,msg);
}

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);
	//Install message handler
	qInstallMessageHandler(stMessageOutput);

	qDebug()<<"Data Proxy Starting.";

	ProxyObject * proxy = new ProxyObject(&a);

	qDebug()<<"Data Proxy Started.";

	return a.exec();
}
