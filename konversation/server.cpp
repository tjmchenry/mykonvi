/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  server.cpp  -  description
  begin:     Sun Jan 20 2002
  copyright: (C) 2002 by Dario Abatianni
  email:     eisfuchs@tigress.com
*/

#include <unistd.h>
#include <sys/socket.h>

#ifdef __linux__
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,50)
typedef unsigned long long __u64;
#endif
#endif

#include <netinet/in.h>

#include <qregexp.h>
#include <qhostaddress.h>
#include <qtextcodec.h>
#include <qdatetime.h>

#include <kapp.h>
#include <klocale.h>
#include <kdebug.h>
#include <kfiledialog.h>
#include <kmessagebox.h>
#if KDE_IS_VERSION(3,2,90)
#include <kresolver.h>
#endif
#include <kstringhandler.h>
#include <kdeversion.h>

#include "server.h"
#include "query.h"
#include "channel.h"
#include "ircserversocket.h"
#include "konversationapplication.h"
#include "dccpanel.h"
#include "dcctransfer.h"
#include "dcctransfersend.h"
#include "dcctransferrecv.h"
#include "dccrecipientdialog.h"
#include "nick.h"
#include "konversationmainwindow.h"
#include "statuspanel.h"
#include "rawlog.h"
#include "channellistpanel.h"
#include "scriptlauncher.h"
#include "konvidebug.h"

#include "addressbook.h"

#ifdef KDE_IS_VERSION
#if KDE_IS_VERSION(3,1,1)
#define USE_KNOTIFY
#endif

#ifdef USE_KNOTIFY
#include <knotifyclient.h>
#endif
#endif

Server::Server(KonversationMainWindow* newMainWindow,int id)
{
  identity=0;
  tryNickNumber=0;
  checkTime=0;
  reconnectCounter=0;
  currentLag=0;
  rawLog=0;
  channelListPanel=0;
  alreadyConnected=false;
  rejoinChannels=false;
  connecting=false;
  isAway = false;

  timerInterval=1;  // flood protection

  QStringList serverEntry=QStringList::split(',',KonversationApplication::preferences.getServerById(id),true);
  setIdentity(KonversationApplication::preferences.getIdentityByName(serverEntry[7]));

  setName(QString("server_"+serverEntry[1]).ascii());

  setMainWindow(newMainWindow);

  serverGroup=serverEntry[0];
  serverName=serverEntry[1];
  serverPort=serverEntry[2].toInt();
  serverKey=serverEntry[3];

  resolver.setRecipient(this);
  installEventFilter(this);

  lastDccDir=QString::null;

  statusView=getMainWindow()->addStatusView(this);
  if(KonversationApplication::preferences.getRawLog()) addRawLog(false);

  setNickname(getIdentity()->getNickname(tryNickNumber));
  obtainNickInfo(getNickname());

  bot=getIdentity()->getBot();
  botPassword=getIdentity()->getPassword();

  if(!serverEntry[4].isEmpty())
  {
    setAutoJoin(true);
    setAutoJoinChannel(serverEntry[4]);
    setAutoJoinChannelKey(serverEntry[5]);
  }
  else autoJoin=false;

  autoRejoin=KonversationApplication::preferences.getAutoRejoin();
  autoReconnect=KonversationApplication::preferences.getAutoReconnect();

  if(!serverEntry[8].isEmpty())
  {
    connectCommands = QStringList::split(";", serverEntry[8]);
  }

  if(KonversationApplication::preferences.getPreShellCommand() != QString::null) {
    statusView->appendServerMessage("Info","Running preconfigured command...");
    
    connect( &preShellCommand,SIGNAL(processExited(KProcess*)),this,SLOT(preShellCommandExited(KProcess*)));

    QString command = KonversationApplication::preferences.getPreShellCommand();
    QStringList commandList = QStringList::split(" ",command);
    
    for (QStringList::Iterator it = commandList.begin(); it != commandList.end(); ++it){
      preShellCommand << *it;
    }

    preShellCommand.start(); // Non blocking
  
  }
  else
    connectToIRCServer();

  // don't delete items when they are removed
  channelList.setAutoDelete(false);
  // For /msg query completion
  completeQueryPosition=0;

  inputFilter.setServer(this);
  outputFilter = new Konversation::OutputFilter(this);

  notifyTimer.setName("notify_timer");
  incomingTimer.setName("incoming_timer");
  incomingTimer.start(10);

  outgoingTimer.setName("outgoing_timer");
  outgoingTimer.start(timerInterval);

  m_scriptLauncher = new ScriptLauncher(this);

  if(KonversationApplication::preferences.getPreShellCommand() == QString::null)
    connectSignals();

  emit serverOnline(false);
}

//FIXME: remove code duplicates by introducing some new method
Server::Server(KonversationMainWindow* mainWindow,const QString& hostName,const QString& port,const QString& nick,const QString& password)
{
  autoJoin=false;
  identity=0;
  tryNickNumber=0;
  checkTime=0;
  reconnectCounter=0;
  currentLag=0;
  rawLog=0;
  channelListPanel=0;
  alreadyConnected=false;
  rejoinChannels=false;
  connecting=false;

  timerInterval=1;  // flood protection

  setIdentity(KonversationApplication::preferences.getIdentityByName("Default"));
  setName(hostName.ascii());
  setMainWindow(mainWindow);

  serverGroup=hostName;
  serverName=hostName;
  serverPort=port.toInt();
  serverKey=password;

  resolver.setRecipient(this);
  installEventFilter(this);

  lastDccDir=QString::null;

  statusView=getMainWindow()->addStatusView(this);
  if(KonversationApplication::preferences.getRawLog()) addRawLog(false);
  setNickname(nick);

  if(KonversationApplication::preferences.getPreShellCommand() != QString::null) {
    statusView->appendServerMessage("Info","Running preconfigured command...");

    connect( &preShellCommand,SIGNAL(processExited(KProcess*)),this,SLOT(preShellCommandExited(KProcess*)));

    QString command = KonversationApplication::preferences.getPreShellCommand();
    QStringList commandList = QStringList::split(" ",command);

    for ( QStringList::Iterator it = commandList.begin(); it != commandList.end(); ++it ) {
      preShellCommand << *it;
    }

    preShellCommand.start(); // Non blocking
  
  }
  else
    connectToIRCServer();

   // don't delete items when they are removed
  channelList.setAutoDelete(false);
  // For /msg query completion
  completeQueryPosition=0;

  inputFilter.setServer(this);
  outputFilter = new Konversation::OutputFilter(this);

  notifyTimer.setName("notify_timer");
  incomingTimer.setName("incoming_timer");
  incomingTimer.start(10);

  outgoingTimer.setName("outgoing_timer");
  outgoingTimer.start(timerInterval);

  m_scriptLauncher = new ScriptLauncher(this);

  if(KonversationApplication::preferences.getPreShellCommand() == QString::null)
    connectSignals();

  emit serverOnline(false);
 
}

Server::~Server()
{
  kdDebug() << "Server::~Server(" << getServerName() << ")" << endl;

  // clear nicks online
  emit nicksNowOnline(this,QStringList(),true);
  // Make sure no signals get sent to a soon to be dying Server Window
  serverSocket.blockSignals(true);
  // Send out the last messages (usually the /QUIT)
  send();

#ifdef USE_MDI
/*
  while(channelList.count())
  {
    Channel* lookChannel=channelList.at(0);
    lookChannel->removeNick(getNickname(),getIdentity()->getPartReason(),true);
  }
  kdDebug() << "Server::~Server(): channel list count now: " << channelList.count() << endl;

  while(queryList.count())
  {
    queryList.at(0)->closeYourself();
  }
*/

  emit serverQuit(getIdentity()->getPartReason());
#else
  closeRawLog();
  closeChannelListPanel();

  channelList.setAutoDelete(true);
  channelList.clear();

  queryList.setAutoDelete(true);
  queryList.clear();
#endif

  // kill resolver thread if it's still running
#if KDE_VERSION >= 310
  if(resolver.running()) resolver.terminate();
#else
  if(resolver.running()) resolver.exit();
#endif

  // Delete all the NickInfos and ChannelNick structures.
  allNicks.clear();
  ChannelMembershipMap::Iterator it;
  for ( it = joinedChannels.begin(); it != joinedChannels.end(); ++it ) delete it.data();
  joinedChannels.clear();
  for ( it = unjoinedChannels.begin(); it != unjoinedChannels.end(); ++it ) delete it.data();
  unjoinedChannels.clear();
  nicknamesOnline.clear();
  nicknamesOffline.clear();
  queryNicks.clear();

  // notify KonversationApplication that this server is gone
  emit deleted(this);
}

void Server::connectSignals()
{
  connect(&incomingTimer,SIGNAL(timeout()),
                    this,SLOT  (processIncomingData()) );

  connect(&outgoingTimer,SIGNAL(timeout()),
                    this,SLOT  (send()) );

  connect(&unlockTimer,SIGNAL(timeout()),
                  this,SLOT  (unlockSending()) );

  connect(outputFilter,SIGNAL (openQuery(const QString&,const QString&)),
                   this,SLOT   (addQuery(const QString&,const QString&)) );
  connect(outputFilter,SIGNAL (requestDccSend()),
                   this,SLOT   (requestDccSend()) );
  connect(outputFilter,SIGNAL (requestDccSend(const QString&)),
                   this,SLOT   (requestDccSend(const QString&)) );
  connect(outputFilter, SIGNAL(multiServerCommand(const QString&, const QString&)),
    this, SLOT(sendMultiServerCommand(const QString&, const QString&)));
  connect(outputFilter, SIGNAL(reconnectServer()), this, SLOT(reconnect()));

  connect(outputFilter,SIGNAL (openDccPanel()),
            this,SLOT   (requestDccPanel()) );
  connect(outputFilter,SIGNAL (closeDccPanel()),
            this,SLOT   (requestCloseDccPanel()) );
  connect(outputFilter,SIGNAL (openDccSend(const QString &, const QString &)),
            this,SLOT   (addDccSend(const QString &, const QString &)) );
  connect(outputFilter,SIGNAL (requestDccChat(const QString &)),
            this,SLOT   (requestDccChat(const QString &)) );
  connect(outputFilter,SIGNAL (connectToServer(const QString&, const QString&, const QString&)),
            this,SLOT   (connectToNewServer(const QString&, const QString&, const QString&)));

  connect(outputFilter,SIGNAL (openKonsolePanel()),
            this,SLOT   (requestKonsolePanel()) );

  connect(outputFilter,SIGNAL (sendToAllChannels(const QString&)),
            this,SLOT   (sendToAllChannels(const QString&)) );
  connect(outputFilter,SIGNAL (banUsers(const QStringList&,const QString&,const QString&)),
            this,SLOT   (requestBan(const QStringList&,const QString&,const QString&)) );
  connect(outputFilter,SIGNAL (unbanUsers(const QString&,const QString&)),
            this,SLOT   (requestUnban(const QString&,const QString&)) );

  connect(outputFilter,SIGNAL (openRawLog(bool)), this,SLOT (addRawLog(bool)) );
  connect(outputFilter,SIGNAL (closeRawLog()),this,SLOT (closeRawLog()) );

  connect(&notifyTimer,SIGNAL(timeout()),
                  this,SLOT  (notifyTimeout()) );
  connect(&notifyCheckTimer,SIGNAL(timeout()),
                  this,SLOT  (notifyCheckTimeout()) );

  connect(&inputFilter,SIGNAL(welcome()),
                  this,SLOT  (connectionEstablished()) );
  connect(&inputFilter,SIGNAL(notifyResponse(const QString&)),
                  this,SLOT  (notifyResponse(const QString&)) );
  connect(&inputFilter,SIGNAL(addDccGet(const QString&, const QStringList&)),
                  this,SLOT  (addDccGet(const QString&, const QStringList&)) );
  connect(&inputFilter,SIGNAL(resumeDccGetTransfer(const QString&, const QStringList&)),
                  this,SLOT  (resumeDccGetTransfer(const QString&, const QStringList&)) );
  connect(&inputFilter,SIGNAL(resumeDccSendTransfer(const QString&, const QStringList&)),
                  this,SLOT  (resumeDccSendTransfer(const QString&, const QStringList&)) );
  connect(&inputFilter,SIGNAL(userhost(const QString&,const QString&,bool,bool)),
                  this,SLOT  (userhost(const QString&,const QString&,bool,bool)) );
  connect(&inputFilter,SIGNAL(topicAuthor(const QString&,const QString&)),
                  this,SLOT  (setTopicAuthor(const QString&,const QString&)) );
  connect(&inputFilter,SIGNAL(addChannelListPanel()),
                  this,SLOT  (addChannelListPanel()) );
  connect(&inputFilter,SIGNAL(invitation(const QString&,const QString&)),
                  this,SLOT  (invitation(const QString&,const QString&)) );

  connect(&inputFilter,SIGNAL (away()),this,SLOT (away()) );
  connect(&inputFilter,SIGNAL (unAway()),this,SLOT (unAway()) );
  connect(&inputFilter,SIGNAL (addDccChat(const QString&,const QString&,const QString&,const QStringList&,bool)),
         getMainWindow(),SLOT (addDccChat(const QString&,const QString&,const QString&,const QStringList&,bool)) );

  connect(this,SIGNAL(serverLag(Server*,int)),getMainWindow(),SLOT(updateLag(Server*,int)) );
  connect(this,SIGNAL(tooLongLag(Server*,int)),getMainWindow(),SLOT(tooLongLag(Server*,int)) );
  connect(this,SIGNAL(resetLag()),getMainWindow(),SLOT(resetLag()) );
  connect(this,SIGNAL(addDccPanel()),getMainWindow(),SLOT(addDccPanel()) );
  connect(this,SIGNAL(addKonsolePanel()),getMainWindow(),SLOT(addKonsolePanel()) );

  connect(&serverSocket,SIGNAL (connectionSuccess())  ,this,SLOT (ircServerConnectionSuccess()) );
  connect(&serverSocket,SIGNAL (connectionFailed(int)),this,SLOT (broken(int)) );
  connect(&serverSocket,SIGNAL (readyRead()),this,SLOT (incoming()) );
  connect(&serverSocket,SIGNAL (readyWrite()),this,SLOT (send()) );
  connect(&serverSocket,SIGNAL (closed(int)),this,SLOT (broken(int)) );

  connect(getMainWindow(),SIGNAL(prefsChanged()),KonversationApplication::kApplication(),SLOT(saveOptions()));
  connect(getMainWindow(),SIGNAL(openPrefsDialog()),KonversationApplication::kApplication(),SLOT(openPrefsDialog()));

  connect(this,SIGNAL (serverOnline(bool)),statusView,SLOT (serverOnline(bool)) );

  connect(outputFilter, SIGNAL(launchScript(const QString&, const QString&)),
    m_scriptLauncher, SLOT(launchScript(const QString&, const QString&)));

  connect(m_scriptLauncher, SIGNAL(scriptNotFound(const QString&)),
                      this, SLOT(scriptNotFound(const QString&)));
  connect(m_scriptLauncher, SIGNAL(scriptExecutionError(const QString&)),
                      this, SLOT(scriptExecutionError(const QString&)));

  connect( Konversation::Addressbook::self()->getAddressBook(), SIGNAL( addressBookChanged( AddressBook * ) ), this, SLOT( slotLoadAddressees() ) );
  connect( Konversation::Addressbook::self(), SIGNAL(addresseesChanged()), this, SLOT(slotLoadAddressees()));
  

}

