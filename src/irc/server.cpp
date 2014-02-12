// -*- mode: c++; c-file-style: "bsd"; c-basic-offset: 4; tabs-width: 4; indent-tabs-mode: nil -*-

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2005 Ismail Donmez <ismail@kde.org>
  Copyright (C) 2005-2006 Peter Simonsson <psn@linux.se>
  Copyright (C) 2006-2008 Eli J. MacKenzie <argonel at gmail.com>
  Copyright (C) 2005-2008 Eike Hein <hein@kde.org>
*/

#include "server.h"
#include "ircqueue.h"
#include "query.h"
#include "channel.h"
#include "application.h"
#include "connectionmanager.h"
#include "dcccommon.h"
#include "dccfiledialog.h"
#include "transfermanager.h"
#include "transfersend.h"
#include "transferrecv.h"
#include "chat.h"
#include "recipientdialog.h"
#include "irccharsets.h"
#include "viewcontainer.h"
#include "rawlog.h"
#include "channellistpanel.h"
#include "scriptlauncher.h"
#include "notificationhandler.h"
#include "awaymanager.h"
#include "ircinput.h"

#include <QTextCodec>
#include <QStringListModel>
#include <QStringBuilder>

#include <KInputDialog>
#include <KWindowSystem>
#include <KShell>

#include <solid/networking.h>

#include <kio/sslui.h>

using namespace Konversation;

int Server::m_availableConnectionId = 0;

Server::Server(QObject* parent, ConnectionSettings& settings) : QObject(parent)
{
    m_connectionId = m_availableConnectionId;
    m_availableConnectionId++;

    setConnectionSettings(settings);

    m_connectionState = Konversation::SSNeverConnected;

    m_recreationScheduled = false;

    m_delayedConnectTimer = new QTimer(this);
    m_delayedConnectTimer->setSingleShot(true);
    connect(m_delayedConnectTimer, SIGNAL(timeout()), this, SLOT(connectToIRCServer()));

    m_reconnectImmediately = false;

    for (int i=0; i <= Application::instance()->countOfQueues(); i++)
    {
        //QList<int> r=Preferences::queueRate(i);
        IRCQueue *q=new IRCQueue(this, Application::instance()->staticrates[i]); //FIXME these are supposed to be in the rc
        m_queues.append(q);
    }

    m_processingIncoming = false;
    m_identifyMsg = false;
    m_capRequested = false;
    m_capAnswered = false;
    m_autoJoin = false;

    m_nickIndices.clear();
    m_nickIndices.append(0);

    m_nickListModel = new QStringListModel(this);

    Application* konvApp = static_cast<Application*>(kapp);
    m_nickListModel2 = konvApp->getConnectionManager()->getNickListModel();
    m_nickListModel2->addServer(connectionId());

    m_currentLag = -1;
    m_rawLog = 0;
    m_channelListPanel = 0;
    m_away = false;
    m_socket = 0;
    m_prevISONList = QStringList();
    m_bytesReceived = 0;
    m_encodedBytesSent=0;
    m_bytesSent=0;
    m_linesSent=0;
    // TODO fold these into a QMAP, and these need to be reset to RFC values if this server object is reused.
    m_serverNickPrefixModes = "ovh";
    m_serverNickPrefixes = "@+%";
    m_banAddressListModes = 'b'; // {RFC-1459, draft-brocklesby-irc-isupport} -> pick one
    m_channelPrefixes = "#&";
    m_modesCount = 3;
    m_sslErrorLock = false;
    m_topicLength = -1;

    setObjectName(QString::fromLatin1("server_") + m_connectionSettings.name());

    setNickname(m_connectionSettings.initialNick());
    //obtainNickInfo(getNickname()); FIXME was this how our nick got added to the nicklistmodel before joining a channel? if so replace with equiv.

    m_statusView = getViewContainer()->addStatusView(this);

    if (Preferences::self()->rawLog())
        addRawLog(false);

    m_inputFilter.setServer(this);
    m_outputFilter = new Konversation::OutputFilter(this);

    // For /msg query completion
    m_completeQueryPosition = 0;

    updateAutoJoin(m_connectionSettings.oneShotChannelList());

    if (!getIdentity()->getShellCommand().isEmpty())
        QTimer::singleShot(0, this, SLOT(doPreShellCommand()));
    else
        QTimer::singleShot(0, this, SLOT(connectToIRCServer()));

    initTimers();

    if (getIdentity()->getShellCommand().isEmpty())
        connectSignals();
    // TODO FIXME this disappeared in a merge, ensure it should have
    updateConnectionState(Konversation::SSNeverConnected);
}

Server::~Server()
{
    m_nickListModel2->removeServer(connectionId());
    //send queued messages
    kDebug() << "Server::~Server(" << getServerName() << ")";

    // clear nicks online
    emit nicksNowOnline(this,QStringList(),true);

    // Make sure no signals get sent to a soon to be dying Server Window
    if (m_socket)
    {
        m_socket->blockSignals(true);
        m_socket->deleteLater();
    }

    delete m_statusView;

    closeRawLog();
    closeChannelListPanel();

    if (m_recreationScheduled)
    {
        Konversation::ChannelList channelList;

        foreach (Channel* channel, m_channelList)
        {
            channelList << channel->channelSettings();
        }

        m_connectionSettings.setOneShotChannelList(channelList);
    }

    qDeleteAll(m_channelList);
    m_channelList.clear();
    m_loweredChannelNameHash.clear();

    qDeleteAll(m_queryList);
    m_queryList.clear();

    purgeData();

    //Delete the queues
    qDeleteAll(m_queues);

    emit destroyed(m_connectionId);

    if (m_recreationScheduled)
    {
        qRegisterMetaType<ConnectionSettings>("ConnectionSettings");
        qRegisterMetaType<Konversation::ConnectionFlag>("Konversation::ConnectionFlag");

        Application* konvApp = static_cast<Application*>(kapp);

        QMetaObject::invokeMethod(konvApp->getConnectionManager(), "connectTo", Qt::QueuedConnection,
            Q_ARG(Konversation::ConnectionFlag, Konversation::CreateNewConnection),
            Q_ARG(ConnectionSettings, m_connectionSettings));
    }

    kDebug() << "~Server done";
}

void Server::purgeData()
{
}

//... so called to match the ChatWindow derivatives.
bool Server::closeYourself(bool askForConfirmation)
{
    m_statusView->closeYourself(askForConfirmation);

    return true;
}

void Server::cycle()
{
    m_recreationScheduled = true;

    m_statusView->closeYourself();
}

void Server::doPreShellCommand()
{
    KShell::Errors e;
    QStringList command = KShell::splitArgs(getIdentity()->getShellCommand(), KShell::TildeExpand, &e);
    if (e != KShell::NoError)
    {
        //FIXME The flow needs to be refactored, add a finally-like method that does the ready-to-connect stuff
        // "The pre-connect shell command could not be understood!");
        preShellCommandExited(m_preShellCommand.exitCode(), m_preShellCommand.exitStatus());
    }
    else
    {
        // FIXME add i18n, and in preShellCommandExited and preShellCommandError
        getStatusView()->appendServerMessage(i18n("Info"), i18nc("The command mentioned is executed in a shell prior to connecting.", "Running pre-connect shell command..."));

        connect(&m_preShellCommand, SIGNAL(finished(int,QProcess::ExitStatus)), SLOT(preShellCommandExited(int,QProcess::ExitStatus)));
        connect(&m_preShellCommand, SIGNAL(error(QProcess::ProcessError)), SLOT(preShellCommandError(QProcess::ProcessError)));

        m_preShellCommand.setProgram(command);
        m_preShellCommand.start();
        // NOTE: isConnecting is tested in connectToIRCServer so there's no guard here
        if (m_preShellCommand.state() == QProcess::NotRunning)
            preShellCommandExited(m_preShellCommand.exitCode(), m_preShellCommand.exitStatus());
    }
}

void Server::initTimers()
{
    m_incomingTimer.setObjectName("incoming_timer");
    m_pingSendTimer.setSingleShot(true);
}

void Server::connectSignals()
{
    // Timers
    connect(&m_incomingTimer, SIGNAL(timeout()), this, SLOT(processIncomingData()));
    connect(&m_pingResponseTimer, SIGNAL(timeout()), this, SLOT(updateLongPongLag()));
    connect(&m_pingSendTimer, SIGNAL(timeout()), this, SLOT(sendPing()));

    // OutputFilter
    connect(getOutputFilter(), SIGNAL(requestDccSend()), this,SLOT(requestDccSend()), Qt::QueuedConnection);
    connect(getOutputFilter(), SIGNAL(requestDccSend(QString)), this, SLOT(requestDccSend(QString)), Qt::QueuedConnection);
    connect(getOutputFilter(), SIGNAL(multiServerCommand(QString,QString)),
        this, SLOT(sendMultiServerCommand(QString,QString)));
    connect(getOutputFilter(), SIGNAL(reconnectServer(QString)), this, SLOT(reconnectServer(QString)));
    connect(getOutputFilter(), SIGNAL(disconnectServer(QString)), this, SLOT(disconnectServer(QString)));
    connect(getOutputFilter(), SIGNAL(quitServer(QString)), this, SLOT(quitServer(QString)));
    connect(getOutputFilter(), SIGNAL(openDccSend(QString,KUrl)), this, SLOT(addDccSend(QString,KUrl)), Qt::QueuedConnection);
    connect(getOutputFilter(), SIGNAL(openDccChat(QString)), this, SLOT(openDccChat(QString)), Qt::QueuedConnection);
    connect(getOutputFilter(), SIGNAL(openDccWBoard(QString)), this, SLOT(openDccWBoard(QString)), Qt::QueuedConnection);
    connect(getOutputFilter(), SIGNAL(acceptDccGet(QString,QString)),
        this, SLOT(acceptDccGet(QString,QString)));
    connect(getOutputFilter(), SIGNAL(sendToAllChannels(QString)), this, SLOT(sendToAllChannels(QString)));
    connect(getOutputFilter(), SIGNAL(banUsers(QStringList,QString,QString)),
        this, SLOT(requestBan(QStringList,QString,QString)));
    connect(getOutputFilter(), SIGNAL(unbanUsers(QString,QString)),
        this, SLOT(requestUnban(QString,QString)));
    connect(getOutputFilter(), SIGNAL(openRawLog(bool)), this, SLOT(addRawLog(bool)));
    connect(getOutputFilter(), SIGNAL(closeRawLog()), this, SLOT(closeRawLog()));
    connect(getOutputFilter(), SIGNAL(encodingChanged()), this, SLOT(updateEncoding()));

    Application* konvApp = static_cast<Application*>(kapp);
    connect(getOutputFilter(), SIGNAL(connectTo(Konversation::ConnectionFlag,QString,QString,QString,QString,QString,bool)),
         konvApp->getConnectionManager(), SLOT(connectTo(Konversation::ConnectionFlag,QString,QString,QString,QString,QString,bool)));
    connect(konvApp->getDccTransferManager(), SIGNAL(newDccTransferQueued(Konversation::DCC::Transfer*)),
            this, SLOT(slotNewDccTransferItemQueued(Konversation::DCC::Transfer*)));

   // ViewContainer
    connect(this, SIGNAL(showView(ChatWindow*)), getViewContainer(), SLOT(showView(ChatWindow*)));
    connect(this, SIGNAL(addDccPanel()), getViewContainer(), SLOT(addDccPanel()));
    connect(this, SIGNAL(addDccChat(Konversation::DCC::Chat*)),
            getViewContainer(), SLOT(addDccChat(Konversation::DCC::Chat*)), Qt::QueuedConnection);
    connect(this, SIGNAL(serverLag(Server*,int)), getViewContainer(), SIGNAL(updateStatusBarLagLabel(Server*,int)));
    connect(this, SIGNAL(tooLongLag(Server*,int)), getViewContainer(), SIGNAL(setStatusBarLagLabelTooLongLag(Server*,int)));
    connect(this, SIGNAL(resetLag(Server*)), getViewContainer(), SIGNAL(resetStatusBarLagLabel(Server*)));
    connect(getOutputFilter(), SIGNAL(showView(ChatWindow*)), getViewContainer(), SLOT(showView(ChatWindow*)));
    connect(getOutputFilter(), SIGNAL(openKonsolePanel()), getViewContainer(), SLOT(addKonsolePanel()));
    connect(getOutputFilter(), SIGNAL(openChannelList(QString)), this, SLOT(requestOpenChannelListPanel(QString)));
    connect(getOutputFilter(), SIGNAL(closeDccPanel()), getViewContainer(), SLOT(closeDccPanel()));
    connect(getOutputFilter(), SIGNAL(addDccPanel()), getViewContainer(), SLOT(addDccPanel()));

    // Inputfilter - queued connections should be used for slots that have blocking UI
    connect(&m_inputFilter, SIGNAL(addDccChat(QString,QStringList)),
            this, SLOT(addDccChat(QString,QStringList)), Qt::QueuedConnection);
    connect(&m_inputFilter, SIGNAL(rejectDccChat(QString)),
            this, SLOT(rejectDccChat(QString)));
    connect(&m_inputFilter, SIGNAL(startReverseDccChat(QString,QStringList)),
            this, SLOT(startReverseDccChat(QString,QStringList)));
    connect(&m_inputFilter, SIGNAL(welcome(QString)), this, SLOT(capCheckIgnored()));
    connect(&m_inputFilter, SIGNAL(welcome(QString)), this, SLOT(connectionEstablished(QString)));
    connect(&m_inputFilter, SIGNAL(startReverseDccSendTransfer(QString,QStringList)),
        this, SLOT(startReverseDccSendTransfer(QString,QStringList)));
    connect(&m_inputFilter, SIGNAL(addDccGet(QString,QStringList)),
            this, SLOT(addDccGet(QString,QStringList)), Qt::QueuedConnection);
    connect(&m_inputFilter, SIGNAL(resumeDccGetTransfer(QString,QStringList)),
        this, SLOT(resumeDccGetTransfer(QString,QStringList)));
    connect(&m_inputFilter, SIGNAL(resumeDccSendTransfer(QString,QStringList)),
        this, SLOT(resumeDccSendTransfer(QString,QStringList)));
    connect(&m_inputFilter, SIGNAL(rejectDccSendTransfer(QString,QStringList)),
        this, SLOT(rejectDccSendTransfer(QString,QStringList)));
    connect(&m_inputFilter, SIGNAL(userhost(QString,QString,bool,bool)),
        this, SLOT(userhost(QString,QString,bool,bool)) );
    connect(&m_inputFilter, SIGNAL(topicAuthor(QString,QString,QDateTime)),
        this, SLOT(setTopicAuthor(QString,QString,QDateTime)) );
    connect(&m_inputFilter, SIGNAL(endOfWho(QString)),
        this, SLOT(endOfWho(QString)) );
    connect(&m_inputFilter, SIGNAL(endOfNames(QString)),
        this, SLOT(endOfNames(QString)) );
    connect(&m_inputFilter, SIGNAL(invitation(QString,QString)),
        this,SLOT(invitation(QString,QString)) );
    connect(&m_inputFilter, SIGNAL(addToChannelList(QString,int,QString)),
        this, SLOT(addToChannelList(QString,int,QString)));

    // Status View
    connect(this, SIGNAL(serverOnline(bool)), getStatusView(), SLOT(serverOnline(bool)));

        // Scripts
    connect(getOutputFilter(), SIGNAL(launchScript(int,QString,QString)),
        konvApp->getScriptLauncher(), SLOT(launchScript(int,QString,QString)));
    connect(konvApp->getScriptLauncher(), SIGNAL(scriptNotFound(QString)),
        this, SLOT(scriptNotFound(QString)));
    connect(konvApp->getScriptLauncher(), SIGNAL(scriptExecutionError(QString)),
        this, SLOT(scriptExecutionError(QString)));

    // Stats
    connect(this, SIGNAL(sentStat(int,int)), SLOT(collectStats(int,int)));
}

