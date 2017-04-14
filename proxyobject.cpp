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
void ProxyObject::slot_SocketError(QObject * senderSock ,QAbstractSocket::SocketError socketError,quint64)
{
	QString msg = tr(",Source=%1, SockError = %2").arg((quint64)senderSock).arg((quint64)socketError);
	qWarning()<<msg;
	if (m_hash_Inner2Outer.contains(senderSock))
	{
        //engine->KickClients(m_hash_Inner2Outer[senderSock]);
        //!In some case, the sender has a fast connection, while the reciever is slow.
        //!Because of the deep cache method in engine, the sender will cut off tcp connection when
        //!It "feels" that sending operation is finished. However, at the time sender closed connection,
        //! the other side is still busy recieveing data. In this case, recieving progress will be
        //! terminate abnormally.
        //! The solution blew can remember the reciever's sock, with a time stamp
        //! In ontimer() function, we will check timestamp and disconnect reciever.
        pending_kick[m_hash_Inner2Outer[senderSock]] = QDateTime::currentDateTime();
		m_hash_Inner2Outer.remove(senderSock);
	}
	else if (m_hash_Outer2Inner.contains(senderSock))
	{
        //engine->KickClients(m_hash_Outer2Inner[senderSock]);
        pending_kick[m_hash_Inner2Outer[senderSock]] = QDateTime::currentDateTime();
        m_hash_Outer2Inner.remove(senderSock);
	}
}

//this event indicates new client connected.
void ProxyObject::slot_NewClientConnected(QObject * clientHandle,quint64 extraData)
{
	QTcpSocket * sock = qobject_cast<QTcpSocket *> (clientHandle);
	if (sock)
	{
		QString pn = sock->peerName();
		if (extraData)
		{
			if (m_para_IPLocalPort.contains(pn))
			{
				qDebug()<<"Outer side " << pn<<":"<<sock->peerPort()<<",Local Port="<<sock->localPort()<<" Connected";
				int nLocalPort = m_para_IPLocalPort[pn];
				QObject * innerClient = reinterpret_cast<QObject *> (extraData);
				if (innerClient)
				{
					m_hash_Inner2Outer[innerClient] = clientHandle;
					m_hash_Outer2Inner[clientHandle] = innerClient;
                    if (pending_data.contains(innerClient))
					{
                        while (pending_data[innerClient].empty()==false)
						{
                            engine->SendDataToClient(clientHandle,pending_data[innerClient].first());
                            pending_data[innerClient].pop_front();
						}
                        pending_data.remove(innerClient);
					}
                    if (pending_data.contains(clientHandle))
					{
                        while (pending_data[clientHandle].empty()==false)
						{
                            engine->SendDataToClient(innerClient,pending_data[clientHandle].first());
                            pending_data[clientHandle].pop_front();
						}
                        pending_data.remove(clientHandle);
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
				engine->connectTo(QHostAddress(m_para_OuterAddress[localPort]),m_para_OuterPort[localPort],false,reinterpret_cast<quint64>(sock));
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
void ProxyObject::slot_ClientDisconnected(QObject * clientHandle,quint64)
{
    pending_data.remove(clientHandle);
	if (m_hash_Inner2Outer.contains(clientHandle))
	{
        //engine->KickClients(m_hash_Inner2Outer[clientHandle]);
        //!In some case, the sender has a fast connection, while the reciever is slow.
        //!Because of the deep cache method in engine, the sender will cut off tcp connection when
        //!It "feels" that sending operation is finished. However, at the time sender closed connection,
        //! the other side is still busy recieveing data. In this case, recieving progress will be
        //! terminate abnormally.
        //! The solution blew can remember the reciever's sock, with a time stamp
        //! In ontimer() function, we will check timestamp and disconnect reciever.
        pending_kick[m_hash_Inner2Outer[clientHandle]] = QDateTime::currentDateTime();
		m_hash_Inner2Outer.remove(clientHandle);
	}
	else if (m_hash_Outer2Inner.contains(clientHandle))
	{
        //engine->KickClients(m_hash_Outer2Inner[clientHandle]);
        pending_kick[m_hash_Outer2Inner[clientHandle]] = QDateTime::currentDateTime();
		m_hash_Outer2Inner.remove(clientHandle);
	}
}

//some data arrival
void ProxyObject::slot_Data_recieved(QObject *  clientHandle,QByteArray  datablock,quint64 )
{
	if (m_hash_Inner2Outer.contains(clientHandle))
		engine->SendDataToClient(m_hash_Inner2Outer[clientHandle],datablock);
	else if (m_hash_Outer2Inner.contains(clientHandle))
		engine->SendDataToClient(m_hash_Outer2Inner[clientHandle],datablock);
    else if (pending_kick.contains(clientHandle)==false)
        pending_data[clientHandle].push_back(datablock);
    //Keep timestamp fresh
    if (pending_kick.contains(clientHandle))
        pending_kick[clientHandle] = QDateTime::currentDateTime();
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
        //kick out clients when the other side is disconnected and
        //no data left to be recieved.
        if (counter % 30 ==0)
        {
            QList<QObject *> timeoutobjs;
            foreach(QObject * obj, pending_kick.keys())
                if (pending_kick[obj].secsTo(QDateTime::currentDateTime())>30)
                    timeoutobjs.push_back(obj);
            foreach(QObject * obj, pending_kick.keys())
            {
                pending_kick.remove(obj);
                engine->KickClients(obj);
            }

        }

	}
}