QString Server::getServerName()  const { return serverName; }
int Server::getPort() const { return serverPort; }

QString Server::getServerGroup() const { return serverGroup; }

int Server::getLag()  const { return currentLag; }

bool Server::getAutoJoin()  const { return autoJoin; }
void Server::setAutoJoin(bool on) { autoJoin=on; }

QString Server::getAutoJoinChannel() const { return autoJoinChannel; }
void Server::setAutoJoinChannel(const QString &channel) { autoJoinChannel=channel; }

QString Server::getAutoJoinChannelKey() const { return autoJoinChannelKey; }
void Server::setAutoJoinChannelKey(const QString &key) { autoJoinChannelKey=key; }

bool Server::isConnected()  const { return serverSocket.socketStatus()==KExtendedSocket::connected; }
bool Server::isConnecting() const { return connecting; }

void Server::preShellCommandExited(KProcess* proc)
{
  if (proc->normalExit())
    statusView->appendServerMessage("Info","Process executed successfully!");
  else
    statusView->appendServerMessage("Warning","There was a problem while executing the command!");

  connectToIRCServer();
  connectSignals();
}

void Server::connectToIRCServer()
{
  outputBuffer.clear();
  deliberateQuit=false;
  serverSocket.blockSignals(false);
  connecting=true;

  // prevent sending queue until server has sent something or the timeout is through
  lockSending();

  // Are we (still) connected (yet)?
  if(isConnected())
  {
    // just join our autojoin-channel if desired
    if (getAutoJoin() && !rejoinChannels) queue(getAutoJoinCommand());
    // TODO: move autojoin here and use signals / slots
  }
  else
  {
    // clean up everything
    serverSocket.reset();
    // set up the connection details
    serverSocket.setAddress(serverName,serverPort);

    // reset server/network properites to RFC1459 compatible modes as default.
    setPrefixes("ov","@+");

    // (re)connect. Autojoin will be done by the input filter
    statusView->appendServerMessage(i18n("Info"),i18n("Looking for server %1:%2...").arg(serverSocket.host()).arg(serverSocket.port()));
    // QDns is broken, so don't use async lookup, use own threaded class instead
    resolver.setSocket(&serverSocket);
    resolver.start();
  }
}

// set user mode prefixes according to non-standard 005-Reply (see inputfilter.cpp)
void Server::setPrefixes(const QString &modes, const QString& prefixes)
{
  // NOTE: serverModes is QString::null, if server did not supply the
  // modes which relates to the network's nick-prefixes
  serverNickPrefixModes=modes;
  serverNickPrefixes=prefixes;
}

// return a nickname without possible mode character at the beginning
bool Server::mangleNicknameWithModes(QString& nickname,bool& isAdmin,bool& isOwner,
                                     bool& isOp,bool& isHalfop,bool& hasVoice,char* realMode )
{
  isAdmin=false;
  isOwner=false;
  isOp=false;
  isHalfop=false;
  hasVoice=false;

  if(realMode)
  {
    *realMode=' ';
  }
  // try to find a prefix
  int modeIndex=serverNickPrefixes.find(nickname[0]);
  if(modeIndex==-1)
  {
    // nothing to do, if it was not found.
    // remember that we've set up RFC1459 compatible serverNickPrefixes
    return false;
  }
  if(realMode)
  {
    *realMode=nickname[0].latin1();
  }
  // cut off the prefix
  nickname=nickname.mid(1);
  // determine, whether status is like op or like voice
  while(static_cast<unsigned int>(modeIndex)<serverNickPrefixes.length())
  {
    switch(serverNickPrefixes[modeIndex].latin1())
    {
      case '*':  // admin (EUIRC)
        {
          isAdmin=true;
          return true;
        }
      case '!':  // channel owner (RFC2811)
        {
          isOwner=true;
          return true;
        }
      case '@':  // channel operator (RFC1459)
        {
          isOp=true;
          return true;
        }
      case '%':  // halfop
        {
          isHalfop=true;
          return true;
        }
      case '+':  // voiced (RFC1459)
        {
          hasVoice=true;
          return true;
        }
      default:
        {
          modeIndex++;
          break;
        }
    }
  } // endwhile

  // a mode was used, which has lower priority than voice.
  // (Not seen an ircd which supports this, yet)
  return true;
}

bool Server::eventFilter(QObject* parent,QEvent* event)
{
  if(event->type()==QEvent::User)
  {
    lookupFinished();
    return true;
  }
  return QObject::eventFilter(parent,event);
}

void Server::lookupFinished()
{
  // error during lookup
  if(serverSocket.status())
  {
    // inform user about the error
    statusView->appendServerMessage(i18n("Error"),i18n("Server %1 not found.  %2").arg(serverName).arg(serverSocket.strError(serverSocket.status(), serverSocket.systemError())));

    serverSocket.resetStatus();
    // prevent retrying to connect
//    autoReconnect=0;
    // broken connection
    broken(0);
  }
  else
  {
    statusView->appendServerMessage(i18n("Info"),i18n("Server found, connecting..."));
    serverSocket.startAsyncConnect();
  }
}

void Server::ircServerConnectionSuccess()
{
  reconnectCounter=0;

  connect(this,SIGNAL (nicknameChanged(const QString&)),statusView,SLOT (setNickname(const QString&)) );
  statusView->appendServerMessage(i18n("Info"),i18n("Connected; logging in..."));

  QString connectString="USER " +
                        identity->getIdent() +
                        " 8 * :" +  // 8 = +i; 4 = +w
                        identity->getRealName();

  if(!serverKey.isEmpty()) queueAt(0,"PASS "+serverKey);

  queueAt(1,"NICK "+getNickname());
  queueAt(2,connectString);

  QStringList::iterator iter;
  for(iter = connectCommands.begin(); iter != connectCommands.end(); ++iter)
  {
    QString output(*iter);
    /*if(output.startsWith("/"))
    {
      output.remove(0, 1);
    }*/
    Konversation::OutputFilterResult result = outputFilter->parse(getNickname(),output,QString::null);
    queue(result.toServer);
  }

  emit nicknameChanged(getNickname());

  serverSocket.enableRead(true);

  // wait at most 2 seconds for server to send something before sending the queue ourselves
  unlockTimer.start(2000);
}

void Server::broken(int state)
{
  serverSocket.enableRead(false);
  serverSocket.enableWrite(false);
  serverSocket.blockSignals(true);

  alreadyConnected=false;
  connecting=false;
  outputBuffer.clear();

  kdDebug() << "Connection broken (Socket fd " << serverSocket.fd() << ") " << state << "!" << endl;

  // clear nicks online
  emit nicksNowOnline(this,QStringList(),true);

  // TODO: Close all queries and channels!
  //       Or at least make sure that all gets reconnected properly
  if(autoReconnect && !getDeliberateQuit())
  {
    statusView->appendServerMessage(i18n("Error"),i18n("Connection to Server %1 lost. Trying to reconnect.").arg(serverName));
    getMainWindow()->appendToFrontmost(i18n("Error"),i18n("Connection to Server %1 lost. Trying to reconnect.").arg(serverName),statusView);
    // TODO: Make retry counter configurable
    if(++reconnectCounter==10)
    {
      statusView->appendServerMessage(i18n("Error"),i18n("Connection to Server %1 failed.").arg(serverName));
      getMainWindow()->appendToFrontmost(i18n("Error"),i18n("Connection to Server %1 failed.").arg(serverName),statusView);
      reconnectCounter=0;
      rejoinChannels = false;
    }
    else
    {
      // TODO: Make timeout configurable
      QTimer::singleShot(5000,this,SLOT(connectToIRCServer()));
      rejoinChannels = true;
    }
  }
  else if(getDeliberateQuit()) // If we quit the connection with the server
  {
    getMainWindow()->serverQuit(this);
  }
  else
  {
    statusView->appendServerMessage(i18n("Error"),i18n("Connection to Server %1 closed.").arg(serverName));
    getMainWindow()->appendToFrontmost(i18n("Error"),i18n("Connection to Server %1 closed.").arg(serverName),statusView);
  }

  emit serverOnline(false);
}

// Will be called from InputFilter as soon as the Welcome message was received
void Server::connectionEstablished()
{
  emit serverOnline(true);

  if(!alreadyConnected)
  {
    alreadyConnected=true;
     // get first notify very early
    startNotifyTimer(1000);
    // register with services
    if(!botPassword.isEmpty() && !bot.isEmpty())
      queue("PRIVMSG "+bot+" :identify "+botPassword);

    if(rejoinChannels) {
      rejoinChannels = false;
      autoRejoinChannels();
    }
  }
  else
    kdDebug() << "alreadyConnected==true! How did that happen?" << endl;
}

void Server::quitServer()
{
  QString command(KonversationApplication::preferences.getCommandChar()+"QUIT");
  Konversation::OutputFilterResult result = outputFilter->parse(getNickname(),command,QString::null);
  queue(result.toServer);
}