int Server::getPort()
{
    return getConnectionSettings().server().port();
}

int Server::getLag()  const
{
    return m_currentLag;
}

bool Server::getAutoJoin()  const
{
    return m_autoJoin;
}

void Server::setAutoJoin(bool on)
{
    m_autoJoin = on;
}

void Server::preShellCommandExited(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    if (exitStatus == QProcess::NormalExit)
        getStatusView()->appendServerMessage(i18n("Info"), i18n("Pre-shell command executed successfully!"));
    else
    {
        QString errorText = i18nc("An error message from KDE or Qt is appended.", "There was a problem while executing the command: ") % m_preShellCommand.errorString();
        getStatusView()->appendServerMessage(i18n("Warning"), errorText);
    }

    connectToIRCServer();
    connectSignals();
}

void Server::preShellCommandError(QProcess::ProcessError error)
{
    Q_UNUSED(error);

    QString errorText = i18nc("An error message from KDE or Qt is appended.", "There was a problem while executing the command: ") % m_preShellCommand.errorString();
    getStatusView()->appendServerMessage(i18n("Warning"), errorText);

    connectToIRCServer();
    connectSignals();
}

void Server::connectToIRCServer()
{
    if (!isConnected() && !isConnecting())
    {
        if (m_sslErrorLock)
        {
            kDebug() << "Refusing to connect while SSL lock from previous connection attempt is being held.";

            return;
        }

        // Reenable check when it works reliably for all backends
//         if(Solid::Networking::status() != Solid::Networking::Connected)
//         {
//             updateConnectionState(Konversation::SSInvoluntarilyDisconnected);
//             return;
//         }

        updateConnectionState(Konversation::SSConnecting);

        m_ownIpByUserhost.clear();

        resetQueues();

        // This is needed to support server groups with mixed SSL and nonSSL servers
        delete m_socket;
        m_socket = 0;
        if (m_referenceNicklist != getIdentity()->getNicknameList())
            m_nickListModel->setStringList(getIdentity()->getNicknameList());
        resetNickSelection();

        m_socket = new KTcpSocket();
        m_socket->setObjectName("serverSocket");

        connect(m_socket, SIGNAL(error(KTcpSocket::Error)), SLOT(broken(KTcpSocket::Error)) );
        connect(m_socket, SIGNAL(readyRead()), SLOT(incoming()));
        connect(m_socket, SIGNAL(disconnected()), SLOT(closed()));

        connect(m_socket, SIGNAL(hostFound()), SLOT (hostFound()));

        getStatusView()->appendServerMessage(i18n("Info"),i18n("Looking for server %1 (port <numid>%2</numid>)...",
            getConnectionSettings().server().host(),
            QString::number(getConnectionSettings().server().port())));

        // connect() will do a async lookup too
        if(getConnectionSettings().server().SSLEnabled() || getIdentity()->getAuthType() == "pemclientcert")
        {
            connect(m_socket, SIGNAL(encrypted()), SLOT (socketConnected()));
            connect(m_socket, SIGNAL(sslErrors(QList<KSslError>)), SLOT(sslError(QList<KSslError>)));

            if (getIdentity()->getAuthType() == "pemclientcert")
            {
                m_socket->setLocalCertificate(getIdentity()->getPemClientCertFile().toLocalFile());
                m_socket->setPrivateKey(getIdentity()->getPemClientCertFile().toLocalFile());
            }

            m_socket->setAdvertisedSslVersion(KTcpSocket::TlsV1);

            m_socket->connectToHostEncrypted(getConnectionSettings().server().host(), getConnectionSettings().server().port());
        }
        else
        {
            connect(m_socket, SIGNAL(connected()), SLOT (socketConnected()));
            m_socket->connectToHost(getConnectionSettings().server().host(), getConnectionSettings().server().port());
        }

        // set up the connection details
        setPrefixes(m_serverNickPrefixModes, m_serverNickPrefixes);
        // reset InputFilter (auto request info, /WHO request info)
        m_inputFilter.reset();
    }
    else
        kDebug() << "connectToIRCServer() called while already connected: This should never happen. (" << (isConnecting() << 1) + isConnected() << ')';
}

void Server::connectToIRCServerIn(uint delay)
{
    m_delayedConnectTimer->setInterval(delay * 1000);
    m_delayedConnectTimer->start();

    updateConnectionState(Konversation::SSScheduledToConnect);
}

void Server::showSSLDialog()
{
        //TODO
        /*
          SSLSocket* sslsocket = dynamic_cast<SSLSocket*>(m_socket);

          if (sslsocket) sslsocket->showInfoDialog();
        */
}

// set available channel types according to 005 RPL_ISUPPORT
void Server::setChannelTypes(const QString &pre)
{
    m_channelPrefixes = pre;
}

QString Server::getChannelTypes() const
{
    return m_channelPrefixes;
}

// set max number of channel modes with parameter according to 005 RPL_ISUPPORT
void Server::setModesCount(int count)
{
    m_modesCount = count;
}

int Server::getModesCount()
{
    return m_modesCount;
}

// set user mode prefixes according to non-standard 005-Reply (see inputfilter.cpp)
void Server::setPrefixes(const QString &modes, const QString& prefixes)
{
    // NOTE: serverModes is QString::null, if server did not supply the
    // modes which relates to the network's nick-prefixes
    m_serverNickPrefixModes = modes;
    m_serverNickPrefixes = prefixes;
}

void Server::setChanModes(const QString& modes)
{
    QStringList abcd = modes.split(',');
    m_banAddressListModes = abcd.value(0);
}

// return a nickname without possible mode character at the beginning
void Server::mangleNicknameWithModes(QString& nickname,bool& isAdmin,bool& isOwner,
bool& isOp,bool& isHalfop,bool& hasVoice)
{
    isAdmin = false;
    isOwner = false;
    isOp = false;
    isHalfop = false;
    hasVoice = false;

    int modeIndex;

    if (nickname.isEmpty()) return;

    while ((modeIndex = m_serverNickPrefixes.indexOf(nickname[0])) != -1)
    {
        if(nickname.isEmpty())
            return;
        nickname = nickname.mid(1);
        // cut off the prefix
        bool recognisedMode = false;
        // determine, whether status is like op or like voice
        while((modeIndex)<int(m_serverNickPrefixes.length()) && !recognisedMode)
        {
            switch(m_serverNickPrefixes[modeIndex].toLatin1())
            {
                case '*':                         // admin (EUIRC)
                {
                    isAdmin = true;
                    recognisedMode = true;
                    break;
                }
                case '&':                         // admin (unrealircd)
                {
                    isAdmin = true;
                    recognisedMode = true;
                    break;
                }
                case '!':                         // channel owner (RFC2811)
                {
                    isOwner = true;
                    recognisedMode = true;
                    break;
                }
                case '~':                         // channel owner (unrealircd)
                {
                    isOwner = true;
                    recognisedMode = true;
                    break;
                }
                case '@':                         // channel operator (RFC1459)
                {
                    isOp = true;
                    recognisedMode = true;
                    break;
                }
                case '%':                         // halfop
                {
                    isHalfop = true;
                    recognisedMode = true;
                    break;
                }
                case '+':                         // voiced (RFC1459)
                {
                    hasVoice = true;
                    recognisedMode = true;
                    break;
                }
                default:
                {
                    ++modeIndex;
                    break;
                }
            }                                     //switch to recognise the mode.
        }                                         // loop through the modes to find one recognised
    }                                             // loop through the name
}

void Server::hostFound()
{
    getStatusView()->appendServerMessage(i18n("Info"),i18n("Server found, connecting..."));
}

void Server::socketConnected()
{
    emit sslConnected(this);
    getConnectionSettings().setReconnectCount(0);

    if (getIdentity() && getIdentity()->getAuthType() == "saslplain"
        && !getIdentity()->getSaslAccount().isEmpty() && !getIdentity()->getAuthPassword().isEmpty())
    {
        capInitiateNegotiation();
    }

    QStringList ql;

    if (getIdentity() && getIdentity()->getAuthType() == "serverpw"
        && !getIdentity()->getAuthPassword().isEmpty())
    {
        ql << "PASS " + getIdentity()->getAuthPassword();
    }
    else if (!getConnectionSettings().server().password().isEmpty())
        ql << "PASS " + getConnectionSettings().server().password();

    ql << "NICK " + getNickname();
    ql << "USER " + getIdentity()->getIdent() + " 8 * :" /* 8 = +i; 4 = +w */ +  getIdentity()->getRealName();

    queueList(ql, HighPriority);

    connect(this, SIGNAL(nicknameChanged(QString)), getStatusView(), SLOT(setNickname(QString)));
    setNickname(getNickname());
}

void Server::capInitiateNegotiation()
{
    getStatusView()->appendServerMessage(i18n("Info"),i18n("Negotiating capabilities with server..."));

    getStatusView()->appendServerMessage(i18n("Info"),i18n("Requesting SASL capability..."));
    queue("CAP REQ :sasl", HighPriority);

    m_capRequested = true;
}

void Server::capReply()
{
    m_capAnswered = true;
}

void Server::capEndNegotiation()
{
    getStatusView()->appendServerMessage(i18n("Info"),i18n("Closing capabilities negotiation."));

    queue("CAP END", HighPriority);
}

void Server::capCheckIgnored()
{
    if (m_capRequested && !m_capAnswered)
        getStatusView()->appendServerMessage(i18n("Error"), i18n("Capabilities negotiation failed: Appears not supported by server."));
}

void Server::capAcknowledged(const QString& name, Server::CapModifiers modifiers)
{
    m_capAnswered = true;

    if (name == "sasl" && modifiers == Server::NoModifiers)
    {
        getStatusView()->appendServerMessage(i18n("Info"), i18n("SASL capability acknowledged by server, attempting SASL PLAIN authentication..."));
        sendAuthenticate("PLAIN");
    }
}

void Server::capDenied(const QString& name)
{
    if (name == "sasl")
        getStatusView()->appendServerMessage(i18n("Error"), i18n("SASL capability denied or not supported by server."));

    capEndNegotiation();
}

void Server::registerWithServices()
{
    if (!getIdentity())
        return;

    Nick2* nick = getNick(getNickname());
    if (nick && nick->isIdentified())
        return;

    if (getIdentity()->getAuthType() == "nickserv")
    {
        if (!getIdentity()->getNickservNickname().isEmpty()
            && !getIdentity()->getNickservCommand().isEmpty()
            && !getIdentity()->getAuthPassword().isEmpty())
        {
            queue("PRIVMSG "+getIdentity()->getNickservNickname()+" :"+getIdentity()->getNickservCommand()+' '+getIdentity()->getAuthPassword(), HighPriority);
        }
    }
    else if (getIdentity()->getAuthType() == "saslplain")
    {
        QString authString = getIdentity()->getSaslAccount();
        authString.append(QChar(QChar::Null));
        authString.append(getIdentity()->getSaslAccount());
        authString.append(QChar(QChar::Null));
        authString.append(getIdentity()->getAuthPassword());

        sendAuthenticate(authString.toAscii().toBase64());
    }
}

void Server::sendAuthenticate(const QString& message)
{
    m_lastAuthenticateCommand = message;
    queue("AUTHENTICATE " + message, HighPriority);
}

void Server::broken(KTcpSocket::Error error)
{
    Q_UNUSED(error);
    kDebug() << "Connection broken with state" << m_connectionState << "and error:" << m_socket->errorString();

    m_socket->blockSignals(true);

    resetQueues();

    m_pingSendTimer.stop();
    m_pingResponseTimer.stop();
    m_inputFilter.setLagMeasuring(false);
    m_currentLag = -1;

    purgeData();

    // HACK Only show one nick change dialog at connection time.
    // This hack is a bit nasty as it assumes that the only KDialog
    // child of the statusview will be the nick change dialog.
    if (getStatusView())
    {
        KDialog* nickChangeDialog = getStatusView()->findChild<KDialog*>();

        if (nickChangeDialog) nickChangeDialog->reject();
    }

    emit resetLag(this);
    emit nicksNowOnline(this, QStringList(), true);
    m_prevISONList.clear();

    updateAutoJoin();


    if (m_sslErrorLock)
    {
        // We got disconnected while dealing with an SSL error, e.g. due to the
        // user taking their time on dealing with the error dialog. Since auto-
        // reconnecting makes no sense in this situation, let's pass it off as a
        // deliberate disconnect. sslError() will either kick off a reconnect, or
        // reaffirm this.

        getStatusView()->appendServerMessage(i18n("SSL Connection Error"),
            i18n("Connection to server %1 (port <numid>%2</numid>) lost while waiting for user response to an SSL error. "
                 "Will automatically reconnect if error is ignored.",
                 getConnectionSettings().server().host(),
                 QString::number(getConnectionSettings().server().port())));

        updateConnectionState(SSDeliberatelyDisconnected);
    }
    else if (getConnectionState() == Konversation::SSDeliberatelyDisconnected)
    {
        if (m_reconnectImmediately)
        {
            m_reconnectImmediately = false;

            QMetaObject::invokeMethod(this, "connectToIRCServer", Qt::QueuedConnection);
        }
    }
    else
    {
        static_cast<Application*>(kapp)->notificationHandler()->connectionFailure(getStatusView(), getServerName());

        QString error = i18n("Connection to server %1 (port <numid>%2</numid>) lost: %3.",
            getConnectionSettings().server().host(),
            QString::number(getConnectionSettings().server().port()),
            m_socket->errorString());

        getStatusView()->appendServerMessage(i18n("Error"), error);

        updateConnectionState(Konversation::SSInvoluntarilyDisconnected);
    }
}

