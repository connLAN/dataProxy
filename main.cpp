#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>
#include "logger/st_logger.h"
#include "proxyobject.h"

STMsgLogger::st_logger g_logger;
QTextStream stream(stdout,QIODevice::WriteOnly);
void stMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	g_logger.MessageOutput(type,context,msg);

	QDateTime dtm = QDateTime::currentDateTime();
	QString msgOut = dtm.toString("yyyy-MM-dd HH:mm:ss.zzz") + ">" + msg;
	stream << msgOut <<"\n";
	stream.flush();
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
