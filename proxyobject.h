#ifndef PROXYOBJECT_H
#define PROXYOBJECT_H

#include <QObject>
#include <QList>
#include <QHash>
#include <QMap>
#include "network/zp_net_threadpool.h"

class ProxyObject : public QObject
{
	Q_OBJECT
public:
	explicit ProxyObject(QObject *parent = 0);
	void initEngine();
protected:
	void timerEvent(QTimerEvent *event);
private:
	//Ports
	QMap<int, int> m_para_OuterPort;
	QMap<int, QString> m_para_OuterAddress;
	QMap<QString,int> m_para_IPLocalPort;

private:
	int m_nTimerRefresh = -1;
	ZPNetwork::zp_net_Engine * engine;
	QHash<QObject *, QObject *> m_hash_Inner2Outer;
	QHash<QObject *, QObject *> m_hash_Outer2Inner;
	QHash<QObject *, QList< QByteArray > > penging_data;
public slots:
	void slot_Message(QObject * pSource,QString );
	//The socket error message
	void slot_SocketError(QObject * senderSock ,QAbstractSocket::SocketError socketError,quint64);
	//this event indicates new client connected.
	void slot_NewClientConnected(QObject * /*clientHandle*/,quint64);
	//this event indicates a client disconnected.
	void slot_ClientDisconnected(QObject * /*clientHandle*/,quint64);
	//some data arrival
	void slot_Data_recieved(QObject *  /*clientHandle*/,QByteArray  /*datablock*/ ,quint64);
};

#endif // PROXYOBJECT_H