void Server::sslError( const QList<KSslError>& errors )
{
    // We have to explicitly grab the socket we got the error from,
    // lest we might end up calling ignoreSslErrors() on a different
    // socket later if m_socket has started pointing at something
    // else.
    QPointer<KTcpSocket> socket = qobject_cast<KTcpSocket*>(QObject::sender());

    m_sslErrorLock = true;
    bool ignoreSslErrors = KIO::SslUi::askIgnoreSslErrors(socket, KIO::SslUi::RecallAndStoreRules);
    m_sslErrorLock = false;

    // The dialog-based user interaction above may take an undefined amount
    // of time, and anything could happen to the socket in that span of time.
    // If it was destroyed, let's not do anything and bail out.
    if (!socket)
    {
        kDebug() << "Socket was destroyed while waiting for user interaction.";

        return;
    }

    // Ask the user if he wants to ignore the errors.
    if (ignoreSslErrors)
    {
        // Show a warning in the chat window that the SSL certificate failed the authenticity check.
        QString error = i18n("The SSL certificate for the server %1 (port <numid>%2</numid>) failed the authenticity check.",
            getConnectionSettings().server().host(),
            QString::number(getConnectionSettings().server().port()));

        getStatusView()->appendServerMessage(i18n("SSL Connection Warning"), error);

        // We may have gotten disconnected while waiting for user response and have
        // to reconnect.
        if (isConnecting())
        {
            // The user has chosen to ignore SSL errors.
            socket->ignoreSslErrors();
        }
        else
        {
            // QueuedConnection is vital here, otherwise we're deleting the socket
            // in a slot connected to one of its signals (connectToIRCServer deletes
            // any old socket) and crash.
            QMetaObject::invokeMethod(this, "connectToIRCServer", Qt::QueuedConnection);
        }
    }
    else
    {
        // Don't auto-reconnect if the user chose to ignore the SSL errors --
        // treat it as a deliberate disconnect.
        updateConnectionState(Konversation::SSDeliberatelyDisconnected);

        QString errorReason;

        for (int i = 0; i < errors.size(); ++i)
        {
            errorReason += errors.at(i).errorString() + ' ';
        }

        QString error = i18n("Could not connect to %1 (port <numid>%2</numid>) using SSL encryption. Either the server does not support SSL (did you use the correct port?) or you rejected the certificate. %3",
            getConnectionSettings().server().host(),
            QString::number(getConnectionSettings().server().port()),
            errorReason);

        getStatusView()->appendServerMessage(i18n("SSL Connection Error"), error);

        emit sslInitFailure();
    }
}

// Will be called from InputFilter as soon as the Welcome message was received
void Server::connectionEstablished(const QString& ownHost)
{
    // Some servers don't include the userhost in RPL_WELCOME, so we
    // need to use RPL_USERHOST to get ahold of our IP later on
    if (!ownHost.isEmpty())
        QHostInfo::lookupHost(ownHost, this, SLOT(gotOwnResolvedHostByWelcome(QHostInfo)));

    updateConnectionState(Konversation::SSConnected);

    // Register with services
    if (getIdentity() && getIdentity()->getAuthType() == "nickserv")
        registerWithServices();

    // get own ip by userhost
    requestUserhost(getNickname());

    // Start the PINGPONG match
    m_pingSendTimer.start(1000 /*1 sec*/);

    // Recreate away state if we were set away prior to a reconnect.
    if (m_away)
    {
        // Correct server's beliefs about its away state.
        m_away = false;
        requestAway(m_awayReason);
    }
}

//FIXME operator[] inserts an empty T& so each destination might just as well have its own key storage
QByteArray Server::getKeyForRecipient(const QString& recipient) const
{
    return m_keyHash[recipient.toLower()];
}

void Server::setKeyForRecipient(const QString& recipient, const QByteArray& key)
{
    m_keyHash[recipient.toLower()] = key;
}

void Server::gotOwnResolvedHostByWelcome(const QHostInfo& res)
{
    if (res.error() == QHostInfo::NoError && !res.addresses().isEmpty())
        m_ownIpByWelcome = res.addresses().first().toString();
    else
        kDebug() << "Got error: " << res.errorString();
}

bool Server::isSocketConnected() const
{
    if (!m_socket) return false;

    return (m_socket->state() == KTcpSocket::ConnectedState);
}

void Server::updateConnectionState(Konversation::ConnectionState state)
{
    if (state != m_connectionState)
    {
        m_connectionState = state;

        if (m_connectionState == Konversation::SSConnected)
            emit serverOnline(true);
        else if (m_connectionState != Konversation::SSConnecting)
            emit serverOnline(false);

       emit connectionStateChanged(this, state);
    }
}

void Server::reconnectServer(const QString& quitMessage)
{
    if (isConnecting() || isSocketConnected())
    {
        m_reconnectImmediately = true;

        quitServer(quitMessage);
    }
    else
        QMetaObject::invokeMethod(this, "connectToIRCServer", Qt::QueuedConnection);
}

void Server::disconnectServer(const QString& quitMessage)
{
    getConnectionSettings().setReconnectCount(0);

    if (isScheduledToConnect())
    {
        m_delayedConnectTimer->stop();
        getStatusView()->appendServerMessage(i18n("Info"), i18n("Delayed connect aborted."));
    }

    if (isSocketConnected()) quitServer(quitMessage);
}

void Server::quitServer(const QString& quitMessage)
{
    // Make clear this is deliberate even if the QUIT never actually goes through the queue
    // (i.e. this is not redundant with _send_internal()'s updateConnectionState() call for
    // a QUIT).
    updateConnectionState(Konversation::SSDeliberatelyDisconnected);

    if (!m_socket) return;

    QString toServer = "QUIT :";

    if (quitMessage.isEmpty())
        toServer += getIdentity()->getQuitReason();
    else
        toServer += quitMessage;

    queue(toServer, HighPriority);

    flushQueues();
    m_socket->flush();

    // Close the socket to allow a dead connection to be reconnected before the socket timeout.
    m_socket->close();

    getStatusView()->appendServerMessage(i18n("Info"), i18n("Disconnected from %1 (port <numid>%2</numid>).",
        getConnectionSettings().server().host(),
        QString::number(getConnectionSettings().server().port())));
}

void Server::notifyAction(const QString& nick)
{
    QString out(Preferences::self()->notifyDoubleClickAction());

    getOutputFilter()->replaceAliases(out);

    // parse wildcards (toParse,nickname,channelName,nickList,parameter)
    out = parseWildcards(out, getNickname(), QString(), QString(), nick, QString());

    // Send all strings, one after another
    QStringList outList = out.split('\n', QString::SkipEmptyParts);
    for (int index=0; index<outList.count(); ++index)
    {
        Konversation::OutputFilterResult result = getOutputFilter()->parse(getNickname(),outList[index],QString());
        queue(result.toServer);
    }                                             // endfor
}

void Server::autoCommandsAndChannels()
{
    if (getServerGroup() && !getServerGroup()->connectCommands().isEmpty())
    {
        QString connectCommands = getServerGroup()->connectCommands();

        if (!getNickname().isEmpty())
            connectCommands.replace("%nick", getNickname());

        QStringList connectCommandsList = connectCommands.split(';', QString::SkipEmptyParts);
        QStringList::iterator iter;

        for (iter = connectCommandsList.begin(); iter != connectCommandsList.end(); ++iter)
        {
            QString output(*iter);
            output = output.simplified();
            getOutputFilter()->replaceAliases(output);
            Konversation::OutputFilterResult result = getOutputFilter()->parse(getNickname(),output,QString());
            queue(result.toServer);
        }
    }

    if (getAutoJoin())
    {
        for ( QStringList::Iterator it = m_autoJoinCommands.begin(); it != m_autoJoinCommands.end(); ++it )
            queue((*it));
    }

    if (!m_connectionSettings.oneShotChannelList().isEmpty())
    {
        QStringList oneShotJoin = generateJoinCommand(m_connectionSettings.oneShotChannelList());
        for ( QStringList::Iterator it = oneShotJoin.begin(); it != oneShotJoin.end(); ++it )
        {
            queue((*it));
        }
        m_connectionSettings.clearOneShotChannelList();
    }
}

/** Create a set of indices into the nickname list of the current identity based on the current nickname.
 *
 * The index list is only used if the current nickname is not available. If the nickname is in the identity,
 * we do not want to retry it. If the nickname is not in the identity, it is considered to be at position -1.
 */
void Server::resetNickSelection()
{
    m_nickIndices.clear();
    //for equivalence testing in case the identity gets changed underneath us
    m_referenceNicklist = getIdentity()->getNicknameList();
    //where in this identities nicklist will we have started?
    int start = m_referenceNicklist.indexOf(getNickname());
    int len = m_referenceNicklist.count();

    //we first use this list of indices *after* we've already tried the current nick, which we don't want
    //to retry if we wrapped, so exclude its index here
    //if it wasn't in the list, we get -1 back, so then we *want* to include 0
    for (int i=start+1; i<len; i++)
        m_nickIndices.append(i);
    //now, from the beginning of the list, to the item before start
    for (int i=0; i<start; i++)
        m_nickIndices.append(i);
}

QString Server::getNextNickname()
{
     //if the identity changed underneath us (likely impossible), start over
    if (m_referenceNicklist != getIdentity()->getNicknameList())
        resetNickSelection();

    QString newNick;

    if (!m_nickIndices.isEmpty())
    {
        newNick = getIdentity()->getNickname(m_nickIndices.takeFirst());
    }
    else
    {
        QString inputText = i18n("No nicknames from the \"%1\" identity were accepted by the connection \"%2\".\nPlease enter a new one or press Cancel to disconnect:", getIdentity()->getName(), getDisplayName());
        newNick = KInputDialog::getText(i18n("Nickname error"), inputText,
                                        QString(), 0, getStatusView());
    }
    return newNick;
}

void Server::processIncomingData()
{
    m_incomingTimer.stop();

    if (!m_inputBuffer.isEmpty() && !m_processingIncoming)
    {
        m_processingIncoming = true;
        QString front(m_inputBuffer.front());
        m_inputBuffer.pop_front();
        m_inputFilter.parseLine(front);
        m_processingIncoming = false;

        if (!m_inputBuffer.isEmpty()) m_incomingTimer.start(0);
    }
}

void Server::incoming()
{
    //if (getConnectionSettings().server().SSLEnabled())
    //    emit sslConnected(this);


    //if (len <= 0 && getConnectionSettings().server().SSLEnabled())
    //    return;

    // split buffer to lines
    QList<QByteArray> bufferLines;
    while (m_socket->canReadLine())
    {
        QByteArray line(m_socket->readLine());
        //remove \n blowfish doesn't like it
        int i = line.size()-1;
        while (i >= 0 && (line[i]=='\n' || line[i]=='\r')) // since euIRC gets away with sending just \r, bet someone sends \n\r?
        {
            i--;
        }
        line.truncate(i+1);

        if (line.size() > 0)
            bufferLines.append(line);
    }

    while(!bufferLines.isEmpty())
    {
        // Pre parsing is needed in case encryption/decryption is needed
        // BEGIN set channel encoding if specified
        QString senderNick;
        bool isServerMessage = false;
        QString channelKey;
        QTextCodec* codec = getIdentity()->getCodec();
        QByteArray first = bufferLines.first();

        bufferLines.removeFirst();

        QStringList lineSplit = codec->toUnicode(first).split(' ', QString::SkipEmptyParts);

        if (lineSplit.count() >= 1)
        {
            if (lineSplit[0][0] == ':')          // does this message have a prefix?
            {
                if(!lineSplit[0].contains('!')) // is this a server(global) message?
                    isServerMessage = true;
                else
                    senderNick = lineSplit[0].mid(1, lineSplit[0].indexOf('!')-1);

                lineSplit.removeFirst();          // remove prefix
            }
        }

        if (lineSplit.isEmpty())
            continue;

        // BEGIN pre-parse to know where the message belongs to
        QString command = lineSplit[0].toLower();
        if( isServerMessage )
        {
            if( lineSplit.count() >= 3 )
            {
                if( command == "332" )            // RPL_TOPIC
                    channelKey = lineSplit[2];
                if( command == "372" )            // RPL_MOTD
                    channelKey = ":server";
            }
        }
        else                                      // NOT a global message
        {
            if( lineSplit.count() >= 2 )
            {
                // query
                if( ( command == "privmsg" ||
                    command == "notice"  ) &&
                    lineSplit[1] == getNickname() )
                {
                    channelKey = senderNick;
                }
                // channel message
                else if( command == "privmsg" ||
                    command == "notice"  ||
                    command == "join"    ||
                    command == "kick"    ||
                    command == "part"    ||
                    command == "topic"   )
                {
                    channelKey = lineSplit[1];
                }
            }
        }
        // END pre-parse to know where the message belongs to
        // Decrypt if necessary

        //send to raw log before decryption
        if (m_rawLog)
            m_rawLog->appendRaw(RawLog::Inbound, first);

#ifdef HAVE_QCA2
        QByteArray cKey = getKeyForRecipient(channelKey);
        if(!cKey.isEmpty())
        {
            if(command == "privmsg")
            {
                //only send encrypted text to decrypter
                int index = first.indexOf(":",first.indexOf(":")+1);
                if(this->identifyMsgEnabled()) // Workaround braindead Freenode prefixing messages with +
                    ++index;
                QByteArray backup = first.mid(0,index+1);

                if(getChannelByName(channelKey) && getChannelByName(channelKey)->getCipher()->setKey(cKey))
                    first = getChannelByName(channelKey)->getCipher()->decrypt(first.mid(index+1));
                else if(getQueryByName(channelKey) && getQueryByName(channelKey)->getCipher()->setKey(cKey))
                    first = getQueryByName(channelKey)->getCipher()->decrypt(first.mid(index+1));

                first.prepend(backup);
            }
        }
#endif
        bool isUtf8 = Konversation::isUtf8(first);

        QString encoded;

        if (isUtf8)
            encoded = QString::fromUtf8(first);
        else
        {
            // check setting
            QString channelEncoding;
            if( !channelKey.isEmpty() )
            {
                if(getServerGroup())
                    channelEncoding = Preferences::channelEncoding(getServerGroup()->id(), channelKey);
                else
                    channelEncoding = Preferences::channelEncoding(getDisplayName(), channelKey);
            }
            // END set channel encoding if specified

            if( !channelEncoding.isEmpty() )
                codec = Konversation::IRCCharsets::self()->codecForName(channelEncoding);

            // if channel encoding is utf-8 and the string is definitely not utf-8
            // then try latin-1
            if (codec->mibEnum() == 106)
                codec = QTextCodec::codecForMib( 4 /* iso-8859-1 */ );

            encoded = codec->toUnicode(first);
        }

        // Qt uses 0xFDD0 and 0xFDD1 to mark the beginning and end of text frames. Remove
        // these here to avoid fatal errors encountered in QText* and the event loop pro-
        // cessing.
        sterilizeUnicode(encoded);

        if (!encoded.isEmpty())
            m_inputBuffer << encoded;

        //FIXME: This has nothing to do with bytes, and it's not raw received bytes either. Bogus number.
        //m_bytesReceived+=m_inputBuffer.back().length();
    }

    if( !m_incomingTimer.isActive() && !m_processingIncoming )
        m_incomingTimer.start(0);
}

