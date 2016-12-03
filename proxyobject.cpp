#include "proxyobject.h"
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QString>
#include <QTextStream>
#include <QTcpSocket>
#include <QTimerEvent>
#include <QHostInfo>
using namespace ZPNetwork;
extern quint64 g_bytesRecieved ;
extern quint64 g_bytesSent ;
extern quint64 g_secRecieved ;
extern quint64 g_secSent ;
ProxyObject::ProxyObject(QObject *parent)
	: QObject(parent)
	, engine(new ZPNetwork::zp_net_Engine(2048,this))
{
	connect (engine, &zp_net_Engine::evt_Message, this, &ProxyObject::slot_Message ,Qt::QueuedConnection);
	connect (engine, &zp_net_Engine::evt_SocketError, this, &ProxyObject::slot_SocketError,Qt::QueuedConnection );
	connect (engine, &zp_net_Engine::evt_NewClientConnected, this, &ProxyObject::slot_NewClientConnected,Qt::QueuedConnection );
	connect (engine, &zp_net_Engine::evt_ClientDisconnected, this, &ProxyObject::slot_ClientDisconnected ,Qt::QueuedConnection);
	connect (engine, &zp_net_Engine::evt_Data_recieved, this, &ProxyObject::slot_Data_recieved ,Qt::QueuedConnection);
	initEngine();
	m_nTimerRefresh = startTimer(1000);
}
void ProxyObject::slot_Message(QObject * pSource,QString message )
{
	QString msg = message + tr(",Source=%1").arg((quint64)pSource);
	QTextStream stout(stdout,QIODevice::WriteOnly);
	QDateTime dtm = QDateTime::currentDateTime();
	QString msgOut = dtm.toString("yyyy-MM-dd HH:mm:ss.zzz") + " " + msg;
	stout<<msgOut<<"\n";
}
void ProxyObject::initEngine()
{
	QTextStream stout(stdout,QIODevice::WriteOnly);
	QString inidfile = QCoreApplication::applicationFilePath()+".ini";
	QSettings settings(inidfile,QSettings::IniFormat);
	int nPorts = settings.value("PROXY/Ports",0).toInt();
	stout<<"Reading config from : "<<inidfile<<"\n";
	stout<<"PROXY/Ports = "<<nPorts<<"\n";
	for (int i=0;i<nPorts;++i)
	{
		stout<<"PORTS"<<i<<":\n";
		QString keyPrefix = QString().sprintf("PORT%d",i);
		QString sk = keyPrefix + "/InnerPort";
		int nInnerPort = settings.value(sk,0).toInt();
		stout<<sk<<"="<<nInnerPort<<"\n";

		sk = keyPrefix + "/InnerAddress";
		QString strInnerAddress = settings.value(sk,"").toString();
		stout<<sk<<"="<<strInnerAddress<<"\n";

		sk = keyPrefix + "/OuterPort";
		int nOuterPort = settings.value(sk,0).toInt();
		stout<<sk<<"="<<nOuterPort<<"\n";

		sk = keyPrefix + "/OuterAddress";
		QString strOuterAddress = settings.value(sk,"").toString();
		stout<<sk<<"="<<strOuterAddress<<"\n";

		if (strInnerAddress.length())
			engine->AddListeningAddress(keyPrefix,QHostAddress(strInnerAddress),nInnerPort,false);
		else
			engine->AddListeningAddress(keyPrefix,QHostAddress::Any,nInnerPort,false);

		QHostInfo info = QHostInfo::fromName(strOuterAddress);
		QList<QHostAddress> lstaddr = info.addresses();
		if (lstaddr.size())
		{
			QString outerIP = lstaddr.first().toString();
			stout<<strOuterAddress<<" IP ="<<outerIP<<"\n";
			m_para_IPLocalPort[outerIP]	= nInnerPort;
			m_para_OuterAddress[nInnerPort] = outerIP;
		}
		//m_para_OuterAddress[nInnerPort] = strOuterAddress;
		m_para_OuterPort[nInnerPort] = nOuterPort;
	}
	engine->AddClientTransThreads(4,false);
}

//The socket error message
void ProxyObject::slot_SocketError(QObject * senderSock ,QAbstractSocket::SocketError socketError)
{
	QString msg = tr(",Source=%1, SockError = %2").arg((quint64)senderSock).arg((quint64)socketError);
	qWarning()<<msg;
}