void Server::notifyAction(const QString& nick)
{
  // parse wildcards (toParse,nickname,channelName,nickList,parameter)
  QString out=parseWildcards(KonversationApplication::preferences.getNotifyDoubleClickAction(),
                             getNickname(),
                             QString::null,
                             QString::null,
                             nick,
                             QString::null);
  // Send all strings, one after another
  QStringList outList=QStringList::split('\n',out);
  for(unsigned int index=0;index<outList.count();index++)
  {
    Konversation::OutputFilterResult result = outputFilter->parse(getNickname(),outList[index],QString::null);
    queue(result.toServer);
  } // endfor
}

void Server::notifyResponse(const QString& nicksOnline)
{
  // We received a 303 or "PONG :LAG" notify message, so calculate server lag
  int lag=notifySent.elapsed();
  currentLag=lag;
  // inform main window
  emit serverLag(this,lag);
  // Stop check timer
  notifyCheckTimer.stop();
  // Switch off lag measuring mode
  inputFilter.setLagMeasuring(false);

  // only update Nicks Online list if we got a 303 response, not a PONG
  if(nicksOnline!="###")
  {

#ifdef USE_NICKINFO
    bool nicksOnlineChanged = false;
    // Create a case correct nick list from the notification reply
    QStringList nickList=QStringList::split(' ',nicksOnline);
    // Create a lower case nick list from the notification reply
    QStringList nickLowerList=QStringList::split(' ',nicksOnline.lower());
    // Get watch list from preferences.
    QString watchlist=getNotifyString();
    // Create a case correct nick list from the watch list.
    QStringList watchList=QStringList::split(' ',watchlist); 
    // Create a lower case nick list from the watch list.
    QStringList watchLowerList=QStringList::split(' ',watchlist.lower());
    // Any new watched nicks online?
    unsigned int index;
    for(index=0;index<nickLowerList.count();index++)
    {
      if (!nicknamesOnline.contains(nickLowerList[index]))
      {
        addNickToOnlineList(nickList[index]);
        nicksOnlineChanged = true;
        getMainWindow()->appendToFrontmost(i18n("Notify"),i18n("%1 is online (%2).").arg(nickList[index]).arg(getServerName()),statusView);

#ifdef USE_KNOTIFY
        KNotifyClient::event(mainWindow->winId(), "notify",
          i18n("%1 is online (%2).").arg(nickList[index]).arg(getServerName()));
#endif
      }
    }
    // Any watched nicks now offline?
    NickInfoMap::Iterator it = nicknamesOnline.begin();
    while (it != nicknamesOnline.end())
    {
      QString lcNickName = it.key();
      NickInfoPtr nickInfo = *it;
      ++it;
      if (nickLowerList.find(lcNickName) == nickLowerList.end())
      {
        QString nickName = nickInfo->getNickname();
        nicksOnlineChanged = true;
        addNickToOfflineList(nickName, watchLowerList);
        getMainWindow()->appendToFrontmost(i18n("Notify"),i18n("%1 went offline. (%2)").arg(nickName).arg(getServerName()),statusView);

#ifdef USE_KNOTIFY
        KNotifyClient::event(mainWindow->winId(), "notify",
        i18n("%1 went offline. (%2)").arg(nickName).arg(getServerName()));
#endif
      }
    }
    // Any nicks on the watch list not yet on either list?  If so, add them to offline list.
    // TODO: It would be better to do this on startup and whenever user changes preferences.
    for(index=0;index<watchLowerList.count();index++)
    {
      if (nicknamesOnline.find(watchLowerList[index]) == nicknamesOnline.end()) addNickToOfflineList(watchList[index], watchLowerList);
    }

#else

    // First copy the old notify cache to a new cache, but all in lowercase
    QStringList notifyLowerCache=QStringList::split(' ',notifyCache.join(" ").lower());
    // Create a case correct nick list from the notification reply
    QStringList nickList=QStringList::split(' ',nicksOnline);
    // Create a lower case nick list from the notification reply
    QStringList nickLowerList=QStringList::split(' ',nicksOnline.lower());
    bool nicksOnlineChanged = false;

    // Did some new nicks appear in our notify?
    for(unsigned int index=0;index<nickLowerList.count();index++)
    {
      if(notifyLowerCache.find(nickLowerList[index])==notifyLowerCache.end())
      {
        nicksOnlineChanged = true;
        getMainWindow()->appendToFrontmost(i18n("Notify"),i18n("%1 is online (%2).").arg(nickList[index]).arg(getServerName()),statusView);

#ifdef USE_KNOTIFY
        KNotifyClient::event(mainWindow->winId(), "notify",
          i18n("%1 is online (%2).").arg(nickList[index]).arg(getServerName()));
#endif
      }
    }

    // Did some nicks leave our notify?
    for(unsigned int index=0;index<notifyLowerCache.count();index++)
    {
      if(nickLowerList.find(notifyLowerCache[index])==nickLowerList.end())
      {
        nicksOnlineChanged = true;
        getMainWindow()->appendToFrontmost(i18n("Notify"),i18n("%1 went offline. (%2)").arg(notifyCache[index]).arg(getServerName()),statusView);

#ifdef USE_KNOTIFY
        KNotifyClient::event(mainWindow->winId(), "notify",
          i18n("%1 went offline. (%2)").arg(notifyCache[index]).arg(getServerName()));
#endif
      }
    }

    // Finally copy the new ISON list with correct case to our notify cache
    notifyCache=nickList;
#endif

    emit nicksNowOnline(this,nickList,nicksOnlineChanged);
  }
  // Next round
  startNotifyTimer();
}

void Server::startNotifyTimer(int msec)
{
  // make sure the timer gets started properly in case we have reconnected
  notifyTimer.stop();
  if(msec==0) msec=KonversationApplication::preferences.getNotifyDelay()*1000; // msec!
  // start the timer in one shot mode
  notifyTimer.start(msec,true);
  notifyCheckTimer.stop();
  // reset check time
  checkTime=0;
}

void Server::startNotifyCheckTimer()
{
  // start the timer in interval mode
  notifyCheckTimer.start(500);
}

void Server::notifyTimeout()
{
  bool sent=false;

  // Notify delay time is over, send ISON request if desired
  if(KonversationApplication::preferences.getUseNotify())
  {
    // But only if there actually are nicks in the notify list
    QString list=getNotifyString();
    
    if(!list.isEmpty())
    {
      queue("ISON "+list);
      // remember that we already sent out ISON
      sent=true;
    }
  }

  // if no ISON was sent, fall back to PING for lag measuring
  if(!sent) queue("PING LAG :"+getIrcName());
  // start check timer waiting for 303 or PONG response
  startNotifyCheckTimer();
  // start lag measuring mode
  inputFilter.setLagMeasuring(true);
}

// waiting too long for 303 response
void Server::notifyCheckTimeout()
{
  checkTime+=500;
  if(isConnected())
  {
    currentLag=checkTime;
    emit tooLongLag(this,checkTime);
    if(KonversationApplication::preferences.getAutoReconnect() &&
      (checkTime/1000)==KonversationApplication::preferences.getMaximumLagTime())
    {
      serverSocket.close();
    }
  }
}

QString Server::getAutoJoinCommand() const
{
  // Multichannel joins
  QStringList channels=QStringList::split(' ',autoJoinChannel);
  QStringList keys=QStringList::split(' ',autoJoinChannelKey);

  QString autoString("JOIN "+channels.join(",")+" "+keys.join(","));

  return autoString;
}

QString Server::getNextNickname()
{
  QString newNick=getNickname();
  if(tryNickNumber!=4)
  {
    tryNickNumber++;
    if (tryNickNumber==4) newNick=getNickname()+"_";
    else newNick=identity->getNickname(tryNickNumber);
  }
  return newNick;
}

void Server::processIncomingData()
{
  int pos;

  pos=inputBuffer.find('\n');
  if(pos!=-1)
  {
    QString line=inputBuffer.left(pos);

    inputBuffer=inputBuffer.mid(pos+1);

    if(rawLog) rawLog->appendRaw(line);
    inputFilter.parseLine(line, mainWindow);
  }
}

void Server::unlockSending()
{
  sendUnlocked=true;
}

void Server::lockSending()
{
  sendUnlocked=false;
}

void Server::incoming()
{
  char buffer[BUFFER_LEN];
  int len = 0;

  len = read(serverSocket.fd(),buffer,BUFFER_LEN-1);

  buffer[len] = 0;

  // convert IRC ascii data to selected encoding
  bool isUtf8 = KStringHandler::isUtf8(buffer);

  if(isUtf8 || ((identity->getCodec() == "utf8") && !isUtf8))
  {
    inputBuffer += KStringHandler::from8Bit(buffer);
  }
  else
  {
    QTextCodec* codec=QTextCodec::codecForName(identity->getCodec().ascii());
    inputBuffer += codec->toUnicode(buffer);
  }

  if(len == 0) broken(0);

  // refresh lock timer if it was still locked
  if(!sendUnlocked) lockSending();
}

void Server::queue(const QString& buffer)
{
  // Only queue lines if we are connected
  if(buffer.length())
  {
    outputBuffer.append(buffer);

    timerInterval*=2;
  }
}

void Server::queueAt(uint pos,const QString& buffer)
{
  // Only queue lines if we are connected
  if(buffer.length() && pos < outputBuffer.count())
  {
    outputBuffer.insert(outputBuffer.at(pos),buffer);

    timerInterval*=2;
  } else {
    queue(buffer);
  }
}

void Server::queueList(const QStringList& buffer)
{
  // Only queue lines if we are connected
  if(buffer.count())
  {
    for(unsigned int i=0;i<buffer.count();i++)
    {
      outputBuffer.append(*buffer.at(i));
      timerInterval*=2;
    } // for
  }
}

void Server::send()
{
  // Check if we are still online
  if(!isConnected()) return;

  if(outputBuffer.count() && sendUnlocked)
  {
    // NOTE: It's important to add the linefeed here, so the encoding process does not trash it
    //       for some servers.
    QString outputLine=outputBuffer[0]+"\n";
    outputBuffer.pop_front();

    // To make lag calculation more precise, we reset the timer here
    if(outputLine.startsWith("ISON") ||
       outputLine.startsWith("PING LAG")) notifySent.start();

    // Don't reconnect if we WANT to quit
    else if(outputLine.startsWith("QUIT")) setDeliberateQuit(true);

    // wrap server socket into a stream
    QTextStream serverStream(&serverSocket);

    // init stream props
    serverStream.setEncoding(QTextStream::Locale);
    QString codecName=identity->getCodec();
    // convert encoded data to IRC ascii only when we don't have the same codec locally
    if(QString(QTextCodec::codecForLocale()->name()).lower()!=codecName.lower())
    {
      serverStream.setCodec(QTextCodec::codecForName(codecName.ascii()));
    }

    serverStream << outputLine;
    if(rawLog) rawLog->appendRaw(outputLine);

    // detach server stream
    serverStream.unsetDevice();
  }

  // Flood-Protection
  if(timerInterval>1)
  {
    int time;
    timerInterval/=2;

    if(timerInterval>40) time=4000;
    else time=timerInterval*10+100;

    outgoingTimer.changeInterval(time);
  }
}

void Server::dcopSay(const QString& target,const QString& command)
{
  if(isAChannel(target))
  {
    Channel* channel=getChannelByName(target);
    if(channel) channel->sendChannelText(command);
  }
  else
  {
    Query* query=getQueryByName(target);
    if(query==0)
    {
      addQuery(target,QString::null);
      query=getQueryByName(target);
    }
    if(query && !command.isEmpty()) query->sendQueryText(command);
  }
}

void Server::dcopInfo(const QString& string)
{
  appendStatusMessage(i18n("DCOP"),string);
}

void Server::ctcpReply(const QString &receiver,const QString &text)
{
  queue("NOTICE "+receiver+" :"+'\x01'+text+'\x01');
}

bool Server::getDeliberateQuit() const
{
  return deliberateQuit;
}

void Server::setDeliberateQuit(bool on)
{
  deliberateQuit=on;
}

QString Server::getNumericalIp()
{
  QHostAddress ip;
  ip.setAddress(getIp());

  return QString::number(ip.ip4Addr());
}