/** Calculate how long this message premable will be.

    This is necessary because the irc server will clip messages so that the
    client receives a maximum of 512 bytes at once.
*/
int Server::getPreLength(const QString& command, const QString& dest)
{
    int hostMaskLength = getNickHostmask(getNickname()).length();

    //:Sho_!i=ehs1@konversation/developer/hein PRIVMSG #konversation :and then back to it

    //<colon>$nickname<!>$hostmask<space>$command<space>$destination<space><colon>$message<cr><lf>
    int x= 512 - 8 - (m_nickname.length() + hostMaskLength + command.length() + dest.length());

    return x;
}

//Commands greater than 1 have localizeable text:         0   1    2       3      4    5    6
static QStringList outcmds = QString("WHO QUIT PRIVMSG NOTICE KICK PART TOPIC").split(QChar(' '));

int Server::_send_internal(QString outputLine)
{
    QStringList outputLineSplit = outputLine.split(' ', QString::SkipEmptyParts);

    int outboundCommand = -1;
    if (!outputLineSplit.isEmpty()) {
        //Lets cache the uppercase command so we don't miss or reiterate too much
        outboundCommand = outcmds.indexOf(outputLineSplit[0].toUpper());
    }

    if (outputLine.at(outputLine.length()-1) == '\n')
    {
        kDebug() << "found \\n on " << outboundCommand;
        outputLine.resize(outputLine.length()-1);
    }

    // remember the first arg of /WHO to identify responses
    if (outboundCommand == 0 && outputLineSplit.count() >= 2) //"WHO"
        m_inputFilter.addWhoRequest(outputLineSplit[1]);
    else if (outboundCommand == 1) //"QUIT"
        updateConnectionState(Konversation::SSDeliberatelyDisconnected);

    // set channel encoding if specified
    QString channelCodecName;

    //[ PRIVMSG | NOTICE | KICK | PART | TOPIC ] target :message
    if (outputLineSplit.count() > 2 && outboundCommand > 1)
    {
        if(getServerGroup()) // if we're connecting via a servergroup
            channelCodecName=Preferences::channelEncoding(getServerGroup()->id(), outputLineSplit[1]);
        else //if we're connecting to a server manually
            channelCodecName=Preferences::channelEncoding(getDisplayName(), outputLineSplit[1]);
    }
    QTextCodec* codec = 0;
    if (channelCodecName.isEmpty())
        codec = getIdentity()->getCodec();
    else
        codec = Konversation::IRCCharsets::self()->codecForName(channelCodecName);

    // Some codecs don't work with a negative value. This is a bug in Qt 3.
    // ex.: JIS7, eucJP, SJIS
    //int outlen=-1;

    //leaving this done twice for now, I'm uncertain of the implications of not encoding other commands
    QByteArray encoded = outputLine.toUtf8();
    if(codec)
        encoded = codec->fromUnicode(outputLine);

    #ifdef HAVE_QCA2
    QString cipherKey;
    if (outputLineSplit.count() > 2 && outboundCommand > 1)
        cipherKey = getKeyForRecipient(outputLineSplit.at(1));
    if (!cipherKey.isEmpty())
    {
        int colon = outputLine.indexOf(':');
        if (colon > -1)
        {
            colon++;

            QString pay(outputLine.mid(colon));
            //only encode the actual user text, IRCD *should* desire only ASCII 31 < x < 127 for protocol elements
            QByteArray payload = pay.toUtf8();

            QByteArray dest;
            if (codec)
            {
                payload=codec->fromUnicode(pay);
                //apparently channel name isn't a protocol element...
                dest = codec->fromUnicode(outputLineSplit.at(1));
            }
            else
            {
                dest = outputLineSplit.at(1).toAscii();
            }

            if (outboundCommand == 2 || outboundCommand == 6) // outboundCommand == 3
            {
                bool doit = true;
                if (outboundCommand == 2)
                {
                    //if its a privmsg and a ctcp but not an action, don't encrypt
                    //not interpreting `payload` in case encoding bollixed it
                    if (outputLineSplit.at(2).startsWith(QLatin1String(":\x01")) && outputLineSplit.at(2) != ":\x01""ACTION")
                        doit = false;
                }
                if (doit)
                {
                    QString target = outputLineSplit.at(1);

                    if(getChannelByName(target) && getChannelByName(target)->getCipher()->setKey(cipherKey.toLocal8Bit()))
                        getChannelByName(target)->getCipher()->encrypt(payload);
                    else if(getQueryByName(target) && getQueryByName(target)->getCipher()->setKey(cipherKey.toLocal8Bit()))
                        getQueryByName(target)->getCipher()->encrypt(payload);

                    encoded = outputLineSplit.at(0).toAscii();
                    kDebug() << payload << "\n" << payload.data();
                    //two lines because the compiler insists on using the wrong operator+
                    encoded += ' ' + dest + " :" + payload;
                }
            }
        }
    }
    #endif

    if (m_rawLog)
        m_rawLog->appendRaw(RawLog::Outbound, encoded);

    encoded += '\n';
    qint64 sout = m_socket->write(encoded, encoded.length());

    return sout;
}

void Server::toServer(QString&s, IRCQueue* q)
{

    int sizesent = _send_internal(s);
    emit sentStat(s.length(), sizesent, q); //tell the queues what we sent
    //tell everyone else
    emit sentStat(s.length(), sizesent);
}

void Server::collectStats(int bytes, int encodedBytes)
{
    m_bytesSent += bytes;
    m_encodedBytesSent += encodedBytes;
    m_linesSent++;
}

bool Server::validQueue(QueuePriority priority)
{
   if (priority >=0 && priority <= Application::instance()->countOfQueues())
       return true;
   return false;
}

bool Server::queue(const QString& line, QueuePriority priority)
{
    if (!line.isEmpty() && validQueue(priority))
    {
        IRCQueue& out=*m_queues[priority];
        out.enqueue(line);
        return true;
    }
    return false;
}

bool Server::queueList(const QStringList& buffer, QueuePriority priority)
{
    if (buffer.isEmpty() || !validQueue(priority))
        return false;

    IRCQueue& out=*(m_queues[priority]);

    for(int i=0;i<buffer.count();i++)
    {
        QString line(buffer.at(i));
        if (!line.isEmpty())
            out.enqueue(line);
    }
    return true;
}

void Server::resetQueues()
{
    m_incomingTimer.stop();
    m_inputBuffer.clear();
    for (int i=0; i <= Application::instance()->countOfQueues(); i++)
        m_queues[i]->reset();
}

//this could flood you off, but you're leaving anyway...
void Server::flushQueues()
{
    int cue;
    do
    {
        cue=-1;
        int wait=0;
        for (int i=1; i <= Application::instance()->countOfQueues(); i++) //slow queue can rot
        {
            IRCQueue *queue=m_queues[i];
            //higher queue indices have higher priorty, higher queue priority wins tie
            if (!queue->isEmpty() && queue->currentWait()>=wait)
            {
                cue=i;
                wait=queue->currentWait();
            }
        }
        if (cue>-1)
            m_queues[cue]->sendNow();
    } while (cue>-1);
}

void Server::closed()
{
    broken(m_socket->error());
}

void Server::dbusRaw(const QString& command)
{
    if(command.startsWith(Preferences::self()->commandChar()))
    {
        queue(command.section(Preferences::self()->commandChar(), 1));
    }
    else
        queue(command);
}

void Server::dbusSay(const QString& target,const QString& command)
{
    if(isAChannel(target))
    {
        Channel* channel=getChannelByName(target);
        if(channel) channel->sendText(command);
    }
    else
    {
        Query* query = getQueryByName(target);
        if(query==0)
        {
            query=addQuery(target, true);
        }
        if(query)
        {
            if(!command.isEmpty())
                query->sendText(command);
            else
            {
                query->adjustFocus();
                getViewContainer()->getWindow()->show();
                KWindowSystem::demandAttention(getViewContainer()->getWindow()->winId());
                KWindowSystem::activateWindow(getViewContainer()->getWindow()->winId());
            }
        }
    }
}

void Server::dbusInfo(const QString& string)
{
    appendMessageToFrontmost(i18n("D-Bus"),string);
}

void Server::ctcpReply(const QString &receiver,const QString &text)
{
    queue("NOTICE "+receiver+" :"+'\x01'+text+'\x01');
}

// Returns pointer to the ChannelNick (mode and pointer to NickInfo) for a given channel and nickname.
// 0 if not found.
Nick2* Server::getNick(const QString& nickname)
{
    return nickListModel2()->getNick(connectionId(), nickname);
}

// Updates a nickname in a channel.  If not on the joined or unjoined lists, and nick
// is in the watch list, adds the channel and nick to the unjoinedChannels list.
// If mode != 99, sets the mode for the nick in the channel.
// Returns the NickInfo object if nick is on any lists, otherwise 0.
void Server::setChannelNick(const QString& channelName, const QString& nickname, unsigned int mode)
{
    m_nickListModel2->addNickToChannel(m_connectionId, channelName, nickname);

    //TODO why 99?
    if(mode != 99)
        m_nickListModel2->setNickMode(m_connectionId, channelName, nickname, mode);

}

// Returns a list of all the shared channels
QStringList Server::getSharedChannels(const QString& nickname)
{
    QStringList channelList = m_nickListModel2->getNickChannels(m_connectionId, nickname);
    QStringList ourChannelList = m_nickListModel2->getNickChannels(m_connectionId, getNickname());

    QStringList sharedChannelsList = QStringList();
    QStringList::ConstIterator i;

    for (i = ourChannelList.constBegin(); i != ourChannelList.constEnd(); ++i)
    {
        if (channelList.contains(*i))
            sharedChannelsList.append(*i);
    }

    return sharedChannelsList;
}

// Returns a list of all the channels (joined or unjoined) that a nick is in.
QStringList Server::getNickChannels(const QString& nickname)
{
    return m_nickListModel2->getNickChannels(m_connectionId, nickname);
}

bool Server::isNickOnline(const QString &nickname)
{
    return m_nickListModel2->isNickOnline(m_connectionId, nickname);
}

QString Server::getOwnIpByNetworkInterface()
{
    return m_socket->localAddress().toString();
}

QString Server::getOwnIpByServerMessage()
{
    if(!m_ownIpByWelcome.isEmpty())
        return m_ownIpByWelcome;
    else if(!m_ownIpByUserhost.isEmpty())
        return m_ownIpByUserhost;
    else
        return QString();
}

Query* Server::addQuery(const QString& nickname, bool weinitiated)
{
    // Only create new query object if there isn't already one with the same name
    Query* query = getQueryByName(nickname);

    if (!query)
    {
        QString lcNickname = nickname.toLower();
        query = getViewContainer()->addQuery(this, nickname, weinitiated);

        query->indicateAway(m_away);

        connect(query, SIGNAL(sendFile(QString)),this, SLOT(requestDccSend(QString)));
        connect(this, SIGNAL(serverOnline(bool)), query, SLOT(serverOnline(bool)));

        // Append query to internal list
        m_queryList.append(query);

        if (!weinitiated)
            static_cast<Application*>(kapp)->notificationHandler()->query(query, nickname);
    }
    else if (weinitiated)
    {
        emit showView(query);
    }

    // try to get hostmask if there's none yet
    if (getNickHostmask(nickname).isEmpty()) requestUserhost(nickname);

    Q_ASSERT(query);

    return query;
}

void Server::closeQuery(const QString &name)
{
    Query* query = getQueryByName(name);
    removeQuery(query);
}

void Server::closeChannel(const QString& name)
{
    kDebug() << "Server::closeChannel(" << name << ")";
    Channel* channelToClose = getChannelByName(name);

    if (channelToClose && channelToClose->joined())
    {
        Konversation::OutputFilterResult result = getOutputFilter()->parse(getNickname(),
            Preferences::self()->commandChar() + "PART", name);
        queue(result.toServer);
    }
}

void Server::requestChannelList()
{
    m_inputFilter.setAutomaticRequest("LIST", QString(), true);
    queue(QString("LIST"));
}

void Server::requestWhois(const QString& nickname)
{
    m_inputFilter.setAutomaticRequest("WHOIS", nickname, true);
    queue("WHOIS "+nickname, LowPriority);
}