//this event indicates new client connected.
void ProxyObject::slot_NewClientConnected(QObject * clientHandle)
{
	QTcpSocket * sock = qobject_cast<QTcpSocket *> (clientHandle);
	if (sock)
	{
		QString pn = sock->peerName();
		if (pn.length())
		{
			if (m_para_IPLocalPort.contains(pn))
			{
				qDebug()<<"Outer side " << pn<<":"<<sock->peerPort()<<",Local Port="<<sock->localPort()<<" Connected";
				int nLocalPort = m_para_IPLocalPort[pn];
				if (m_pendingInners[nLocalPort].size())
				{
					QObject * innerClient = m_pendingInners[nLocalPort].first();
					m_pendingInners[nLocalPort].pop_front();
					m_hash_Inner2Outer[innerClient] = clientHandle;
					m_hash_Outer2Inner[clientHandle] = innerClient;
					if (penging_data.contains(innerClient))
					{
						while (penging_data[innerClient].empty()==false)
						{
							engine->SendDataToClient(clientHandle,penging_data[innerClient].first());
							penging_data[innerClient].pop_front();
						}
						penging_data.remove(innerClient);
					}
					if (penging_data.contains(clientHandle))
					{
						while (penging_data[clientHandle].empty()==false)
						{
							engine->SendDataToClient(innerClient,penging_data[clientHandle].first());
							penging_data[clientHandle].pop_front();
						}
						penging_data.remove(clientHandle);
					}
				}
				else
				{
					qWarning()<<"Incomming Out connection has no pending local peer. Port="<<nLocalPort;
					//engine->KickClients(clientHandle);
				}

			}
			else
			{
				qWarning()<<"Incomming Out connection "<<pn<<"has no local Port";
				//engine->KickClients(clientHandle);
			}
		}
		else
		{
			int localPort = sock->localPort();
			if (m_para_OuterPort.contains(localPort))
			{
				qDebug()<<"Inner side "<<sock->peerAddress().toString()<<":"<<sock->peerPort()<<",Local Port="<<sock->localPort()<<" Connected";
				m_pendingInners[localPort].push_back(clientHandle);
				engine->connectTo(QHostAddress(m_para_OuterAddress[localPort]),m_para_OuterPort[localPort],false);
			}
			else
			{
				qWarning()<<"Local port "<<localPort<<" Is not valid.";
				engine->KickClients(clientHandle);
			}
		}

	}
}

//this event indicates a client disconnected.
void ProxyObject::slot_ClientDisconnected(QObject * clientHandle)
{
	penging_data.remove(clientHandle);
	m_hash_Inner2Outer.remove(clientHandle);
	m_hash_Outer2Inner.remove(clientHandle);
	foreach (int k , m_pendingInners.keys())
		m_pendingInners[k].removeAll(clientHandle);
}

//some data arrival
void ProxyObject::slot_Data_recieved(QObject *  clientHandle,QByteArray  datablock )
{
	if (m_hash_Inner2Outer.contains(clientHandle))
		engine->SendDataToClient(m_hash_Inner2Outer[clientHandle],datablock);
	else if (m_hash_Outer2Inner.contains(clientHandle))
		engine->SendDataToClient(m_hash_Outer2Inner[clientHandle],datablock);
	else
		penging_data[clientHandle].push_back(datablock);
}

void ProxyObject::timerEvent(QTimerEvent *event)
{
	if (event->timerId()==m_nTimerRefresh)
	{
		static int counter = 0;
		fprintf (stdout,"Send %.2lf MB(%.2lfkbps) Rev %.2lf MB (%.2lfkbps)              \r",
				g_bytesRecieved/1024.0/1024.0,
				g_secRecieved /1024.0*8,
				g_bytesSent/1024.0/1024.0,
				g_secSent/1024.0*8
				);
		g_secRecieved = 0;
		g_secSent = 0;

		if (++counter % 3600 == 0)
		{
			killTimer(m_nTimerRefresh);
			m_nTimerRefresh = -1;
			QTextStream stout(stdout,QIODevice::WriteOnly);
			QString inidfile = QCoreApplication::applicationFilePath()+".ini";
			QSettings settings(inidfile,QSettings::IniFormat);
			int nPorts = settings.value("PROXY/Ports",0).toInt();
			for (int i=0;i<nPorts;++i)
			{
				QString keyPrefix = QString().sprintf("PORT%d",i);
				QString sk = keyPrefix + "/InnerPort";
				int nInnerPort = settings.value(sk,0).toInt();

				sk = keyPrefix + "/OuterPort";
				int nOuterPort = settings.value(sk,0).toInt();

				sk = keyPrefix + "/OuterAddress";
				QString strOuterAddress = settings.value(sk,"").toString();

				QHostInfo info = QHostInfo::fromName(strOuterAddress);
				QList<QHostAddress> lstaddr = info.addresses();
				if (lstaddr.size())
				{
					QString outerIP = lstaddr.first().toString();
					m_para_IPLocalPort[outerIP]	= nInnerPort;
					m_para_OuterAddress[nInnerPort] = outerIP;
					stout<<strOuterAddress<<" IP ="<<outerIP<<"\n";
				}
				//m_para_OuterAddress[nInnerPort] = strOuterAddress;
				m_para_OuterPort[nInnerPort] = nOuterPort;
			}
			m_nTimerRefresh = startTimer(1000);
		}

	}
}