// Given a nickname, returns NickInfo object.   0 if not found.
NickInfoPtr Server::getNickInfo(const QString& nickname)
{
  QString lcNickname(nickname.lower());
  if (allNicks.contains(lcNickname))
  {
    NickInfoPtr nickinfo = allNicks[lcNickname];
    Q_ASSERT(nickinfo);
    return nickinfo;
  }
  else
    return 0;
}

// Given a nickname, returns an existing NickInfo object, or creates a new NickInfo object.
// Returns pointer to the found or created NickInfo object.
NickInfoPtr Server::obtainNickInfo(const QString& nickname)
{
  NickInfoPtr nickInfoPtr = getNickInfo(nickname);
  if (!nickInfoPtr)
  {
    nickInfoPtr = new NickInfo(nickname, this);
    allNicks.insert(QString(nickname.lower()), nickInfoPtr);
  }
  return nickInfoPtr;
}

// Returns the list of members for a channel in the joinedChannels list.
// 0 if channel is not in the joinedChannels list.
// Using code must not alter the list.
const ChannelNickMap *Server::getJoinedChannelMembers(const QString& channelName) const
{
  QString lcChannelName = channelName.lower();
  if (joinedChannels.contains(lcChannelName))
    return joinedChannels[lcChannelName];
  else
    return 0;
}

// Returns the list of members for a channel in the unjoinedChannels list.
// 0 if channel is not in the unjoinedChannels list.
// Using code must not alter the list.
const ChannelNickMap *Server::getUnjoinedChannelMembers(const QString& channelName) const
{
  QString lcChannelName = channelName.lower();
  if (unjoinedChannels.contains(lcChannelName))
    return unjoinedChannels[lcChannelName];
  else
    return 0;
}

// Searches the Joined and Unjoined lists for the given channel and returns the member list.
// 0 if channel is not in either list.
// Using code must not alter the list.
const ChannelNickMap *Server::getChannelMembers(const QString& channelName) const
{
  const ChannelNickMap *members = getJoinedChannelMembers(channelName);
  if (members)
    return members;
  else
    return getUnjoinedChannelMembers(channelName);
}

// Returns pointer to the ChannelNick (mode and pointer to NickInfo) for a given channel and nickname.
// 0 if not found.
ChannelNickPtr Server::getChannelNick(const QString& channelName, const QString& nickname)
{
  QString lcChannelName = channelName.lower();
  QString lcNickname = nickname.lower();
  const ChannelNickMap *channelNickMap = getChannelMembers(lcChannelName);
  if (channelNickMap)
  {
    if (channelNickMap->contains(lcNickname))
      return (*channelNickMap)[lcNickname];
    else
      return 0;
  }
  else
  {
    return 0;
  }
}

// Updates a nickname in a channel.  If not on the joined or unjoined lists, and nick
// is in the watch list, adds the channel and nick to the unjoinedChannels list.
// If mode != 99, sets the mode for the nick in the channel.
// Returns the NickInfo object if nick is on any lists, otherwise 0.
ChannelNickPtr Server::setChannelNick(const QString& channelName, const QString& nickname, unsigned int mode)
{
  QString lcNickname = nickname.lower();
  // If already on a list, update mode.
  ChannelNickPtr channelNick = getChannelNick(channelName, lcNickname);
  if (!channelNick)
  {
    // Get watch list from preferences.
    QString watchlist=getNotifyString();
    // Create a lower case nick list from the watch list.
    QStringList watchLowerList=QStringList::split(' ',watchlist.lower());
    // If on the watch list, add channel and nick to unjoinedChannels list.
    if (watchLowerList.find(lcNickname) != watchLowerList.end())
    {
      channelNick = addNickToUnjoinedChannelsList(channelName, nickname);
      channelNick->setMode(mode);
    }
    else return 0;
  }

    if (mode != 99) channelNick->setMode(mode);
    return channelNick;
}

// Returns a list of all the channels (joined or unjoined) that a nick is in.
QStringList Server::getNickChannels(const QString& nickname)
{
  QString lcNickname = nickname.lower();
  QStringList channellist;
  ChannelMembershipMap::Iterator channel;
  for( channel = joinedChannels.begin(); channel != joinedChannels.end(); ++channel )
  {
    if (channel.data()->contains(lcNickname)) channellist.append(channel.key());
  }
  for( channel = unjoinedChannels.begin(); channel != unjoinedChannels.end(); ++channel )
  {
    if (channel.data()->contains(lcNickname)) channellist.append(channel.key());
  }
  return channellist;
}

#if USE_NICKINFO

bool Server::isNickOnline(const QString &nickname) {
  return nicknamesOnline.contains(nickname.lower());
}
#else
bool Server::isNickOnline(const QString &nickname) {
  Channel* lookChannel=channelList.first();

  while(lookChannel)
  {
    if(lookChannel->getNickByName(nickname)) return true;
    lookChannel = channelList.next();
  }
//Check queries as well

  Query* lookQuery=queryList.first();
  while(lookQuery)
  {
    lookQuery = queryList.next();
  }
  return false;
}
#endif
// Returns a list of the nicks on the watch list that are online.
const NickInfoMap* Server::getNicksOnline() { return &nicknamesOnline; }

// Returns a list of the nicks on the watch list that are offline.
const NickInfoMap* Server::getNicksOffline() { return &nicknamesOffline; }

QString Server::getIp()
{
  // Get our own IP address.
  KSocketAddress* ipAddr=KExtendedSocket::localAddress(serverSocket.fd());

  if(ipAddr)
  {
    KInetSocketAddress inetSocket((const sockaddr_in*)ipAddr->address(),ipAddr->size());

    struct in_addr in_addr=inetSocket.hostV4();
    QString ip(KInetSocketAddress::addrToString(inetSocket.family(),&in_addr));
    // remove temporary object
    delete ipAddr;
    return ip;
  }
  return QString::null;
}

void Server::addQuery(const QString& nickname,const QString& hostmask, bool weinitiated )
{
  // Only create new query object if there isn't already one with the same name
  Query* query=getQueryByName(nickname);
  if(!query)
  {
    query=getMainWindow()->addQuery(this,nickname, weinitiated);
    query->setIdentity(getIdentity());

    connect(query,SIGNAL (sendFile(const QString&)),this,SLOT (requestDccSend(const QString &)) );
    connect(this,SIGNAL (serverOnline(bool)),query,SLOT (serverOnline(bool)) );

    // Append query to internal list
    queryList.append(query);

#ifdef USE_NICKINFO
    // Update NickInfo.
    QString lcNickname = nickname.lower();
    NickInfoPtr nickInfo = obtainNickInfo(nickname);
    if ((nickInfo->getHostmask() != hostmask) && !hostmask.isEmpty())
    {
      nickInfo->setHostmask(hostmask);
    }
    queryNicks.insert(lcNickname, nickInfo);
#endif
  }

  // try to get hostmask if there's none yet
  if(hostmask.isEmpty()) requestUserhost(nickname);

  // Always set hostmask
  if(query) query->setHostmask(hostmask);
}

void Server::closeQuery(const QString &name)
{
  Query* query=getQueryByName(name);
  removeQuery(query);

#ifdef USE_NICKINFO
  // Update NickInfo.
  queryNicks.remove(QString(name.lower()));
#endif
}

void Server::closeChannel(const QString& name)
{
  kdDebug() << "Server::closeChannel(" << name << ")" << endl;
  Channel* channelToClose=getChannelByName(name);
  if(channelToClose)
  {
    Konversation::OutputFilterResult result = outputFilter->parse(getNickname(),
      KonversationApplication::preferences.getCommandChar() + "PART", name);
    queue(result.toServer);
  }
}

void Server::requestChannelList()
{
  inputFilter.setAutomaticRequest(true);
  queue("LIST");
}

void Server::requestWhois(const QString& nickname)
{
  inputFilter.setAutomaticRequest(true);
  queue("WHOIS "+nickname);
}

void Server::requestUserhost(const QString& nicks)
{
  inputFilter.setAutomaticRequest(true);
  queue("USERHOST "+nicks);
}

void Server::requestBan(const QStringList& users,const QString& channel,const QString& a_option)
{
  QString hostmask;
  QString option=a_option.lower();

  Channel* targetChannel=getChannelByName(channel);

  for(unsigned int index=0;index<users.count();index++)
  {
    // first, set the ban mask to the specified nick
    QString mask=users[index];
    // did we specify an option?
    if(!option.isEmpty())
    {
      // try to find specified nick on the channel
      Nick* targetNick=targetChannel->getNickByName(mask);
      // if we found the nick try to find their hostmask
      if(targetNick)
      {
        QString hostmask=targetNick->getHostmask();
        // if we found the hostmask, add it to the ban mask
        if(!hostmask.isEmpty())
        {
          mask=targetNick->getNickname()+"!"+hostmask;

          // adapt ban mask to the option given
          if(option=="host")
            mask="*!*@*."+hostmask.section('.',1);
          else if(option=="domain")
            mask="*!*@"+hostmask.section('@',1);
          else if(option=="userhost")
            mask="*!"+hostmask.section('@',0,0)+"@*."+hostmask.section('.',1);
          else if(option=="userdomain")
            mask="*!"+hostmask.section('@',0,0)+"@"+hostmask.section('@',1);
        }
      }
    }

    Konversation::OutputFilterResult result = outputFilter->execBan(mask,channel);
    queue(result.toServer);
  }
}

void Server::requestUnban(const QString& mask,const QString& channel)
{
  Konversation::OutputFilterResult result = outputFilter->execUnban(mask,channel);
  queue(result.toServer);
}

void Server::requestDccSend()
{
  requestDccSend(QString::null);
}

void Server::requestDccSend(const QString &a_recipient)
{
  QString recipient(a_recipient);
  // if we don't have a recipient yet, let the user select one
  if(recipient.isEmpty())
  {
    QStringList nickList;
    Channel* lookChannel=channelList.first();

    // fill nickList with all nicks we know about
    while(lookChannel)
    {
      QPtrList<Nick> nicks=lookChannel->getNickList();
      Nick* lookNick=nicks.first();
      while(lookNick)
      {
        if(!nickList.contains(lookNick->getNickname())) nickList.append(lookNick->getNickname());
        lookNick=nicks.next();
      }
      lookChannel=channelList.next();
    }

    // add Queries as well, but don't insert duplicates
    Query* lookQuery=queryList.first();
    while(lookQuery)
    {
      if(!nickList.contains(lookQuery->getName())) nickList.append(lookQuery->getName());
      lookQuery=queryList.next();
    }

    recipient=DccRecipientDialog::getNickname(getMainWindow(),nickList);
  }
  // do we have a recipient *now*?
  if(!recipient.isEmpty())
  {
    QString fileName=KFileDialog::getOpenFileName(
                                                   lastDccDir,
                                                   QString::null,
                                                   getMainWindow(),
                                                   i18n("Select File to Send to %1").arg(recipient)
                                                 );
    if(!fileName.isEmpty())
    {
      QFileInfo fileInfo(fileName);

      lastDccDir=fileInfo.dirPath();

      if(fileInfo.isDir())
        appendStatusMessage(i18n("DCC"),i18n("Error: \"%1\" is not a regular file.").arg(fileName));
      else
        addDccSend(recipient,fileName);
    }
  }
}

void Server::addDccSend(const QString &recipient,const QString &fileName)
{
  emit addDccPanel();

  QString ownIp;
  if(KonversationApplication::preferences.getDccGetIpFromServer())
    ownIp=ownIpByServer;
  if(ownIp.isEmpty())
    ownIp=getIp();

  // We already checked that the file exists in output filter / requestDccSend() resp.
  DccTransferSend* newDcc=new DccTransferSend(getMainWindow()->getDccPanel()->getListView(),
                                              recipient,
                                              fileName,  // path to the sending file
                                              ownIp);    // ip

  connect(newDcc,SIGNAL (sendReady(const QString&,const QString&,const QString&,const QString&,unsigned long)),
    this,SLOT (dccSendRequest(const QString&,const QString&,const QString&,const QString&,unsigned long)) );
  connect(newDcc,SIGNAL (done(const QString&)),this,SLOT (dccSendDone(const QString&)) );
  newDcc->start();
}