void Server::requestWho(const QString& channel)
{
    m_inputFilter.setAutomaticRequest("WHO", channel, true);
    queue("WHO "+channel, LowPriority);
}

void Server::requestUserhost(const QString& nicks)
{
    const QStringList nicksList = nicks.split(' ', QString::SkipEmptyParts);
    for(QStringList::ConstIterator it=nicksList.constBegin() ; it!=nicksList.constEnd() ; ++it)
        m_inputFilter.setAutomaticRequest("USERHOST", *it, true);
    queue("USERHOST "+nicks, LowPriority);
}

void Server::requestTopic(const QString& channel)
{
    m_inputFilter.setAutomaticRequest("TOPIC", channel, true);
    queue("TOPIC "+channel, LowPriority);
}

void Server::resolveUserhost(const QString& nickname)
{
    m_inputFilter.setAutomaticRequest("WHOIS", nickname, true);
    m_inputFilter.setAutomaticRequest("DNS", nickname, true);
    queue("WHOIS "+nickname, LowPriority); //FIXME when is this really used?
}

void Server::requestBan(const QStringList& users,const QString& channel,const QString& a_option)
{
    QString hostmask;
    QString option=a_option.toLower();

    Channel* targetChannel=getChannelByName(channel);

    for(int index=0;index<users.count();index++)
    {
        // first, set the ban mask to the specified nick
        QString mask=users[index];
        // did we specify an option?
        if(!option.isEmpty())
        {
            // try to find specified nick on the channel
            Nick2* targetNick = targetChannel->getNickByName(mask);
            // if we found the nick try to find their hostmask
            if(targetNick)
            {
                QString hostmask = targetNick->getHostmask();
                // if we found the hostmask, add it to the ban mask
                if(!hostmask.isEmpty())
                {
                    mask = targetNick->getNickname()+'!'+hostmask;

                    // adapt ban mask to the option given
                    if(option=="host")
                        mask="*!*@*."+hostmask.section('.',1);
                    else if(option=="domain")
                        mask="*!*@"+hostmask.section('@',1);
                    else if(option=="userhost")
                        mask="*!"+hostmask.section('@',0,0)+"@*."+hostmask.section('.',1);
                    else if(option=="userdomain")
                        mask="*!"+hostmask.section('@',0,0)+'@'+hostmask.section('@',1);
                }
            }
        }

        Konversation::OutputFilterResult result = getOutputFilter()->execBan(mask,channel);
        queue(result.toServer);
    }
}

void Server::requestUnban(const QString& mask,const QString& channel)
{
    Konversation::OutputFilterResult result = getOutputFilter()->execUnban(mask,channel);
    queue(result.toServer);
}

void Server::requestDccSend()
{
    requestDccSend(QString());
}

void Server::sendURIs(const KUrl::List& uris, const QString& nick)
{
    foreach(const KUrl& uri, uris)
         addDccSend(nick, uri);
}

void Server::requestDccSend(const QString &a_recipient)
{
    QString recipient(a_recipient);
    // if we don't have a recipient yet, let the user select one
    if (recipient.isEmpty())
    {
        recipient = recipientNick();
    }

    // do we have a recipient *now*?
    if(!recipient.isEmpty())
    {
        QPointer<DccFileDialog> dlg = new DccFileDialog (KUrl(), QString(), getViewContainer()->getWindow());
        //DccFileDialog fileDialog(KUrl(), QString(), getViewContainer()->getWindow());
        KUrl::List fileURLs = dlg->getOpenUrls(
            KUrl(),
            QString(),
            i18n("Select File(s) to Send to %1", recipient)
        );
        KUrl::List::const_iterator it;
        for ( it = fileURLs.constBegin() ; it != fileURLs.constEnd() ; ++it )
        {
            addDccSend( recipient, *it, dlg->passiveSend());
        }
        delete dlg;
    }
}

void Server::slotNewDccTransferItemQueued(DCC::Transfer* transfer)
{
    if (transfer->getConnectionId() == connectionId() )
    {
        kDebug() << "connecting slots for " << transfer->getFileName() << " [" << transfer->getType() << "]";
        if ( transfer->getType() == DCC::Transfer::Receive )
        {
            connect( transfer, SIGNAL(done(Konversation::DCC::Transfer*)), this, SLOT(dccGetDone(Konversation::DCC::Transfer*)) );
            connect( transfer, SIGNAL(statusChanged(Konversation::DCC::Transfer*,int,int)), this, SLOT(dccStatusChanged(Konversation::DCC::Transfer*,int,int)) );
        }
        else
        {
            connect( transfer, SIGNAL(done(Konversation::DCC::Transfer*)), this, SLOT(dccSendDone(Konversation::DCC::Transfer*)) );
            connect( transfer, SIGNAL(statusChanged(Konversation::DCC::Transfer*,int,int)), this, SLOT(dccStatusChanged(Konversation::DCC::Transfer*,int,int)) );
        }
    }
}

void Server::addDccSend(const QString &recipient, KUrl fileURL, bool passive, const QString &altFileName, quint64 fileSize)
{
    if (!fileURL.isValid())
    {
        return;
    }

    // We already checked that the file exists in output filter / requestDccSend() resp.
    DCC::TransferSend* newDcc = Application::instance()->getDccTransferManager()->newUpload();

    newDcc->setConnectionId(connectionId());

    newDcc->setPartnerNick(recipient);
    newDcc->setFileURL(fileURL);
    newDcc->setReverse(passive);
    if (!altFileName.isEmpty())
        newDcc->setFileName(altFileName);
    if (fileSize != 0)
        newDcc->setFileSize(fileSize);

    emit addDccPanel();

    if (newDcc->queue())
        newDcc->start();
}

QString Server::recoverDccFileName(const QStringList & dccArguments, int offset) const
{
    QString fileName;
    if(dccArguments.count() > offset + 1)
    {
        kDebug() << "recover filename";
        const int argumentOffsetSize = dccArguments.size() - offset;
        for (int i = 0; i < argumentOffsetSize; ++i)
        {
            fileName += dccArguments.at(i);
            //if not last element, append a space
            if (i < (argumentOffsetSize - 1))
            {
                fileName += ' ';
            }
        }
    }
    else
    {
        fileName = dccArguments.at(0);
    }

    return cleanDccFileName(fileName);
}

QString Server::cleanDccFileName(const QString& filename) const
{
    QString cleanFileName = filename;

    //we want a clean filename to get rid of the mass """filename"""
    //NOTE: if a filename starts really with a ", it is escaped -> \" (2 chars)
    //      but most clients don't support that and just replace it with a _
    while (cleanFileName.startsWith('\"') && cleanFileName.endsWith('\"'))
    {
        cleanFileName = cleanFileName.mid(1, cleanFileName.length() - 2);
    }

    return cleanFileName;
}

quint16 Server::stringToPort(const QString &port, bool *ok)
{
    bool toUintOk = false;
    uint uPort32 = port.toUInt(&toUintOk);
    // ports over 65535 are invalid, someone sends us bad data
    if (!toUintOk || uPort32 > USHRT_MAX)
    {
        if (ok)
        {
            *ok = false;
        }
    }
    else
    {
        if (ok)
        {
            *ok = true;
        }
    }
    return (quint16)uPort32;
}

QString Server::recipientNick() const
{
    //FIXME set the root index for this view
    return DCC::RecipientDialog::getNickname(getViewContainer()->getWindow(), m_nickListModel2);
}

void Server::addDccGet(const QString &sourceNick, const QStringList &dccArguments)
{
    //filename ip port filesize [token]
    QString ip;
    quint16 port;
    QString fileName;
    quint64 fileSize;
    QString token;
    const int argumentSize = dccArguments.count();
    bool ok = true;

    if (dccArguments.at(argumentSize - 3) == "0") //port==0, for passive send, ip can't be 0
    {
        //filename ip port(0) filesize token
        fileName = recoverDccFileName(dccArguments, 4); //ip port filesize token
        ip = DCC::DccCommon::numericalIpToTextIp( dccArguments.at(argumentSize - 4) ); //-1 index, -1 token, -1 port, -1 filesize
        port = 0;
        fileSize = dccArguments.at(argumentSize - 2).toULongLong(); //-1 index, -1 token
        token = dccArguments.at(argumentSize - 1); //-1 index
    }
    else
    {
        //filename ip port filesize
        ip = DCC::DccCommon::numericalIpToTextIp( dccArguments.at(argumentSize - 3) ); //-1 index, -1 filesize
        fileName = recoverDccFileName(dccArguments, 3); //ip port filesize
        fileSize = dccArguments.at(argumentSize - 1).toULongLong(); //-1 index
        port = stringToPort(dccArguments.at(argumentSize - 2), &ok); //-1 index, -1 filesize
    }

    if (!ok)
    {
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1=nickname","Received invalid DCC SEND request from %1.",
                                       sourceNick));
        return;
    }

    DCC::TransferRecv* newDcc = Application::instance()->getDccTransferManager()->newDownload();

    newDcc->setConnectionId(connectionId());
    newDcc->setPartnerNick(sourceNick);

    newDcc->setPartnerIp(ip);
    newDcc->setPartnerPort(port);
    newDcc->setFileName(fileName);
    newDcc->setFileSize(fileSize);
    // Reverse DCC
    if (!token.isEmpty())
    {
        newDcc->setReverse(true, token);
    }

    kDebug() << "ip: " << ip;
    kDebug() << "port: " << port;
    kDebug() << "filename: " << fileName;
    kDebug() << "filesize: " << fileSize;
    kDebug() << "token: " << token;

    //emit after data was set
    emit addDccPanel();

    if ( newDcc->queue() )
    {
        appendMessageToFrontmost( i18n( "DCC" ),
                                  i18n( "%1 offers to send you \"%2\" (%3)...",
                                        newDcc->getPartnerNick(),
                                        fileName,
                                        ( newDcc->getFileSize() == 0 ) ? i18n( "unknown size" ) : KIO::convertSize( newDcc->getFileSize() ) ) );

        if (Preferences::self()->dccAutoGet())
            newDcc->start();
    }
}

void Server::addDccChat(const QString& sourceNick, const QStringList& dccArguments)
{
    //chat ip port [token]
    QString ip;
    quint16 port = 0;
    QString token;
    bool reverse = false;
    const int argumentSize = dccArguments.count();
    bool ok = true;
    QString extension;

    extension = dccArguments.at(0);
    ip = DCC::DccCommon::numericalIpToTextIp(dccArguments.at(1));

    if (argumentSize == 3)
    {
        //extension ip port
        port = stringToPort(dccArguments.at(2), &ok);
    }
    else if (argumentSize == 4)
    {
        //extension ip port(0) token
        token = dccArguments.at(3);
        reverse = true;
    }

    if (!ok)
    {
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1=nickname","Received invalid DCC CHAT request from %1.",
                                       sourceNick));
        return;
    }

    DCC::Chat* newChat = Application::instance()->getDccTransferManager()->newChat();

    newChat->setConnectionId(connectionId());
    newChat->setPartnerNick(sourceNick);
    newChat->setOwnNick(getNickname());

    kDebug() << "ip: " << ip;
    kDebug() << "port: " << port;
    kDebug() << "token: " << token;
    kDebug() << "extension: " << extension;

    newChat->setPartnerIp(ip);
    newChat->setPartnerPort(port);
    newChat->setReverse(reverse, token);
    newChat->setSelfOpened(false);
    newChat->setExtension(extension);

    emit addDccChat(newChat);
    newChat->start();
}

void Server::openDccChat(const QString& nickname)
{
    kDebug();
    QString recipient(nickname);
    // if we don't have a recipient yet, let the user select one
    if (recipient.isEmpty())
    {
        recipient = recipientNick();
    }

    // do we have a recipient *now*?
    if (!recipient.isEmpty())
    {
        DCC::Chat* newChat = Application::instance()->getDccTransferManager()->newChat();
        newChat->setConnectionId(connectionId());
        newChat->setPartnerNick(recipient);
        newChat->setOwnNick(getNickname());
        newChat->setSelfOpened(true);
        emit addDccChat(newChat);
        newChat->start();
    }
}

void Server::openDccWBoard(const QString& nickname)
{
    kDebug();
    QString recipient(nickname);
    // if we don't have a recipient yet, let the user select one
    if (recipient.isEmpty())
    {
        recipient = recipientNick();
    }

    // do we have a recipient *now*?
    if (!recipient.isEmpty())
    {
        DCC::Chat* newChat = Application::instance()->getDccTransferManager()->newChat();
        newChat->setConnectionId(connectionId());
        newChat->setPartnerNick(recipient);
        newChat->setOwnNick(getNickname());
        // Set extension before emiting addDccChat
        newChat->setExtension(DCC::Chat::Whiteboard);
        newChat->setSelfOpened(true);
        emit addDccChat(newChat);
        newChat->start();
    }
}

void Server::requestDccChat(const QString& partnerNick, const QString& extension, const QString& numericalOwnIp, quint16 ownPort)
{
    Konversation::OutputFilterResult result = getOutputFilter()->requestDccChat(partnerNick, extension, numericalOwnIp,ownPort);
    queue(result.toServer);
}

void Server::acceptDccGet(const QString& nick, const QString& file)
{
    Application::instance()->getDccTransferManager()->acceptDccGet(m_connectionId, nick, file);
}

void Server::dccSendRequest(const QString &partner, const QString &fileName, const QString &address, quint16 port, quint64 size)
{
    Konversation::OutputFilterResult result = getOutputFilter()->sendRequest(partner,fileName,address,port,size);
    queue(result.toServer);

    appendMessageToFrontmost( i18n( "DCC" ),
                              i18n( "Asking %1 to accept upload of \"%2\" (%3)...",
                                    partner,
                                    cleanDccFileName(fileName),
                                    ( size == 0 ) ? i18n( "unknown size" ) : KIO::convertSize( size ) ) );
}

void Server::dccPassiveSendRequest(const QString& recipient,const QString& fileName,const QString& address,quint64 size,const QString& token)
{
    Konversation::OutputFilterResult result = getOutputFilter()->passiveSendRequest(recipient,fileName,address,size,token);
    queue(result.toServer);

    appendMessageToFrontmost( i18n( "DCC" ),
                              i18n( "Asking %1 to accept passive upload of \"%2\" (%3)...",
                                    recipient,
                                    cleanDccFileName(fileName),
                                    ( size == 0 ) ? i18n( "unknown size" ) : KIO::convertSize( size ) ) );
}

void Server::dccPassiveChatRequest(const QString& recipient, const QString& extension, const QString& address, const QString& token)
{
    Konversation::OutputFilterResult result = getOutputFilter()->passiveChatRequest(recipient, extension, address, token);
    queue(result.toServer);

    appendMessageToFrontmost(i18n("DCC"),
                             i18nc("%1=name, %2=dcc extension, chat or wboard for example","Asking %1 to accept %2...", recipient, extension));
}

void Server::dccPassiveResumeGetRequest(const QString& sender,const QString& fileName,quint16 port,KIO::filesize_t startAt,const QString &token)
{
    Konversation::OutputFilterResult result = getOutputFilter()->resumePassiveRequest(sender,fileName,port,startAt,token);;
    queue(result.toServer);
}

void Server::dccResumeGetRequest(const QString &sender, const QString &fileName, quint16 port, KIO::filesize_t startAt)
{
    Konversation::OutputFilterResult result = getOutputFilter()->resumeRequest(sender,fileName,port,startAt);;
    queue(result.toServer);
}

void Server::dccReverseSendAck(const QString& partnerNick,const QString& fileName,const QString& ownAddress,quint16 ownPort,quint64 size,const QString& reverseToken)
{
    Konversation::OutputFilterResult result = getOutputFilter()->acceptPassiveSendRequest(partnerNick,fileName,ownAddress,ownPort,size,reverseToken);
    queue(result.toServer);
}

void Server::dccReverseChatAck(const QString& partnerNick, const QString& extension, const QString& ownAddress, quint16 ownPort, const QString& reverseToken)
{
    Konversation::OutputFilterResult result = getOutputFilter()->acceptPassiveChatRequest(partnerNick, extension, ownAddress, ownPort, reverseToken);
    queue(result.toServer);
}

void Server::dccRejectSend(const QString& partnerNick, const QString& fileName)
{
    Konversation::OutputFilterResult result = getOutputFilter()->rejectDccSend(partnerNick,fileName);
    queue(result.toServer);
}

void Server::dccRejectChat(const QString& partnerNick, const QString& extension)
{
    Konversation::OutputFilterResult result = getOutputFilter()->rejectDccChat(partnerNick, extension);
    queue(result.toServer);
}

void Server::startReverseDccChat(const QString &sourceNick, const QStringList &dccArguments)
{
    kDebug();
    DCC::TransferManager* dtm = Application::instance()->getDccTransferManager();

    bool ok = true;
    QString partnerIP = DCC::DccCommon::numericalIpToTextIp(dccArguments.at(1));
    quint16 port = stringToPort(dccArguments.at(2), &ok);
    QString token = dccArguments.at(3);

    kDebug() << "ip: " << partnerIP;
    kDebug() << "port: " << port;
    kDebug() << "token: " << token;

    if (!ok || dtm->startReverseChat(connectionId(), sourceNick,
                                    partnerIP, port, token) == 0)
    {
        // DTM could not find a matched item
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1 = nickname",
                                       "Received invalid passive DCC chat acceptance message from %1.",
                                       sourceNick));
    }
}

void Server::startReverseDccSendTransfer(const QString& sourceNick,const QStringList& dccArguments)
{
    kDebug();
    DCC::TransferManager* dtm = Application::instance()->getDccTransferManager();

    bool ok = true;
    const int argumentSize = dccArguments.size();
    QString partnerIP = DCC::DccCommon::numericalIpToTextIp( dccArguments.at(argumentSize - 4) ); //dccArguments[1] ) );
    quint16 port = stringToPort(dccArguments.at(argumentSize - 3), &ok);
    QString token = dccArguments.at(argumentSize - 1);
    quint64 fileSize = dccArguments.at(argumentSize - 2).toULongLong();
    QString fileName = recoverDccFileName(dccArguments, 4); //ip port filesize token

    kDebug() << "ip: " << partnerIP;
    kDebug() << "port: " << port;
    kDebug() << "filename: " << fileName;
    kDebug() << "filesize: " << fileSize;
    kDebug() << "token: " << token;

    if (!ok ||
        dtm->startReverseSending(connectionId(), sourceNick,
                                 fileName,  // filename
                                 partnerIP,  // partner IP
                                 port,  // partner port
                                 fileSize,  // filesize
                                 token  // Reverse DCC token
         ) == 0)
    {
        // DTM could not find a matched item
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1 = file name, %2 = nickname",
                                       "Received invalid passive DCC send acceptance message for \"%1\" from %2.",
                                       fileName,
                                       sourceNick));
    }
}

void Server::resumeDccGetTransfer(const QString &sourceNick, const QStringList &dccArguments)
{
    DCC::TransferManager* dtm = Application::instance()->getDccTransferManager();

    //filename port position [token]
    QString fileName;
    quint64 position;
    quint16 ownPort;
    bool ok = true;
    const int argumentSize = dccArguments.count();

    if (dccArguments.at(argumentSize - 3) == "0") //-1 index, -1 token, -1 pos
    {
        fileName = recoverDccFileName(dccArguments, 3); //port position token
        ownPort = 0;
        position = dccArguments.at(argumentSize - 2).toULongLong(); //-1 index, -1 token
    }
    else
    {
        fileName = recoverDccFileName(dccArguments, 2); //port position
        ownPort = stringToPort(dccArguments.at(argumentSize - 2), &ok); //-1 index, -1 pos
        position = dccArguments.at(argumentSize - 1).toULongLong(); //-1 index
    }
    //do we need the token here?

    DCC::TransferRecv* dccTransfer = 0;
    if (ok)
    {
        dccTransfer = dtm->resumeDownload(connectionId(), sourceNick, fileName, ownPort, position);
    }

    if (dccTransfer)
    {
        appendMessageToFrontmost(i18n("DCC"),
                                 i18nc("%1 = file name, %2 = nickname of sender, %3 = percentage of file size, %4 = file size",
                                       "Resuming download of \"%1\" from %2 starting at %3% of %4...",
                                       fileName,
                                       sourceNick,
                                       QString::number( dccTransfer->getProgress()),
                                       (dccTransfer->getFileSize() == 0) ? i18n("unknown size") : KIO::convertSize(dccTransfer->getFileSize())));
    }
    else
    {
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1 = file name, %2 = nickname",
                                       "Received invalid resume acceptance message for \"%1\" from %2.",
                                       fileName,
                                       sourceNick));
    }
}

void Server::resumeDccSendTransfer(const QString &sourceNick, const QStringList &dccArguments)
{
    DCC::TransferManager* dtm = Application::instance()->getDccTransferManager();

    bool passiv = false;
    QString fileName;
    quint64 position;
    QString token;
    quint16 ownPort;
    bool ok = true;
    const int argumentSize = dccArguments.count();

    //filename port filepos [token]
    if (dccArguments.at( argumentSize - 3) == "0")
    {
        //filename port filepos token
        passiv = true;
        ownPort = 0;
        token = dccArguments.at( argumentSize - 1); // -1 index
        position = dccArguments.at( argumentSize - 2).toULongLong(); // -1 index, -1 token
        fileName = recoverDccFileName(dccArguments, 3); //port filepos token
    }
    else
    {
        //filename port filepos
        ownPort = stringToPort(dccArguments.at(argumentSize - 2), &ok); //-1 index, -1 filesize
        position = dccArguments.at( argumentSize - 1).toULongLong(); // -1 index
        fileName = recoverDccFileName(dccArguments, 2); //port filepos
    }

    DCC::TransferSend* dccTransfer = 0;
    if (ok)
    {
        dccTransfer = dtm->resumeUpload(connectionId(), sourceNick, fileName, ownPort, position);
    }

    if (dccTransfer)
    {
        appendMessageToFrontmost(i18n("DCC"),
                                 i18nc("%1 = file name, %2 = nickname of recipient, %3 = percentage of file size, %4 = file size",
                                       "Resuming upload of \"%1\" to %2 starting at %3% of %4...",
                                       fileName,
                                       sourceNick,
                                       QString::number(dccTransfer->getProgress()),
                                       (dccTransfer->getFileSize() == 0) ? i18n("unknown size") : KIO::convertSize(dccTransfer->getFileSize())));

        // fileName can't have " here
        if (fileName.contains(' '))
            fileName = '\"'+fileName+'\"';

        // FIXME: this operation should be done by TransferManager
        Konversation::OutputFilterResult result;
        if (passiv)
            result = getOutputFilter()->acceptPassiveResumeRequest( sourceNick, fileName, ownPort, position, token );
        else
            result = getOutputFilter()->acceptResumeRequest( sourceNick, fileName, ownPort, position );
        queue( result.toServer );
    }
    else
    {
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1 = file name, %2 = nickname",
                                       "Received invalid resume request for \"%1\" from %2.",
                                       fileName,
                                       sourceNick));
    }
}

void Server::rejectDccSendTransfer(const QString &sourceNick, const QStringList &dccArguments)
{
    DCC::TransferManager* dtm = Application::instance()->getDccTransferManager();

    //filename
    QString fileName = recoverDccFileName(dccArguments, 0);

    DCC::TransferSend* dccTransfer = dtm->rejectSend(connectionId(), sourceNick, fileName);

    if (!dccTransfer)
    {
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1 = file name, %2 = nickname",
                                       "Received invalid reject request for \"%1\" from %2.",
                                       fileName,
                                       sourceNick));
    }
}

void Server::rejectDccChat(const QString& sourceNick)
{
    DCC::TransferManager* dtm = Application::instance()->getDccTransferManager();

    DCC::Chat* dccChat = dtm->rejectChat(connectionId(), sourceNick);

    if (!dccChat)
    {
        appendMessageToFrontmost(i18n("Error"),
                                 i18nc("%1 = nickname",
                                       "Received invalid reject request from %1.",
                                       sourceNick));
    }
}

void Server::dccGetDone(DCC::Transfer* item)
{
    if (!item)
        return;

    if(item->getStatus() == DCC::Transfer::Done)
    {
        appendMessageToFrontmost(i18n("DCC"), i18nc("%1 = file name, %2 = nickname of sender",
            "Download of \"%1\" from %2 finished.", item->getFileName(), item->getPartnerNick()));
    }
    else if(item->getStatus() == DCC::Transfer::Failed)
    {
        appendMessageToFrontmost(i18n("DCC"), i18nc("%1 = file name, %2 = nickname of sender",
            "Download of \"%1\" from %2 failed. Reason: %3.", item->getFileName(),
            item->getPartnerNick(), item->getStatusDetail()));
    }
}

void Server::dccSendDone(DCC::Transfer* item)
{
    if (!item)
        return;

    if(item->getStatus() == DCC::Transfer::Done)
        appendMessageToFrontmost(i18n("DCC"), i18nc("%1 = file name, %2 = nickname of recipient",
            "Upload of \"%1\" to %2 finished.", item->getFileName(), item->getPartnerNick()));
    else if(item->getStatus() == DCC::Transfer::Failed)
        appendMessageToFrontmost(i18n("DCC"), i18nc("%1 = file name, %2 = nickname of recipient",
            "Upload of \"%1\" to %2 failed. Reason: %3.", item->getFileName(), item->getPartnerNick(),
            item->getStatusDetail()));
}

void Server::dccStatusChanged(DCC::Transfer *item, int newStatus, int oldStatus)
{
    if(!item)
        return;

    if ( item->getType() == DCC::Transfer::Send )
    {
        // when resuming, a message about the receiver's acceptance has been shown already, so suppress this message
        if ( newStatus == DCC::Transfer::Transferring && oldStatus == DCC::Transfer::WaitingRemote && !item->isResumed() )
            appendMessageToFrontmost( i18n( "DCC" ), i18nc( "%1 = file name, %2 nickname of recipient",
                "Sending \"%1\" to %2...", item->getFileName(), item->getPartnerNick() ) );
    }
    else  // type == Receive
    {
        if ( newStatus == DCC::Transfer::Transferring && !item->isResumed() )
        {
            appendMessageToFrontmost( i18n( "DCC" ),
                                        i18nc( "%1 = file name, %2 = file size, %3 = nickname of sender", "Downloading \"%1\" (%2) from %3...",
                                              item->getFileName(),
                                            ( item->getFileSize() == 0 ) ? i18n( "unknown size" ) : KIO::convertSize( item->getFileSize() ),
                                            item->getPartnerNick() ) );
        }
    }
}

void Server::removeQuery(Query* query)
{
    m_queryList.removeOne(query);
    query->deleteLater();
}

void Server::sendJoinCommand(const QString& name, const QString& password)
{
    Konversation::OutputFilterResult result = getOutputFilter()->parse(getNickname(),
        Preferences::self()->commandChar() + "JOIN " + name + ' ' + password, QString());
    queue(result.toServer);
}

Channel* Server::joinChannel(const QString& name, const QString& hostmask)
{
    // (re-)join channel, open a new panel if needed
    Channel* channel = getChannelByName(name);

    if (!channel)
    {
        channel=getViewContainer()->addChannel(this,name);
        Q_ASSERT(channel);
        channel->setNickname(getNickname());
        channel->indicateAway(m_away);

        if (getServerGroup())
        {
            Konversation::ChannelSettings channelSettings = getServerGroup()->channelByNameFromHistory(name);
            channel->setNotificationsEnabled(channelSettings.enableNotifications());
            getServerGroup()->appendChannelHistory(channelSettings);
        }

        m_channelList.append(channel);
        m_loweredChannelNameHash.insert(channel->getName().toLower(), channel);

        connect(channel,SIGNAL (sendFile()),this,SLOT (requestDccSend()) );
        connect(this, SIGNAL(nicknameChanged(QString)), channel, SLOT(setNickname(QString)));
    }

    addHostmaskToNick(getNickname(), hostmask);

    channel->joinNickname(getNickname());

    return channel;
}

void Server::removeChannel(Channel* channel)
{
    if (getServerGroup())
    {
        Konversation::ChannelSettings channelSettings = getServerGroup()->channelByNameFromHistory(channel->getName());
        channelSettings.setNotificationsEnabled(channel->notificationsEnabled());
        getServerGroup()->appendChannelHistory(channelSettings);
    }

    m_channelList.removeOne(channel);
    m_loweredChannelNameHash.remove(channel->getName().toLower());

    if (!isConnected())
        updateAutoJoin();
}