void Server::addDccGet(const QString &sourceNick, const QStringList &dccArguments)
{
  emit addDccPanel();

  QHostAddress ip;

  ip.setAddress(dccArguments[1].toULong());

  appendStatusMessage(i18n("DCC"),
                      i18n("%1 offers the file \"%2\" (%3 bytes) for download (%4:%5).")
                              .arg(sourceNick)               // name
                              .arg(dccArguments[0])          // file
                              .arg((dccArguments[3].isEmpty()) ? i18n("unknown number of") : dccArguments[3] ) // size
                              .arg(ip.toString())            // ip
                              .arg(dccArguments[2])          // port
                             );

  DccTransferRecv* newDcc=new DccTransferRecv(getMainWindow()->getDccPanel()->getListView(),
                      sourceNick,
                      KonversationApplication::preferences.getDccPath(),
                      dccArguments[0],     // name
                      dccArguments[3].isEmpty() ? 0 : dccArguments[3].toULong(),  // size
                      ip.toString(),       // ip
                      dccArguments[2]);    // port

  connect(newDcc,SIGNAL (resumeRequest(const QString&,const QString&,const QString&,int)),this,
         SLOT (dccResumeGetRequest(const QString&,const QString&,const QString&,int)) );
  connect(newDcc,SIGNAL (done(const QString&)),
              this,SLOT (dccGetDone(const QString&)) );
  connect(newDcc,SIGNAL (statusChanged(const DccTransfer* )), this,
         SLOT(dccStatusChanged(const DccTransfer*)) );

  if(KonversationApplication::preferences.getDccAutoGet()) newDcc->start();
}

void Server::requestKonsolePanel()
{
  emit addKonsolePanel();
}

void Server::requestDccPanel()
{
  emit addDccPanel();
}

void Server::requestCloseDccPanel()
{
  emit closeDccPanel();
}

void Server::requestDccChat(const QString& nickname)
{
  getMainWindow()->addDccChat(getNickname(),nickname,getNumericalIp(),QStringList(),true);
}

void Server::dccSendRequest(const QString &partner, const QString &fileName, const QString &address, const QString &port, unsigned long size)
{
  Konversation::OutputFilterResult result = outputFilter->sendRequest(partner,fileName,address,port,size);
  queue(result.toServer);
  appendStatusMessage(result.typeString, result.output);
}

void Server::dccResumeGetRequest(const QString &sender, const QString &fileName, const QString &port, int startAt)
{
  Konversation::OutputFilterResult result = outputFilter->resumeRequest(sender,fileName,port,startAt);
  queue(result.toServer);
  appendStatusMessage(result.typeString, result.output);
}

void Server::resumeDccGetTransfer(const QString &sourceNick, const QStringList &dccArguments)
{
  // Check if there actually is a transfer going on on that port
  DccTransferRecv* dccTransfer=static_cast<DccTransferRecv*>(getMainWindow()->getDccPanel()->getTransferByPort(dccArguments[1],DccTransfer::Receive,true));
  if(!dccTransfer)
    // Check if there actually is a transfer going on with that name, could be behind a NAT
    // so the port number may get changed
    // mIRC substitutes this with "file.ext", so we have a problem here with mIRCs behind a NAT
    dccTransfer=static_cast<DccTransferRecv*>(getMainWindow()->getDccPanel()->getTransferByName(dccArguments[0],DccTransfer::Receive,true));

  if(dccTransfer)
  {
    // overcome mIRCs brain-dead "file.ext" substitution
    QString fileName=dccTransfer->getFileName();
    appendStatusMessage(i18n("DCC"),i18n("Resuming file \"%1\", offered by %2 from position %3.").arg(fileName).arg(sourceNick).arg(dccArguments[2]));
    dccTransfer->startResume(dccArguments[2].toULong());
  }
  else
  {
    appendStatusMessage(i18n("Error"),i18n("No DCC download running on port %1.").arg(dccArguments[1]));
  }
}

void Server::resumeDccSendTransfer(const QString &recipient, const QStringList &dccArguments)
{
  // Check if there actually is a transfer going on on that port
  DccTransferSend* dccTransfer=static_cast<DccTransferSend*>(getMainWindow()->getDccPanel()->getTransferByPort(dccArguments[1],DccTransfer::Send));
  if(!dccTransfer)
    // Check if there actually is a transfer going on with that name, could be behind a NAT
    // so the port number may get changed
    // mIRC substitutes this with "file.ext", so we have a problem here with mIRCs behind a NAT
    dccTransfer=static_cast<DccTransferSend*>(getMainWindow()->getDccPanel()->getTransferByName(dccArguments[0],DccTransfer::Send));

  if(dccTransfer && dccTransfer->getStatus() == DccTransfer::WaitingRemote)
  {
    QString fileName=dccTransfer->getFileName();
    appendStatusMessage(i18n("DCC"),i18n("Resuming file \"%1\", offered by %2 from position %3.").arg(fileName).arg(recipient).arg(dccArguments[2]));
    dccTransfer->setResume(dccArguments[2].toULong());
    Konversation::OutputFilterResult result = outputFilter->acceptRequest(recipient,
      fileName, dccArguments[1], dccArguments[2].toUInt());
    queue(result.toServer);
    appendStatusMessage(result.typeString, result.output);
  }
  else
  {
    appendStatusMessage(i18n("Error"),i18n("No DCC upload running on port %1.").arg(dccArguments[1]));
  }
}

void Server::dccGetDone(const QString &fileName)
{
  appendStatusMessage(i18n("DCC"),i18n("DCC download of file \"%1\" finished.").arg(fileName));
}

void Server::dccSendDone(const QString &fileName)
{
  appendStatusMessage(i18n("DCC"),i18n("DCC upload of file \"%1\" finished.").arg(fileName));
}

void Server::dccStatusChanged(const DccTransfer *item)
{
  getMainWindow()->getDccPanel()->dccStatusChanged(item);
}

QString Server::getNextQueryName()
{
  // Check if completion position is out of range
  if(completeQueryPosition>=queryList.count()) completeQueryPosition=0;
  // return the next query in the list (for /msg completion)
  if(queryList.count()) return queryList.at(completeQueryPosition++)->getName();

  return QString::null;
}

void Server::removeQuery(Query* query)
{
  // Traverse through list to find the query
  Query* lookQuery=queryList.first();
  while(lookQuery)
  {
    // Did we find our query?
    if(lookQuery==query)
    {
      // Remove it from the query list
      queryList.remove(lookQuery);
      // break out of the loop
      lookQuery=0;
    }
    // else select next query
    else lookQuery=queryList.next();
  }
#ifndef USE_MDI
  // Free the query object
  delete query;
#endif
}

void Server::sendJoinCommand(const QString& name)
{
  Konversation::OutputFilterResult result = outputFilter->parse(getNickname(),
    KonversationApplication::preferences.getCommandChar() + "JOIN " + name, QString::null);
  queue(result.toServer);
}

void Server::joinChannel(const QString &name, const QString &hostmask, const QString &/*key*/)
{
  // (re-)join channel, open a new panel if needed
  Channel* channel=getChannelByName(name);
  if(!channel)
  {
    channel=getMainWindow()->addChannel(this,name);
    Q_ASSERT(channel);
    channel->setIdentity(getIdentity());
    channel->setNickname(getNickname());
  //channel->setKey(key);

    channelList.append(channel);

    connect(channel,SIGNAL (sendFile()),this,SLOT (requestDccSend()) );
    connect(this,SIGNAL (serverOnline(bool)),channel,SLOT (serverOnline(bool)) );
  }
#ifdef USE_NICKINFO
  // Move channel from unjoined (if present) to joined list and add our own nickname to the joined list.
  ChannelNickPtr channelNick = addNickToJoinedChannelsList(name, getNickname());
  
  if ((channelNick->getHostmask() != hostmask ) && !hostmask.isEmpty())
  {
    NickInfoPtr nickInfo = channelNick->getNickInfo();
    nickInfo->setHostmask(hostmask);
  }
  channel->joinNickname(channelNick);
#else
  channel->joinNickname(getNickname(),hostmask);
#endif
}

void Server::removeChannel(Channel* channel)
{
#ifdef USE_NICKINFO
  // Update NickInfo.
  removeJoinedChannel(channel->getName());
#endif

  channelList.removeRef(channel);
}

void Server::updateChannelMode(const QString &updater, const QString &channelName, char mode, bool plus, const QString &parameter)
{

  Channel* channel=getChannelByName(channelName);
#ifdef USE_NICKINFO

  ChannelNickPtr updaterNick = getChannelNick(channelName, updater);
  if(!updaterNick) {
	  kdDebug() << "in updateChannelMode, could not find updater nick " << updater << " for channel " << channelName << endl;
	  return;
  }
  if(channel) //Let the channel be verbose to the screen about the change, and update channelNick
	  channel->updateMode(updaterNick, mode, plus, parameter);
  // TODO: What is mode character for owner?
  // Answer from JOHNFLUX - I think that admin is the same as owner.  Channel.h has owner as "a"
  QString userModes="vho?a";    // voice halfop op owner admin
  int modePos = userModes.find(mode);
  if (modePos > 0)
  {
    ChannelNickPtr updateeNick = getChannelNick(channelName, parameter);
    if(!updaterNick) {
	  kdDebug() << "in updateChannelMode, could not find updatee nick " << parameter << " for channel " << channelName << endl;
	  kdDebug() << "This could indicate an obscure race condition that is safely being handled (like the mode of someone changed and they quit almost simulatanously, or it could indicate an internal error.";
	  //TODO Do we need to add this nick?
	  return;
    }

    updateeNick->setMode(mode, plus);

    // Note that channel will be moved to joined list if necessary.
    addNickToJoinedChannelsList(channelName, parameter);
  }
#else
  if(channel) channel->updateMode(updater,mode,plus,parameter);
#endif

}


void Server::updateChannelModeWidgets(const QString &channelName, char mode, const QString &parameter)
{
  Channel* channel=getChannelByName(channelName);
  if(channel) channel->updateModeWidgets(mode,true,parameter);
}

void Server::updateChannelQuickButtons()
{
  Channel* channel=channelList.first();
  while(channel)
  {
    channel->updateQuickButtons(KonversationApplication::preferences.getButtonList());
    channel=channelList.next();
  }
}

// TODO: Maybe use a Signal / Slot mechanism for these things?
void Server::updateFonts()
{
  Channel* channel=channelList.first();
  while(channel)
  {
    channel->updateFonts();
    channel->updateStyleSheet();
    channel=channelList.next();
  }

  Query* query=queryList.first();
  while(query)
  {
    query->updateFonts();
    query=queryList.next();
  }

  statusView->updateFonts();
  getMainWindow()->updateFonts();

  if(rawLog) rawLog->updateFonts();
}

// TODO: Maybe use a Signal / Slot mechanism for these things?
void Server::setShowQuickButtons(bool state)
{
  Channel* channel=channelList.first();
  while(channel)
  {
    channel->showQuickButtons(state);
    channel=channelList.next();
  }
}

// TODO: Maybe use a Signal / Slot mechanism for these things?
void Server::setShowModeButtons(bool state)
{
  Channel* channel=channelList.first();
  while(channel)
  {
    channel->showModeButtons(state);
    channel=channelList.next();
  }
}

void Server::setShowTopic(bool state)
{
  Channel* channel=channelList.first();
  while(channel)
  {
    channel->showTopic(state);
    channel=channelList.next();
  }
}

Channel* Server::getChannelByName(const QString& name)
{
  // Convert wanted channel name to lowercase
  QString wanted=name;
  wanted=wanted.lower();

  // Traverse through list to find the channel named "name"
  Channel* lookChannel=channelList.first();
  while(lookChannel)
  {
    if(lookChannel->getName().lower()==wanted) return lookChannel;
    lookChannel=channelList.next();
  }
  // No channel by that name found? Return 0. Happens on first channel join
  return 0;
}

Query* Server::getQueryByName(const QString& name)
{
  // Convert wanted query name to lowercase
  QString wanted=name;
  wanted=wanted.lower();

  // Traverse through list to find the query with "name"
  Query* lookQuery=queryList.first();
  while(lookQuery)
  {
    if(lookQuery->getName().lower()==wanted) return lookQuery;
    lookQuery=queryList.next();
  }
  // No query by that name found? Must be a new query request. Return 0
  return 0;
}