void Server::updateChannelMode(const QString &updater, const QString &channelName, char mode, bool plus, const QString &parameter)
{

    Channel* channel=getChannelByName(channelName);

    if(channel)                                   //Let the channel be verbose to the screen about the change, and update channelNick
        channel->updateMode(updater, mode, plus, parameter);
    // TODO: What is mode character for owner?
    // Answer from JOHNFLUX - I think that admin is the same as owner.  Channel.h has owner as "a"
    // "q" is the likely answer.. UnrealIRCd and euIRCd use it.
    // TODO these need to become dynamic
    QString userModes="vhoqa";                    // voice halfop op owner admin
    int modePos = userModes.indexOf(mode);
    if (modePos > 0)
    {
        m_nickListModel2->setNickMode(m_connectionId, channelName, parameter, mode, plus);
    }

    // Update channel ban list.
    if (mode == 'b')
    {
        if (plus)
        {
            QDateTime when;
            addBan(channelName, QString("%1 %2 %3").arg(parameter).arg(updater).arg(QDateTime::currentDateTime().toTime_t()));
        } else {
            removeBan(channelName, parameter);
        }
    }
}

void Server::updateChannelModeWidgets(const QString &channelName, char mode, const QString &parameter)
{
    Channel* channel=getChannelByName(channelName);
    if(channel) channel->updateModeWidgets(mode,true,parameter);
}

Channel* Server::getChannelByName(const QString& name)
{
    // Convert wanted channel name to lowercase
    QString wanted = name.toLower();

    if (m_loweredChannelNameHash.contains(wanted))
        return m_loweredChannelNameHash.value(wanted);

    return 0;
}

Query* Server::getQueryByName(const QString& name)
{
    // Convert wanted query name to lowercase
    QString wanted = name.toLower();

    // Traverse through list to find the query with "name"
    foreach (Query* lookQuery, m_queryList)
    {
        if(lookQuery->getName().toLower()==wanted) return lookQuery;
    }
    // No query by that name found? Must be a new query request. Return 0
    return 0;
}

ChatWindow* Server::getChannelOrQueryByName(const QString& name)
{
    ChatWindow* window = getChannelByName(name);

    if (!window)
        window = getQueryByName(name);

    return window;
}

/**
 * If not already online, changes a nick to the online state by creating
 * a NickInfo for it and emits various signals and messages for it.
 * This method should only be called for nicks on the watch list.
 * @param nickname           The nickname that is online.
 * @return                   Pointer to NickInfo for nick.
 */
void Server::announceWatchedNickOnline(const QString& nickname)
{
    appendMessageToFrontmost(i18nc("Message type", "Notify"), i18n("%1 is online (%2).", nickname, getServerName()), getStatusView());
}

void Server::announceWatchedNickOffline(const QString& nickname)
{
    appendMessageToFrontmost(i18nc("Message type", "Notify"), i18n("%1 went offline (%2).", nickname, getServerName()), getStatusView());
}

/**
 * Return true if the given nickname is on the watch list.
 */
bool Server::isWatchedNick(const QString& nickname)
{
    if (getServerGroup())
    {
        return getServerGroup()->notifyList().contains(nickname, Qt::CaseInsensitive);
    }

    return false;
}

Channel* Server::nickJoinsChannel(const QString &channelName, const QString &nickname, const QString &hostmask)
{
    Channel* outChannel = getChannelByName(channelName);
    if(outChannel)
    {
        addHostmaskToNick(nickname, hostmask);

        outChannel->joinNickname(nickname);
    }

    return outChannel;
}

void Server::addHostmaskToNick(const QString& sourceNick, const QString& sourceHostmask)
{
    m_nickListModel2->setNickHostmask(m_connectionId, sourceNick, sourceHostmask);
}

QString Server::getNickHostmask(const QString& sourceNick) const
{
    return m_nickListModel2->getNickHostmask(m_connectionId, sourceNick);
}

Channel* Server::removeNickFromChannel(const QString &channelName, const QString &nickname, const QString &reason, bool quit)
{
    Channel* outChannel = getChannelByName(channelName);

    if(outChannel)
    {
        outChannel->removeNick(nickname, reason, quit);
    }

    // If not listed in any channel, and not on query list, delete the NickInfo,
    // but only if not on the notify list.  ISON replies will take care of deleting
    // the NickInfo, if on the notify list.

    m_nickListModel2->removeNickFromChannel(m_connectionId, channelName, nickname);

    return outChannel;
}

void Server::nickWasKickedFromChannel(const QString &channelName, const QString &nickname, const QString &kicker, const QString &reason)
{
    Channel* outChannel = getChannelByName(channelName);

    if(outChannel)
    {
        outChannel->kickNick(nickname, kicker, reason);
    }
}

void Server::removeNickFromServer(const QString &nickname,const QString &reason)
{
    foreach (Channel* channel, m_channelList)
    {
        removeNickFromChannel(channel->getName(),nickname,reason,true);
    }

    Query* query = getQueryByName(nickname);
    if (query) query->quitNick(reason);
}

void Server::renameNick(const QString &nickname, const QString &newNick)
{
    m_nickListModel2->setNewNickname(m_connectionId, nickname, newNick);

    if(nickname.isEmpty() || newNick.isEmpty())
    {
        kDebug() << "called with empty strings!  Trying to rename '" << nickname << "' to '" << newNick << "'";
        return;
    }

    // If this was our own nickchange, tell our server object about it
    if (nickname == getNickname())
        setNickname(newNick);

    //The rest of the code below allows the channels to echo to the user to tell them that the nick has changed.

    // Rename the nick in every channel they are in
    foreach (Channel* channel, m_channelList)
    {

        // All we do is notify that the nick has been renamed.. we haven't actually renamed it yet
        channel->nickRenamed(nickname, newNick);
    }

    // We had an encrypt conversation with the user that changed his nick, lets copy the key to the new nick and remove the old nick
    #ifdef HAVE_QCA2
    QByteArray userKey = getKeyForRecipient(nickname);

    if (!userKey.isEmpty())
    {
        setKeyForRecipient(newNick, userKey);
        m_keyHash.remove(nickname.toLower());
    }
    #endif

}

void Server::userhost(const QString& nick,const QString& hostmask,bool away,bool /* ircOp */)
{
    m_nickListModel2->setNickAway(m_connectionId, nick, away);
    addHostmaskToNick(nick, hostmask);
    // remember my IP for DCC things
                                                  // myself
    if (m_ownIpByUserhost.isEmpty() && nick == getNickname())
    {
        QString myhost = hostmask.section('@', 1);
        // Use async lookup else you will be blocking GUI badly
        QHostInfo::lookupHost(myhost, this, SLOT(gotOwnResolvedHostByUserhost(QHostInfo)));
    }
}

void Server::gotOwnResolvedHostByUserhost(const QHostInfo& res)
{
    if ( res.error() == QHostInfo::NoError && !res.addresses().isEmpty() )
        m_ownIpByUserhost = res.addresses().first().toString();
    else
        kDebug() << "Got error: " << res.errorString();
}

void Server::appendServerMessageToChannel(const QString& channel,const QString& type,const QString& message)
{
    Channel* outChannel = getChannelByName(channel);
    if (outChannel) outChannel->appendServerMessage(type,message);
}

void Server::appendCommandMessageToChannel(const QString& channel,const QString& command,const QString& message, bool highlight, bool parseURL)
{
    Channel* outChannel = getChannelByName(channel);
    if (outChannel)
    {
        outChannel->appendCommandMessage(command,message,parseURL,!highlight);
    }
    else
    {
        appendStatusMessage(command, QString("%1 %2").arg(channel).arg(message));
    }
}

void Server::appendStatusMessage(const QString& type,const QString& message)
{
    getStatusView()->appendServerMessage(type,message);
}

void Server::appendMessageToFrontmost(const QString& type,const QString& message, bool parseURL)
{
    getViewContainer()->appendToFrontmost(type, message, getStatusView(), parseURL);
}

void Server::setNickname(const QString &newNickname)
{
    m_nickname = newNickname;
    m_loweredNickname = newNickname.toLower();
    if (!m_nickListModel->stringList().contains(newNickname)) {
        m_nickListModel->insertRows(m_nickListModel->rowCount(), 1);
        m_nickListModel->setData(m_nickListModel->index(m_nickListModel->rowCount() -1 , 0), newNickname, Qt::DisplayRole);
    }
    emit nicknameChanged(newNickname);
}

void Server::setChannelTopic(const QString &channel, const QString &newTopic)
{
    Channel* outChannel = getChannelByName(channel);
    if(outChannel)
    {
        // encoding stuff is done in send()
        outChannel->setTopic(newTopic);
    }
}

                                                  // Overloaded
void Server::setChannelTopic(const QString& nickname, const QString &channel, const QString &newTopic)
{
    Channel* outChannel = getChannelByName(channel);
    if(outChannel)
    {
        // encoding stuff is done in send()
        outChannel->setTopic(nickname,newTopic);
    }
}

void Server::setTopicAuthor(const QString& channel, const QString& author, QDateTime time)
{
    Channel* outChannel = getChannelByName(channel);
    if(outChannel)
        outChannel->setTopicAuthor(author, time);
}

void Server::endOfWho(const QString& target)
{
    Channel* channel = getChannelByName(target);
    if(channel)
        channel->scheduleAutoWho();
}

void Server::endOfNames(const QString& target)
{
    Channel* channel = getChannelByName(target);
    if(channel)
        channel->endOfNames();
}

bool Server::isNickname(const QString &compare) const
{
    return (m_nickname == compare);
}

QString Server::getNickname() const
{
    return m_nickname;
}

QString Server::loweredNickname() const
{
    return m_loweredNickname;
}

QString Server::parseWildcards(const QString& toParse, ChatWindow* context, QStringList nicks)
{
    QString inputLineText;

    if (context && context->getInputBar())
        inputLineText = context->getInputBar()->toPlainText();

    if (!context)
        return parseWildcards(toParse, getNickname(), QString(), QString(), QString(), QString());
    else if (context->getType() == ChatWindow::Channel)
    {
        Channel* channel = static_cast<Channel*>(context);

        return parseWildcards(toParse, getNickname(), context->getName(), channel->getPassword(),
            nicks.count() ? nicks : channel->getSelectedNickList(), inputLineText);
    }
    else if (context->getType() == ChatWindow::Query)
        return parseWildcards(toParse, getNickname(), context->getName(), QString(), context->getName(), inputLineText);

    return parseWildcards(toParse, getNickname(), context->getName(), QString(), QString(), inputLineText);
}

QString Server::parseWildcards(const QString& toParse,
const QString& sender,
const QString& channelName,
const QString& channelKey,
const QString& nick,
const QString& inputLineText)
{
    return parseWildcards(toParse, sender, channelName, channelKey, nick.split(' ', QString::SkipEmptyParts), inputLineText);
}

QString Server::parseWildcards(const QString& toParse,
const QString& sender,
const QString& channelName,
const QString& channelKey,
const QStringList& nickList,
const QString& inputLineText
)
{
    // store the parsed version
    QString out;

    // default separator
    QString separator(" ");

    int index = 0, found = 0;
    QChar toExpand;

    while ((found = toParse.indexOf('%', index)) != -1)
    {
                                                  // append part before the %
        out.append(toParse.mid(index,found-index));
        index = found + 1;                        // skip the part before, including %
        if (index >= (int)toParse.length())
            break;                                // % was the last char (not valid)
        toExpand = toParse.at(index++);
        if (toExpand == 's')
        {
            found = toParse.indexOf('%', index);
            if (found == -1)                      // no other % (not valid)
                break;
            separator = toParse.mid(index,found-index);
            index = found + 1;                    // skip separator, including %
        }
        else if (toExpand == 'u')
        {
            out.append(nickList.join(separator));
        }
        else if (toExpand == 'c')
        {
            if(!channelName.isEmpty())
                out.append(channelName);
        }
        else if (toExpand == 'o')
        {
            out.append(sender);
        }
        else if (toExpand == 'k')
        {
            if(!channelKey.isEmpty())
                out.append(channelKey);
        }
        else if (toExpand == 'K')
        {
            if(getConnectionSettings().server().password().isEmpty())
                out.append(getConnectionSettings().server().password());
        }
        else if (toExpand == 'n')
        {
            out.append("\n");
        }
        else if (toExpand == 'p')
        {
            out.append("%");
        }
        else if (toExpand == 'i')
        {
            out.append(inputLineText);
        }
    }

                                                  // append last part
    out.append(toParse.mid(index,toParse.length()-index));
    return out;
}

void Server::sendToAllChannels(const QString &text)
{
    // Send a message to all channels we are in
    foreach (Channel* channel, m_channelList)
    {
        channel->sendText(text);
    }
}

void Server::invitation(const QString& nick,const QString& channel)
{
    if(!m_inviteDialog)
    {
        KDialog::ButtonCode buttonCode = KDialog::Cancel;

        if(!InviteDialog::shouldBeShown(buttonCode))
        {
            if (buttonCode == KDialog::Ok)
                sendJoinCommand(channel);

            return;
        }

        m_inviteDialog = new InviteDialog (getViewContainer()->getWindow());
        connect(m_inviteDialog, SIGNAL(joinChannelsRequested(QString)),
                this, SLOT(sendJoinCommand(QString)));
    }

    m_inviteDialog->show();
    m_inviteDialog->raise();

    m_inviteDialog->addInvite(nick, channel);
}

void Server::scriptNotFound(const QString& name)
{
    appendMessageToFrontmost(i18n("D-Bus"),i18n("Error: Could not find script \"%1\".", name));
}

void Server::scriptExecutionError(const QString& name)
{
    appendMessageToFrontmost(i18n("D-Bus"),i18n("Error: Could not execute script \"%1\". Check file permissions.", name));
}

bool Server::isAChannel(const QString &channel) const
{
    if (channel.isEmpty()) return false;

    return (getChannelTypes().contains(channel.at(0)) > 0);
}