void Server::addPendingNickList(const QString& channelName,const QStringList& nickList)
{
  Channel* outChannel=getChannelByName(channelName);
#ifdef USE_NICKINFO
  ChannelNickList pendingChannelNickList;
  // Update NickInfo.
  if (outChannel)
  {
    for(unsigned int i=0;i<nickList.count();i++)
    {
      QString nickname = nickList[i].section(" ",0,0);
      unsigned int mode = nickList[i].section(" ",1,1).toInt();
      if (!nickname.isEmpty()) {
	      ChannelNickPtr channelNick = addNickToJoinedChannelsList(channelName, nickname);
	      Q_ASSERT(channelNick);
	      //KX << _S(channelName) << _S(nickname) << _S(mode) << endl;
	      channelNick->setMode(mode);
	      pendingChannelNickList.append(channelNick);
      }
    }
  }
  if(outChannel) outChannel->addPendingNickList(pendingChannelNickList);
#else
  if(outChannel) outChannel->addPendingNickList(nickList);
#endif
}

// Adds a nickname to the joinedChannels list.
// Creates new NickInfo if necessary.
// If needed, moves the channel from the unjoined list to the joined list.
// If needed, moves the nickname from the Offline to the Online list.
// Returns the NickInfo for the nickname.
#ifdef USE_NICKINFO
ChannelNickPtr Server::addNickToJoinedChannelsList(const QString& channelName, const QString& nickname)
{
  bool doChannelJoinedSignal = false;
  bool doWatchedNickChangedSignal = false;
  bool doChannelMembersChangedSignal = false;
  QString lcNickname = nickname.lower();
  // Create NickInfo if not already created.
  NickInfoPtr nickInfo = obtainNickInfo(nickname);
  // Move the channel from unjoined list (if present) to joined list.
  QString lcChannelName = channelName.lower();
  ChannelNickMap *channel;
  if (unjoinedChannels.contains(lcChannelName))
  {
    channel = unjoinedChannels[lcChannelName];
    unjoinedChannels.remove(lcChannelName);
    joinedChannels.insert(lcChannelName, channel);
  }
  else
  {
    // Create a new list in the joined channels if not already present.
    if (!joinedChannels.contains(lcChannelName))
    { 
      channel = new ChannelNickMap;
      joinedChannels.insert(lcChannelName, channel);
      doChannelJoinedSignal = true;
    }
    else 
      channel = joinedChannels[lcChannelName];
  }
  // Add NickInfo to channel list if not already in the list.
  ChannelNickPtr channelNick;
  if (!channel->contains(lcNickname))
  {
    channelNick = new ChannelNick(nickInfo, false, false, false, false, false);
    Q_ASSERT(channelNick);
    channel->insert(lcNickname, channelNick);
    doChannelMembersChangedSignal = true;
  }
  channelNick = (*channel)[lcNickname];
  Q_ASSERT(channelNick); //Since we just added it if it didn't exist, it should be guaranteed to exist now
  // Move from the Offline to Online lists.
  if (nicknamesOffline.contains(lcNickname))
  {
    nicknamesOnline.insert(lcNickname, nickInfo);
    nicknamesOffline.remove(lcNickname);
    doWatchedNickChangedSignal = true;
  }
  if (doWatchedNickChangedSignal) emit watchedNickChanged(this, nickInfo, true);
  if (doChannelJoinedSignal) emit channelJoinedOrUnjoined(this, channelName, true);
  if (doChannelMembersChangedSignal) emit channelMembersChanged(this, channelName, true, false, nickname);
  return channelNick;
}
#else
ChannelNickPtr Server::addNickToJoinedChannelsList(const QString&, const QString&) { return 0; }
#endif

/** This function should _only_ be called from the ChannelNick class.
 *  This function should also be the only one to emit this signal.
 *  In this class, when channelNick is changed, it emits its own signal, and
 *  calls this function itself.
 */
void Server::emitChannelNickChanged(const ChannelNickPtr channelNick) {
#ifdef USE_NICKINFO
  emit channelNickChanged(this, channelNick);
#endif
}
/** This function should _only_ be called from the NickInfo class.
 *  This function should also be the only one to emit this signal.
 *  In this class, when nickInfo is changed, it emits its own signal, and
 *  calls this function itself.
 */
void Server::emitNickInfoChanged(const NickInfoPtr nickInfo) {
#ifdef USE_NICKINFO
  emit nickInfoChanged(this, nickInfo);
#endif
}

// Adds a nickname to the unjoinedChannels list.
// Creates new NickInfo if necessary.
// If needed, moves the channel from the joined list to the unjoined list.
// If needed, moves the nickname from the Offline to Online lists.
// If mode != 99 sets the mode for this nick in this channel.
// Returns the NickInfo for the nickname.
#ifdef USE_NICKINFO
ChannelNickPtr Server::addNickToUnjoinedChannelsList(const QString& channelName, const QString& nickname)
{
  bool doChannelUnjoinedSignal = false;
  bool doWatchedNickChangedSignal = false;
  bool doChannelMembersChangedSignal = false;
  QString lcNickname = nickname.lower();
  // Create NickInfo if not already created.
  NickInfoPtr nickInfo = obtainNickInfo(nickname);
  // Move the channel from joined list (if present) to unjoined list.
  QString lcChannelName = channelName.lower();
  ChannelNickMap *channel;
  if (joinedChannels.contains(lcChannelName))
  {
    channel = joinedChannels[lcChannelName];
    joinedChannels.remove(lcChannelName);
    unjoinedChannels.insert(lcChannelName, channel);
    doChannelUnjoinedSignal = true;
  }
  else
  {
    // Create a new list in the unjoined channels if not already present.
    if (!unjoinedChannels.contains(lcChannelName))
    {
      channel = new ChannelNickMap;
      unjoinedChannels.insert(lcChannelName, channel);
      doChannelUnjoinedSignal = true;
    }
    else
      channel = unjoinedChannels[lcChannelName];
  }
  // Add NickInfo to unjoinedChannels list if not already in the list.
  ChannelNickPtr channelNick;
  if (!channel->contains(lcNickname))
  {
    channelNick = new ChannelNick(nickInfo, false, false, false, false, false);
    channel->insert(lcNickname, channelNick);
    doChannelMembersChangedSignal = true;
  }
  channelNick = (*channel)[lcNickname];
  // Move from the Offline to Online lists.
  if (nicknamesOffline.contains(lcNickname))
  {
    nickInfo = nicknamesOffline[lcNickname];
    nicknamesOffline.remove(lcNickname);
    nicknamesOnline.insert(lcNickname, nickInfo);
    doWatchedNickChangedSignal = true;
  }
  // Set the mode for the nick in this channel.
  if (doWatchedNickChangedSignal) emit watchedNickChanged(this, nickInfo, true);
  if (doChannelUnjoinedSignal) emit channelJoinedOrUnjoined(this, channelName, false);
  if (doChannelMembersChangedSignal) emit channelMembersChanged(this, channelName, false, false, nickname);
  return channelNick;
}
#else
ChannelNickPtr Server::addNickToUnjoinedChannelsList(const QString&, const QString&) { return 0; }
#endif

// Adds a nickname to the Online list, removing it from the Offline list, if present.
// Returns the NickInfo of the nickname.
// Creates new NickInfo if necessary.
#ifdef USE_NICKINFO
NickInfoPtr Server::addNickToOnlineList(const QString& nickname)
{
  bool doSignal = false;
  QString lcNickname = nickname.lower();
  nicknamesOffline.remove(lcNickname);
  NickInfoPtr nickInfo = obtainNickInfo(nickname);
  if (!nicknamesOnline.contains(lcNickname))
  {
    nicknamesOnline.insert(lcNickname, nickInfo);
    doSignal = true;
  }
  if (doSignal) emit watchedNickChanged(this, nickInfo, true);
  Konversation::Addressbook::self()->emitContactPresenceChanged(nickInfo->getAddressee().uid());
    
  return nickInfo;
}
#else
NickInfoPtr Server::addNickToOnlineList(const QString&) { return 0; }
#endif

// Adds a nickname to the Offline list provided it is on the watch list,
// removing it from the Online list, if present.
// Also removes it from all channels on the joined and unjoined lists.
// Returns the NickInfo of the nickname or 0 if deleted altogether.
// Creates new NickInfo if necessary.
#ifdef USE_NICKINFO
NickInfoPtr Server::addNickToOfflineList(const QString& nickname, const QStringList& watchList)
{
  bool doSignal = false;
  QString lcNickname = nickname.lower();
  nicknamesOnline.remove(lcNickname);
  NickInfoPtr nickInfo;
  if (watchList.find(lcNickname) != watchList.end())
  {
    nickInfo = obtainNickInfo(nickname);
    if (!nicknamesOffline.contains(lcNickname))
    {
      nicknamesOffline.insert(lcNickname, nickInfo);
      doSignal = true;
    }
  }
  else
  {
    nickInfo = 0;
  }
  // Delete the nickname from all channels (joined or unjoined).
  // When deleted from last channel and not on the watch list,
  // the nick will be deleted altogether.
  QStringList nickChannels = getNickChannels(lcNickname);
  for (QStringList::iterator it = nickChannels.begin();it != nickChannels.end();)
  {
    QString channel = *it;
    it++;  //Make sure we iterate before the channel is (possibly) deleted.
    removeChannelNick(channel, lcNickname);
  }
  if (doSignal) emit watchedNickChanged(this, nickInfo, false);
  Konversation::Addressbook::self()->emitContactPresenceChanged(Konversation::Addressbook::self()->getKABCAddresseeFromNick(lcNickname).uid(), 1);
  return nickInfo;
}
#else
NickInfoPtr Server::addNickToOfflineList(const QString&, const QStringList&) { return 0; }
#endif

// Remove nickname from a channel (on joined or unjoined lists).
// Delete the nickname altogether if no longer on any lists.
#ifdef USE_NICKINFO
void Server::removeChannelNick(const QString& channelName, const QString& nickname)
{
  bool doSignal = false;
  bool joined = false;
  QString lcChannelName = channelName.lower();
  QString lcNickname = nickname.lower();
  ChannelNickMap *channel;
  if (joinedChannels.contains(lcChannelName))
  {
    channel = joinedChannels[lcChannelName];
    if (channel->contains(lcNickname))
    {
      channel->remove(lcNickname);
      doSignal = true;
      joined = true;
      // TODO: If channel is now empty, delete it?
    }
  }
  else
  {
    if (unjoinedChannels.contains(lcChannelName))
    {
      channel = unjoinedChannels[lcChannelName];
      if (channel->contains(lcNickname))
      {
        channel->remove(lcNickname);
        doSignal = true;
        joined = false;
        // TODO: If channel is now empty, delete it?
      }
    }
    // If nickname is no longer on any list, delete it altogether.
    QStringList nickChannels = getNickChannels(lcNickname);
    if (nickChannels.isEmpty())
    {
      if (!nicknamesOnline.contains(lcNickname) && !nicknamesOffline.contains(lcNickname) && !queryNicks.contains(lcNickname))
      {
        allNicks.remove(lcNickname);
      }
    }
  }
  if (doSignal) emit channelMembersChanged(this, channelName, joined, true, nickname);
}
#else
void Server::removeChannelNick(const QString&, const QString&) { }
#endif

QString Server::getNotifyString() {
  if( KonversationApplication::preferences.getNotifyString() != QString::null ) 
    return KonversationApplication::preferences.getNotifyString() +" " + Konversation::Addressbook::self()->allContactsNicks().join(" ");
  else
    return Konversation::Addressbook::self()->allContactsNicks().join(" ");
}

// Remove channel from the joined list.
// Nicknames in the channel are added to the unjoined list if they are in the watch list.
#ifdef USE_NICKINFO
void Server::removeJoinedChannel(const QString& channelName)
{
  bool doSignal = false;
  QString watchList = getNotifyString();
  QStringList watchListLower = QStringList::split(' ', watchList.lower());
  QString lcChannelName = channelName.lower();
  // Move the channel nick list from the joined to unjoined lists.
  ChannelNickMap *channel;
  ChannelNickMap::Iterator member;
  if (joinedChannels.contains(lcChannelName))
  {
    channel = joinedChannels[lcChannelName];
    joinedChannels.remove(lcChannelName);
    unjoinedChannels.insert(lcChannelName, channel);
    // Remove nicks not on the watch list.
    bool allDeleted = true;
    Q_ASSERT(channel);
    if(!channel) return; //already removed.. hmm
    for ( member = channel->begin(); member != channel->end() ;)
    {
      QString lcNickname = member.key();
      if (watchListLower.find(lcNickname) == watchListLower.end())
      {
        // Remove the nickname from the unjoined channel.  If nickname is no longer
        // on any lists, it is deleted altogether.
        removeChannelNick(lcChannelName, lcNickname);
	member = channel->begin();
      }
      else
      {
        allDeleted = false;
	member++;
      }
    }
    // If all were deleted, remove the channel from the unjoined list.
    if (allDeleted)
    {
      channel = unjoinedChannels[lcChannelName];
      unjoinedChannels.remove(lcChannelName);
      delete channel;  // recover memory!
    }
  }
  if (doSignal) emit channelJoinedOrUnjoined(this, channelName, false);
}
#else
void Server::removeJoinedChannel(const QString&) {}
#endif


// Renames a nickname in all NickInfo lists.
// Returns pointer to the NickInfo object or 0 if nick not found.
#ifdef USE_NICKINFO
void Server::renameNickInfo(NickInfoPtr nickInfo, const QString& newname)
{
  if (nickInfo)
  {
    // Get existing lowercase nickname and rename nickname in the NickInfo object.
    QString lcNickname = nickInfo->getNickname().lower();
    nickInfo->setNickname(newname);
    QString lcNewname = newname.lower();
    // Rename the key in allNicks list.
    allNicks.remove(lcNickname);
    allNicks.insert(lcNewname, nickInfo);
    // Rename key in the joined and unjoined lists.
    QStringList nickChannels = getNickChannels(lcNickname);
    for (unsigned int index=0;index<nickChannels.count();index++)
    {
      const ChannelNickMap *channel = getChannelMembers(nickChannels[index]);
      Q_ASSERT(channel);
      ChannelNickPtr member = (*channel)[lcNickname];
      Q_ASSERT(member);
      const_cast<ChannelNickMap *>(channel)->remove(lcNickname);
      const_cast<ChannelNickMap *>(channel)->insert(lcNewname, member);
    }
    // Rename key in the Online, Offline, and Query lists.
    if (nicknamesOnline.contains(lcNickname))
    {
      nicknamesOnline.remove(lcNickname);
      nicknamesOnline.insert(lcNewname, nickInfo);
    }
    if (nicknamesOffline.contains(lcNickname))
    {
      nicknamesOffline.remove(lcNickname);
      nicknamesOffline.insert(lcNewname, nickInfo);
    }
    if (queryNicks.contains(lcNickname))
    {
      queryNicks.remove(lcNickname);
      queryNicks.insert(lcNewname, nickInfo);
    }
  } else {
    kdDebug() << "server::renameNickInfo() was called for newname='" << newname << "' but nickInfo is null" << endl;
  }
}
#else
void Server::renameNickInfo(NickInfoPtr, const QString&) { return; }
#endif

void Server::noMorePendingNicks(const QString& channelName)
{
  Channel* outChannel=getChannelByName(channelName);
  if(outChannel) outChannel->setPendingNicks(false);
}

void Server::addNickToChannel(const QString &channelName,const QString &nickname,const QString &hostmask,
                              bool admin,bool owner,bool op,bool halfop,bool voice)
{
  Channel* outChannel=getChannelByName(channelName);

#ifdef USE_NICKINFO
  // Update NickInfo.
  ChannelNickPtr channelNick = addNickToJoinedChannelsList(channelName, nickname);
  channelNick->setMode(admin,owner,op,halfop,voice);
  if(outChannel) outChannel->addNickname(channelNick);
  NickInfoPtr nickInfo = channelNick->getNickInfo();
  if ((nickInfo->getHostmask() != hostmask) && !hostmask.isEmpty())
  {
    nickInfo->setHostmask(hostmask);
  }
#else

  if(outChannel) outChannel->addNickname(nickname,hostmask,admin,owner,op,halfop,voice);
#endif
}

void Server::nickJoinsChannel(const QString &channelName, const QString &nickname, const QString &hostmask)
{
  Channel* outChannel=getChannelByName(channelName);
  if(outChannel)
  {

    // OnScreen Message
    if(KonversationApplication::preferences.getOSDShowChannelEvent() && outChannel->notificationsEnabled())
    {
      KonversationApplication *konvApp=static_cast<KonversationApplication *>(KApplication::kApplication());
      konvApp->osd->showOSD(i18n( "(%1) %2 has joined this channel. (%3)" )
                            .arg(channelName).arg(nickname).arg(hostmask));
    }

#ifdef USE_NICKINFO
    // Update NickInfo.
    ChannelNickPtr channelNick = addNickToJoinedChannelsList(channelName, nickname);
    NickInfoPtr nickInfo = channelNick->getNickInfo();
    if ((nickInfo->getHostmask() != hostmask) && !hostmask.isEmpty())
    {
      nickInfo->setHostmask(hostmask);
    }
    outChannel->joinNickname(channelNick);
#else

    outChannel->joinNickname(nickname,hostmask);
#endif
  }
}

void Server::addHostmaskToNick(const QString& sourceNick, const QString& sourceHostmask)
{
  // remember my IP for DCC sending
  if(ownIpByServer.isEmpty() && sourceNick==nickname)  // myself
  {
    QString myhost = sourceHostmask.section('@', 1);
#if KDE_IS_VERSION(3,2,90)
    KNetwork::KResolverResults res = KNetwork::KResolver::resolve(myhost, "");
    if(res.size() > 0)
      ownIpByServer = res.first().address().nodeName();
#else
    QPtrList<KAddressInfo> res = KExtendedSocket::lookup(myhost, "");
    if(res.count() > 0)
      ownIpByServer = res.first()->address()->nodeName();
#endif
  }



#ifdef USE_NICKINFO
  // Update NickInfo.
  NickInfoPtr nickInfo=getNickInfo(sourceNick);
  if (nickInfo)
  {
    if ((nickInfo->getHostmask() != sourceHostmask) && !sourceHostmask.isEmpty())
    {
      nickInfo->setHostmask(sourceHostmask);
    }
  }
#else

  Channel* channel=channelList.first();
  while(channel)
  {
    Nick* nick=channel->getNickByName(sourceNick);
    if(nick)
      nick->setHostmask(sourceHostmask);
    channel=channelList.next();
  }

#endif
  // Set hostmask for query with the same name
  Query* query=getQueryByName(sourceNick);
  if(query) query->setHostmask(sourceHostmask);
}

void Server::removeNickFromChannel(const QString &channelName, const QString &nickname, const QString &reason, bool quit)
{
  Channel* outChannel=getChannelByName(channelName);
  if(outChannel)
  {
    // OnScreen Message
    if(KonversationApplication::preferences.getOSDShowChannelEvent() && outChannel->notificationsEnabled())
    {
      KonversationApplication *konvApp=static_cast<KonversationApplication *>(KApplication::kApplication());
      konvApp->osd->showOSD(i18n( "(%1) %2 has left this channel. (%3)" )
                            .arg(channelName).arg(nickname).arg(reason));
    }
#ifdef USE_NICKINFO
    ChannelNickPtr channelNick = getChannelNick(channelName, nickname);
    if(channelNick) outChannel->removeNick(channelNick,reason,quit);
#else
    if(outChannel->getNickByName(nickname)) outChannel->removeNick(nickname,reason,quit);
#endif
  }

#ifdef USE_NICKINFO
  // Update NickInfo.  Remove the nick from the channel.
  removeChannelNick(channelName, nickname);
#endif
}

void Server::nickWasKickedFromChannel(const QString &channelName, const QString &nickname, const QString &kicker, const QString &reason)
{
  Channel* outChannel=getChannelByName(channelName);
  if(outChannel)
  {
#ifdef USE_NICKINFO
    ChannelNickPtr channelNick = getChannelNick(channelName, nickname);
    ChannelNickPtr kickerNick = getChannelNick(channelName, kicker);
    if(channelNick) outChannel->kickNick(channelNick, *kickerNick, reason);
#else
    if(outChannel->getNickByName(nickname)) outChannel->kickNick(nickname,kicker,reason);
#endif
  }

  // TODO: Need to update NickInfo, or does that happen in method above?
}

void Server::removeNickFromServer(const QString &nickname,const QString &reason)
{
  Channel* channel=channelList.first();
  while(channel)
  {
    // Check if nick is this channel or not.
    Channel *nextchannel = channelList.next();
    if( channel->getNickByName( nickname ) )
      removeNickFromChannel(channel->getName(),nickname,reason,true);
    channel=nextchannel;
  }

#ifdef USE_NICKINFO
  // Unless nick is in the watch list, delete it altogether, otherwise move it to nicknamesOffline list.
  QString watchList = getNotifyString();
  QStringList watchListLower = QStringList::split(' ', watchList.lower());
  addNickToOfflineList(nickname, watchListLower);
#endif
}

void Server::renameNick(const QString &nickname, const QString &newNick)
{
  if(nickname.isEmpty() || newNick.isEmpty()) {
    kdDebug() << "server::renameNick called with empty strings!  Trying to rename '" << nickname << "' to '" << newNick << "'" << endl;
    return;
  }
#ifdef USE_NICKINFO
  //Actually do the rename.
  NickInfoPtr nickInfo = getNickInfo(nickname);
  if(!nickInfo) {
    kdDebug() << "server::renameNick called for nickname '" << nickname << "' to '" << newNick << "' but getNickInfo('" << nickname << "') returned no results." << endl;
    return;
  }
  kdDebug() << "server::renameNick - renaming " << nickname << "' to '" << newNick << "'" << endl;
  renameNickInfo(nickInfo, newNick);
  //The rest of the code below allows the channels to echo to the user to tell them that the nick has changed.
#endif

  // Rename the nick in every channel they are in
  Channel* channel=channelList.first();
  while(channel)
  {
#ifdef USE_NICKINFO
    // All we do is notify that the nick has been renamed.. we haven't actually renamed it yet
    // Note that NickPanel has already updated, so pass new nick to getNickByName.
    if(channel->getNickByName(newNick)) channel->nickRenamed(nickname, *nickInfo);
#else
    if(channel->getNickByName(nickname)) channel->renameNick(nickname,newNick);
#endif
    channel=channelList.next();
  }
  // If this was our own nickchange, tell our server object about it
  if(nickname==getNickname()) setNickname(newNick);
  // If we had a query with this nick, change that name, too
  Query* query=queryList.first();
  while(query)
  {
    if(query->getName().lower()==nickname.lower())
    {
      query->setName(newNick);
    }
    query=queryList.next();
  }

}

#ifdef USE_NICKINFO
void Server::userhost(const QString& nick,const QString& hostmask,bool away,bool /* ircOp */)
{
  addHostmaskToNick(nick,hostmask);
  NickInfoPtr nickInfo = getNickInfo(nick);
  if (nickInfo)
  {
    if (nickInfo->isAway() != away)
    {
      nickInfo->setAway(away);
    }
  }
}
#else
void Server::userhost(const QString& nick,const QString& hostmask,bool /* away */ ,bool /* ircOp */)
{
  addHostmaskToNick(nick,hostmask);
}
#endif

void Server::appendToChannel(const QString& channel,const QString& nickname,const QString& message)
{
  Channel* outChannel=getChannelByName(channel);
  if(outChannel) outChannel->append(nickname,message);

  // OnScreen Message
  if (KonversationApplication::preferences.getOSDShowChannel() && outChannel->notificationsEnabled())
  {
    KonversationApplication *konvApp=static_cast<KonversationApplication *>(KApplication::kApplication());
    konvApp->osd->showOSD("(" + channel + ") <" + nickname + "> " + message);
  }
}

void Server::appendActionToChannel(const QString& channel,const QString& nickname,const QString& message)
{
  Channel* outChannel=getChannelByName(channel);
  if(outChannel) outChannel->appendAction(nickname,message);
}

void Server::appendServerMessageToChannel(const QString& channel,const QString& type,const QString& message)
{
  Channel* outChannel=getChannelByName(channel);
  if(outChannel) outChannel->appendServerMessage(type,message);
}

void Server::appendCommandMessageToChannel(const QString& channel,const QString& command,const QString& message)
{
  Channel* outChannel=getChannelByName(channel);
  if(outChannel) outChannel->appendCommandMessage(command,message);
}

void Server::appendToQuery(const QString& queryName,const QString& message)
{
  Query* outQuery=getQueryByName(queryName);
  if(outQuery)
  {
    outQuery->appendQuery(queryName,message);
    // OnScreen Message
    if(KonversationApplication::preferences.getOSDShowQuery() && outQuery->notificationsEnabled())
    {
      KonversationApplication *konvApp=static_cast<KonversationApplication *>(KApplication::kApplication());
      konvApp->osd->showOSD(i18n( "(Query) <%1> %2" ).arg(queryName).arg(message));
    }
  }
  else kdWarning() << "Server::appendToQuery(" << queryName << "): Query not found!" << endl;
}

void Server::appendActionToQuery(const QString& queryName,const QString& message)
{
  Query* outQuery=getQueryByName(queryName);
  if(outQuery) outQuery->appendAction(queryName,message);
  else kdWarning() << "Server::appendActionToQuery(" << queryName << "): Query not found!" << endl;
}

void Server::appendServerMessageToQuery(const QString& queryName,const QString& type,const QString& message)
{
  Query* outQuery=getQueryByName(queryName);
  if(outQuery) outQuery->appendServerMessage(type,message);
  else kdWarning() << "Server::appendServerMessageToQuery(" << queryName << "): Query not found!" << endl;
}

void Server::appendCommandMessageToQuery(const QString& queryName,const QString& command,const QString& message)
{
  Query* outQuery=getQueryByName(queryName);
  if(outQuery) outQuery->appendCommandMessage(command,message);
  else kdWarning() << "Server::appendCommandMessageToQuery(" << queryName << "): Query not found!" << endl;
}

void Server::appendStatusMessage(const QString& type,const QString& message)
{
  getMainWindow()->appendToFrontmost(type,message,statusView);
}

void Server::setNickname(const QString &newNickname)
{
  nickname=newNickname;
  statusView->setNickname(newNickname);
}

void Server::setChannelTopic(const QString &channel, const QString &newTopic)
{
  Channel* outChannel=getChannelByName(channel);
  if(outChannel)
  {
    // encoding stuff is done in send()
    outChannel->setTopic(newTopic);
  }
}

void Server::setChannelTopic(const QString& nickname, const QString &channel, const QString &newTopic) // Overloaded
{
  Channel* outChannel=getChannelByName(channel);
  if(outChannel)
  {
    // encoding stuff is done in send()
    outChannel->setTopic(nickname,newTopic);
  }
}

void Server::setTopicAuthor(const QString& channel,const QString& author)
{
  Channel* outChannel=getChannelByName(channel);
  if(outChannel)
  {
    outChannel->setTopicAuthor(author);
  }
}

bool Server::isNickname(const QString &compare)
{
  return (nickname==compare);
}

QString Server::getNickname() const
{
  return nickname;
}

QString Server::parseWildcards(const QString& toParse,
                               const QString& sender,
                               const QString& channelName,
                               const QString& channelKey,
                               const QString& nick,
                               const QString& parameter)
{
  return parseWildcards(toParse,sender,channelName,channelKey,QStringList::split(' ',nick),parameter);
}

QString Server::parseWildcards(const QString& toParse,
                               const QString& sender,
                               const QString& channelName,
                               const QString& channelKey,
                               const QStringList& nickList,
                               const QString& /*parameter*/)
{
  // TODO: parameter handling.
  //       since parameters are not functional yet

  // make a copy to work with
  QString out(toParse);
  // define default separator and regular expression for definition
  QString separator(" ");
  QRegExp separatorRegExp("%s[^%]*%");

  // separator definition found?
  int pos=out.find(separatorRegExp);
  if(pos!=-1)
  {
    // TODO: This could be better done with .cap() and proper RegExp ...
    // skip "%s" at the beginning
    pos+=2;
    // copy out all text to the next "%" as new separator
    separator=out.mid(pos,out.find("%",pos+1)-pos);
    // remove separator definition from string
    out.replace(separatorRegExp, QString::null);
  }

  out.replace(QRegExp("%u"),nickList.join(separator));
  if(!channelName.isEmpty()) out.replace(QRegExp("%c"),channelName);
  out.replace(QRegExp("%o"),sender);
  if(!channelKey.isEmpty()) out.replace(QRegExp("%k"),channelKey);
  if(!serverKey.isEmpty()) out.replace(QRegExp("%K"),serverKey);
  out.replace(QRegExp("%n"),"\n");
//  out.replace(QRegExp("%f"),getFortuneCookie());
//  out.replace(QRegExp("%p"),parameter);

  // finally replace all "%p" with "%"
  out.replace(QRegExp("%p"),"%");

  return out;
}

void Server::setIrcName(const QString &newIrcName)
{
  ircName=newIrcName;
}

QString Server::getIrcName() const
{
  return ircName;
}

Konversation::OutputFilter* Server::getOutputFilter()
{
  return outputFilter;
}

void Server::sendToAllChannels(const QString &text)
{
  // Send a message to all channels we are in
  Channel* channel=channelList.first();

  while(channel)
  {
    channel->sendChannelText(text);
    channel=channelList.next();
  }
}

void Server::invitation(const QString& nick,const QString& channel)
{
  if(KonversationApplication::preferences.getAutojoinOnInvite() &&
     KMessageBox::questionYesNo(static_cast<QWidget*>(0),
                                i18n("You were invited by %1 to join channel %2. "
                                     "Do you accept the invitation?").arg(nick).arg(channel),
                                i18n("Invitation"),
                                KStdGuiItem::yes(),
                                KStdGuiItem::no(),
                                "Invitation")==KMessageBox::Yes)
  {
    sendJoinCommand(channel);
  }
}

void Server::scriptNotFound(const QString& name)
{
  appendStatusMessage(i18n("DCOP"),i18n("Error: Could not find script \"%1\".").arg(name));
}

void Server::scriptExecutionError(const QString& name)
{
  appendStatusMessage(i18n("DCOP"),i18n("Error: Could not execute script \"%1\". Check file permissions.").arg(name));
}

void Server::away()
{
  isAway=true;
  emit awayState(isAway);

  if(!getIdentity()->getAwayNick().isEmpty() &&
     getIdentity()->getAwayNick() != getNickname()) {
    nonAwayNick = getNickname();
    queue("NICK " + getIdentity()->getAwayNick());
  }

  if(identity->getInsertRememberLineOnAway())
  {
    emit awayInsertRememberLine();
  }
  // TODO: call renameNickInfo ?
}

void Server::unAway()
{
  isAway=false;
  emit awayState(isAway);

  if(!getIdentity()->getAwayNick().isEmpty() && !nonAwayNick.isEmpty()) {
    queue("NICK " + nonAwayNick);
  }

  // TODO: call renameNickInfo ?
}

bool Server::isAChannel(const QString &check)
{
  QChar initial=check.at(0);

  return (initial=='#' || initial=='&' || initial=='+' || initial=='!');
}

void Server::addRawLog(bool show)
{
  if(!rawLog) rawLog=getMainWindow()->addRawLog(this);

  connect(this,SIGNAL (serverOnline(bool)),rawLog,SLOT (serverOnline(bool)) );

  // bring raw log to front since the main window does not do this for us
  if(show) getMainWindow()->showView(rawLog);
}

// in MDI mode thiis function may only be run from the raw log panel itself!
void Server::closeRawLog()
{
  if(rawLog)
  {
#ifndef USE_MDI
    delete rawLog;
#endif
    rawLog=0;
  }
}

void Server::addChannelListPanel()
{
  if(!channelListPanel)
  {
    channelListPanel=getMainWindow()->addChannelListPanel(this);

    connect(channelListPanel,SIGNAL (refreshChannelList()),this,SLOT (requestChannelList()) );
    connect(channelListPanel,SIGNAL (joinChannel(const QString&)),this,SLOT (sendJoinCommand(const QString&)) );
    connect(this,SIGNAL (serverOnline(bool)),channelListPanel,SLOT (serverOnline(bool)) );

    connect(&inputFilter,SIGNAL (addToChannelList(const QString&,int,const QString&)),
          channelListPanel,SLOT (addToChannelList(const QString&,int,const QString&)) );
  }
}

ChannelListPanel* Server::getChannelListPanel() const { return channelListPanel; }

// in MDI mode this function may only be called from the channel list panel itself!
void Server::closeChannelListPanel()
{
  if(channelListPanel)
  {
#ifndef USE_MDI
    delete channelListPanel;
#endif
    channelListPanel=0;
  }
}

void Server::autoRejoinChannels()
{
  QStringList channels;
  QStringList keys;

  for(Channel* ch = channelList.first(); ch; ch = channelList.next()) {
    channels.append(ch->getName());
    keys.append(ch->getKey());
  }

  QString joinString("JOIN "+channels.join(",")+" "+keys.join(","));
  queue(joinString);
}

void Server::setIdentity(Identity* newIdentity)
{
  identity=newIdentity;
}

Identity* Server::getIdentity() { return identity; }

void Server::setMainWindow(KonversationMainWindow* newMainWindow) { mainWindow=newMainWindow; }
KonversationMainWindow* Server::getMainWindow() const { return mainWindow; }

bool Server::connected() { return alreadyConnected; }

void Server::sendMultiServerCommand(const QString& command, const QString& parameter)
{
  emit multiServerCommand(command, parameter);
}

void Server::executeMultiServerCommand(const QString& command, const QString& parameter)
{
  if(command == "away") {
    QString str = KonversationApplication::preferences.getCommandChar() + command;

    if(!parameter.isEmpty()) {
      str += " " + parameter;
    }

    Konversation::OutputFilterResult result = outputFilter->parse(getNickname(), str, QString::null);
    queue(result.toServer);
  } else if(command == "msg") {
    sendToAllChannelsAndQueries(parameter);
  } else {
    sendToAllChannelsAndQueries(KonversationApplication::preferences.getCommandChar() + command + " " + parameter);
  }
}

void Server::sendToAllChannelsAndQueries(const QString& text)
{
  // Send a message to all channels we are in
  Channel* channel=channelList.first();

  while(channel)
  {
    channel->sendChannelText(text);
    channel=channelList.next();
  }

  // Send a message to all queries we are in
  Query* query=queryList.first();

  while(query)
  {
    query->sendQueryText(text);
    query=queryList.next();
  }
}

void Server::reconnect() {
  if(!isConnected()) {
    reconnectCounter = 0;
    connectToIRCServer();
  } else {
    getStatusView()->appendServerMessage("Error", i18n("Server already connected."));
  }
}

void Server::connectToNewServer(const QString& server, const QString& port, const QString& password)
{
  KonversationApplication *konvApp = static_cast<KonversationApplication*>(KApplication::kApplication());
  konvApp->quickConnectToServer(server, port, KonversationApplication::preferences.getNickname(0), password);
}

QString Server::awayTime()
{
  QString retVal;

  if(isAway) {
    int diff = QDateTime::currentDateTime().toTime_t() - m_awayTime;
    int num = diff / 3600;

    if(num < 10) {
      retVal = "0" + QString::number(num) + ":";
    } else {
      retVal = QString::number(num) + ":";
    }

    num = (diff % 3600) / 60;

    if(num < 10) {
      retVal += "0";
    }

    retVal += QString::number(num) + ":";

    num = (diff % 3600) % 60;

    if(num < 10) {
      retVal += "0";
    }

    retVal += QString::number(num);
  } else {
    retVal = "00:00:00";
  }

  return retVal;
}

void Server::startAwayTimer()
{
  m_awayTime = QDateTime::currentDateTime().toTime_t();
}


/** Intended to be called when the addressbook changes.  Cycles through all the nicks and
 *  calls 'refreshAddressee' on all of them.
 */
void Server::slotLoadAddressees() {
  kdDebug() << "server::slotLoadAddressess " << endl;
  for(NickInfoMap::Iterator it=nicknamesOnline.begin(); it != nicknamesOnline.end(); ++it)
  {
    NickInfoPtr addressee = it.data();
    addressee->refreshAddressee();
  }
}


#include "server.moc"