void Server::addRawLog(bool show)
{
    if (!m_rawLog) m_rawLog = getViewContainer()->addRawLog(this);

    connect(this, SIGNAL(serverOnline(bool)), m_rawLog, SLOT(serverOnline(bool)));

    // bring raw log to front since the main window does not do this for us
    if (show) emit showView(m_rawLog);
}

void Server::closeRawLog()
{
    delete m_rawLog;
}

void Server::requestOpenChannelListPanel(const QString& filter)
{
    getViewContainer()->openChannelList(this, filter, true);
}

ChannelListPanel* Server::addChannelListPanel()
{
    if(!m_channelListPanel)
    {
        m_channelListPanel = getViewContainer()->addChannelListPanel(this);

        connect(&m_inputFilter, SIGNAL(endOfChannelList()), m_channelListPanel, SLOT(endOfChannelList()));
        connect(m_channelListPanel, SIGNAL(refreshChannelList()), this, SLOT(requestChannelList()));
        connect(m_channelListPanel, SIGNAL(joinChannel(QString)), this, SLOT(sendJoinCommand(QString)));
        connect(this, SIGNAL(serverOnline(bool)), m_channelListPanel, SLOT(serverOnline(bool)));
    }

    return m_channelListPanel;
}

void Server::addToChannelList(const QString& channel, int users, const QString& topic)
{
    addChannelListPanel();
    m_channelListPanel->addToChannelList(channel, users, topic);
}

ChannelListPanel* Server::getChannelListPanel() const
{
    return m_channelListPanel;
}

void Server::closeChannelListPanel()
{
    delete m_channelListPanel;
}

void Server::updateAutoJoin(Konversation::ChannelList channels)
{
    Konversation::ChannelList tmpList;

    if (!channels.isEmpty())
    {
        foreach (const ChannelSettings& cs, channels)
        {
            tmpList << cs;
        }
    }
    else if (m_channelList.isEmpty() && getServerGroup())
        tmpList = getServerGroup()->channelList();
    else
    {
        foreach (Channel* channel, m_channelList)
        {
            tmpList << channel->channelSettings();
        }
    }

    if (!tmpList.isEmpty())
    {
        setAutoJoinCommands(generateJoinCommand(tmpList));
        setAutoJoin(!m_autoJoinCommands.isEmpty());
    }
    else
    {
        m_autoJoinCommands.clear();
        setAutoJoin(false);
    }
}


QStringList Server::generateJoinCommand(const Konversation::ChannelList &tmpList)
{
    QStringList channels;
    QStringList passwords;
    QStringList joinCommands;
    uint length = 0;

    Konversation::ChannelList::const_iterator it;

    for (it = tmpList.constBegin(); it != tmpList.constEnd(); ++it)
    {
        QString channel = (*it).name();

        // Only add the channel to the JOIN command if it has a valid channel name.
        if (isAChannel(channel))
        {
            QString password = ((*it).password().isEmpty() ? "." : (*it).password());

            uint currentLength = getIdentity()->getCodec()->fromUnicode(channel).length();
            currentLength += getIdentity()->getCodec()->fromUnicode(password).length();

            //channels.count() and passwords.count() account for the commas
            if (length + currentLength + 6 + channels.count() + passwords.count() >= 512) // 6: "JOIN " plus separating space between chans and pws.
            {
                while (!passwords.isEmpty() && passwords.last() == ".") passwords.pop_back();

                joinCommands << "JOIN " + channels.join(",") + ' ' + passwords.join(",");

                channels.clear();
                passwords.clear();

                length = 0;
            }

            length += currentLength;

            channels << channel;
            passwords << password;
        }
    }

    while (!passwords.isEmpty() && passwords.last() == ".") passwords.pop_back();

    // Even if the given tmpList contained entries they might have been filtered
    // out by the isAChannel() check.
    if (!channels.isEmpty())
    {
        joinCommands << "JOIN " + channels.join(",") + ' ' + passwords.join(",");
    }

    return joinCommands;
}

ViewContainer* Server::getViewContainer() const
{
    Application* konvApp = static_cast<Application *>(kapp);
    return konvApp->getMainWindow()->getViewContainer();
}


bool Server::getUseSSL() const
{
        if ( m_socket )
            return ( m_socket->encryptionMode() != KTcpSocket::UnencryptedMode );
        else
            return false;
}


QString Server::getSSLInfo() const
{
//     SSLSocket* sslsocket = dynamic_cast<SSLSocket*>(m_socket);

//     if(sslsocket)
//         return sslsocket->details();

    return QString();
}


void Server::sendMultiServerCommand(const QString& command, const QString& parameter)
{
    emit multiServerCommand(command, parameter);
}

void Server::executeMultiServerCommand(const QString& command, const QString& parameter)
{
    if (command == "msg")
        sendToAllChannelsAndQueries(parameter);
    else
        sendToAllChannelsAndQueries(Preferences::self()->commandChar() + command + ' ' + parameter);
}

void Server::sendToAllChannelsAndQueries(const QString& text)
{
    // Send a message to all channels we are in
    foreach (Channel* channel, m_channelList)
    {
        channel->sendText(text);
    }

    // Send a message to all queries we are in
    foreach (Query* query, m_queryList)
    {
        query->sendText(text);
    }
}

void Server::requestAway(const QString& reason)
{
    QString awayReason = reason;

    IdentityPtr identity = getIdentity();

    if (awayReason.isEmpty() && identity)
        awayReason = identity->getAwayMessage();

    // Fallback in case the identity has no away message set.
    if (awayReason.isEmpty())
        awayReason = i18n("Gone away for now");

    setAwayReason(awayReason);

    queue("AWAY :" + awayReason);
}

void Server::requestUnaway()
{
    queue("AWAY");
}

void Server::setAway(bool away)
{
    m_nickListModel2->setNickAway(m_connectionId, getNickname(), away);

    IdentityPtr identity = getIdentity();

    if (away)
    {
        if (!m_away) startAwayTimer();

        m_away = true;

        emit awayState(true);

        if (identity && !identity->getAwayNickname().isEmpty() && identity->getAwayNickname() != getNickname())
        {
            m_nonAwayNick = getNickname();
            queue("NICK " + getIdentity()->getAwayNickname());
        }

        if (!m_awayReason.isEmpty())
            appendMessageToFrontmost(i18n("Away"), i18n("You are now marked as being away (reason: %1).",m_awayReason));
        else
           appendMessageToFrontmost(i18n("Away"), i18n("You are now marked as being away."));

        if (identity && identity->getRunAwayCommands())
        {
            QString message = identity->getAwayCommand();
            sendToAllChannels(message.replace(QRegExp("%s", Qt::CaseInsensitive), m_awayReason));
        }

        if (identity && identity->getInsertRememberLineOnAway())
            emit awayInsertRememberLine(this);
    }
    else
    {
        m_awayReason.clear();

        emit awayState(false);

        if (!identity->getAwayNickname().isEmpty() && !m_nonAwayNick.isEmpty())
        {
            queue("NICK " + m_nonAwayNick);
            m_nonAwayNick.clear();
        }

        if (m_away)
        {
            appendMessageToFrontmost(i18n("Away"), i18n("You are no longer marked as being away."));

            if (identity && identity->getRunAwayCommands())
            {
                QString message = identity->getReturnCommand();
                sendToAllChannels(message.replace(QRegExp("%t", Qt::CaseInsensitive), awayTime()));
            }
        }
        else
            appendMessageToFrontmost(i18n("Away"), i18n("You are not marked as being away."));

        m_away = false;
    }
}

QString Server::awayTime() const
{
    QString retVal;

    if (m_away)
    {
        int diff = QDateTime::currentDateTime().toTime_t() - m_awayTime;
        int num = diff / 3600;

        if (num < 10)
            retVal = '0' + QString::number(num) + ':';
        else
            retVal = QString::number(num) + ':';

        num = (diff % 3600) / 60;

        if (num < 10) retVal += '0';

        retVal += QString::number(num) + ':';

        num = (diff % 3600) % 60;

        if (num < 10) retVal += '0';

        retVal += QString::number(num);
    }
    else
        retVal = "00:00:00";

    return retVal;
}

void Server::startAwayTimer()
{
    m_awayTime = QDateTime::currentDateTime().toTime_t();
}

void Server::enableIdentifyMsg(bool enabled)
{
    m_identifyMsg = enabled;
}

bool Server::identifyMsgEnabled()
{
    return m_identifyMsg;
}

void Server::addBan(const QString &channel, const QString &ban)
{
    Channel* outChannel = getChannelByName(channel);
    if(outChannel)
    {
        outChannel->addBan(ban);
    }
}

void Server::removeBan(const QString &channel, const QString &ban)
{
    Channel* outChannel = getChannelByName(channel);
    if(outChannel)
    {
        outChannel->removeBan(ban);
    }
}

void Server::sendPing()
{
    //WHO ourselves once a minute in case the irc server has changed our
    //hostmask, such as what happens when a Freenode cloak is activated.
    //It might be more intelligent to only do this when there is text
    //in the inputbox. Kinda changes this into a "do minutely"
    //queue :-)
    QStringList ql;
    ql << "PING LAG" + QTime::currentTime().toString("hhmmss");
    getInputFilter()->setAutomaticRequest("WHO", getNickname(), true);
    ql << "WHO " + getNickname();
    queueList(ql, HighPriority);

    m_lagTime.start();
    m_inputFilter.setLagMeasuring(true);
    m_pingResponseTimer.start(1000 /*1 sec*/);
}

void Server::pongReceived()
{
    // ignore unrequested PONGs
    if (m_pingSendTimer.isActive())
        return;

    m_currentLag = m_lagTime.elapsed();
    m_inputFilter.setLagMeasuring(false);
    m_pingResponseTimer.stop();

    emit serverLag(this, m_currentLag);

    // Send another PING in 60 seconds
    m_pingSendTimer.start(60000 /*60 sec*/);
}

void Server::updateLongPongLag()
{
    if (isSocketConnected())
    {
        m_currentLag = m_lagTime.elapsed();
        emit tooLongLag(this, m_currentLag);
        // kDebug() << "Current lag: " << currentLag;

        if (m_currentLag > (Preferences::self()->maximumLagTime() * 1000))
            m_socket->close();
    }
}

void Server::updateEncoding()
{
    if(getViewContainer() && getViewContainer()->getFrontView())
        getViewContainer()->updateViewEncoding(getViewContainer()->getFrontView());
}

#ifdef HAVE_QCA2
void Server::initKeyExchange(const QString &receiver)
{
    Query* query;
    if (getQueryByName(receiver))
    {
        query = getQueryByName(receiver);
    }
    else
    {
        query = addQuery(receiver, true);
    }

    Konversation::Cipher* cipher = query->getCipher();

    QByteArray pubKey = cipher->initKeyExchange();
    if(pubKey.isEmpty())
    {
        appendMessageToFrontmost(i18n("Error"), i18n("Failed to initiate key exchange with %1.", receiver));
    }
    else
    {
        queue("NOTICE "+receiver+" :DH1080_INIT "+pubKey);
    }
}

void Server::parseInitKeyX(const QString &sender, const QString &remoteKey)
{
    if (!Konversation::Cipher::isFeatureAvailable(Konversation::Cipher::DH))
    {
        appendMessageToFrontmost(i18n("Error"), i18n("Unable to perform key exchange with %1.", sender)
            + ' ' + Konversation::Cipher::runtimeError());

        return;
    }

    //TODO ask the user to accept without blocking
    Query* query;
    if (getQueryByName(sender))
    {
        query = getQueryByName(sender);
    }
    else
    {
        query = addQuery(sender, false);
    }

    Konversation::Cipher* cipher = query->getCipher();

    QByteArray pubKey = cipher->parseInitKeyX(remoteKey.toLocal8Bit());

    if (pubKey.isEmpty())
    {
        appendMessageToFrontmost(i18n("Error"), i18n("Failed to parse the DH1080_INIT of %1. Key exchange failed.", sender));
    }
    else
    {
        setKeyForRecipient(sender, cipher->key());
        query->setEncryptedOutput(true);
        appendMessageToFrontmost(i18n("Notice"), i18n("Your key is set and your messages will now be encrypted, sending DH1080_FINISH to %1.", sender));
        queue("NOTICE "+sender+" :DH1080_FINISH "+pubKey);
    }
}

void Server::parseFinishKeyX(const QString &sender, const QString &remoteKey)
{
    Query* query;
    if (getQueryByName(sender))
    {
        query = getQueryByName(sender);
    }
    else
        return;

    if (!Konversation::Cipher::isFeatureAvailable(Konversation::Cipher::DH))
    {
        appendMessageToFrontmost(i18n("Error"), i18n("Unable to complete key exchange with %1.", sender)
            + ' ' + Konversation::Cipher::runtimeError());

        return;
    }

    Konversation::Cipher* cipher = query->getCipher();

    if (cipher->parseFinishKeyX(remoteKey.toLocal8Bit()))
    {
        setKeyForRecipient(sender,cipher->key());
        query->setEncryptedOutput(true);
        appendMessageToFrontmost(i18n("Notice"), i18n("Successfully parsed DH1080_FINISH sent by %1. Your key is set and your messages will now be encrypted.", sender));
    }
    else
    {
        appendMessageToFrontmost(i18n("Error"), i18n("Failed to parse DH1080_FINISH sent by %1. Key exchange failed.", sender));
    }
}
#endif

QAbstractItemModel* Server::nickListModel() const
{
    return m_nickListModel;
}

NickListModel* Server::nickListModel2() const
{
    return m_nickListModel2;
}

void Server::involuntaryQuit()
{
    if((m_connectionState == Konversation::SSConnected || m_connectionState == Konversation::SSConnecting) &&
       (m_socket->peerAddress() != QHostAddress(QHostAddress::LocalHost) && m_socket->peerAddress() != QHostAddress(QHostAddress::LocalHostIPv6)))
    {
        quitServer();
        updateConnectionState(Konversation::SSInvoluntarilyDisconnected);
    }
}

void Server::reconnectInvoluntary()
{
    if(m_connectionState == Konversation::SSInvoluntarilyDisconnected)
        reconnectServer();
}

#include "server.moc"

// kate: space-indent on; tab-width 4; indent-width 4; mixed-indent off; replace-tabs on;
// vim: set et sw=4 ts=4 cino=l1,cs,U1:
