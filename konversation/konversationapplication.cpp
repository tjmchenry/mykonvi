/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  konversationapplication.cpp  -  The main application
  begin:     Mon Jan 28 2002
  copyright: (C) 2002 by Dario Abatianni
  email:     eisfuchs@tigress.com
*/

#include <qtextcodec.h>
#include <qregexp.h>
#include <qfileinfo.h>

#include <kdebug.h>
#include <kconfig.h>
#include <dcopclient.h>
#include <kdeversion.h>
#include <kstandarddirs.h>
#include <klocale.h>
#include <kmessagebox.h>

#include "konversationapplication.h"
#include "konversationmainwindow.h"
#include "prefsdialog.h"
#include "highlight.h"
#include "server.h"
#include "serverentry.h"
#include "konversationsound.h"
#include "quickconnectdialog.h"

// include static variables
Preferences KonversationApplication::preferences;

KonversationApplication::KonversationApplication()
    : KApplication()
{
  // make sure all vars are initialized properly
  prefsDialog=0;
  quickConnectDialog=0;

  // initialize OSD display here, so we can read the preferences properly
  osd = new OSDWidget( "Konversation" );

  preferences.setOSDFont(font());
  preferences.setTextFont(font());
  preferences.setListFont(font());

  readOptions();

  // Auto-alias scripts
  QStringList scripts = KGlobal::dirs()->findAllResources("data","konversation/scripts/*");
  QFileInfo* fileInfo = new QFileInfo();
  QStringList aliasList(KonversationApplication::preferences.getAliasList());
  QString newAlias;

  for ( QStringList::Iterator it = scripts.begin(); it != scripts.end(); ++it ) {
      fileInfo->setFile( *it );
      if ( fileInfo->isExecutable() ) {
          newAlias = (*it).section('/',-1)+" "+"/exec "+(*it).section('/', -1 );

          if(!aliasList.contains(newAlias))
              aliasList.append(newAlias);
      }
  }

  KonversationApplication::preferences.setAliasList(aliasList);

  // Setup system codec
  // TODO: check if this works now as intended
  QTextCodec::setCodecForCStrings(QTextCodec::codecForLocale());

  // open main window
  mainWindow=new KonversationMainWindow();
  connect(mainWindow,SIGNAL (openPrefsDialog()),this,SLOT (openPrefsDialog()) );
  connect(mainWindow,SIGNAL (openPrefsDialog(Preferences::Pages)),this,SLOT (openPrefsDialog(Preferences::Pages)) );
  connect(mainWindow,SIGNAL (showQuickConnectDialog()), this, SLOT (openQuickConnectDialog()) );
  connect(&preferences,SIGNAL (updateTrayIcon()),mainWindow,SLOT (updateTrayIcon()) );
  connect(this, SIGNAL(prefsChanged()), mainWindow, SLOT(slotPrefsChanged()));

  // handle autoconnect on startup
  QValueList<int> list=preferences.getAutoConnectServerIDs();
  // if there is at least one autoconnect server, start connecting right away
  if(list.count())
  {
    for(unsigned int index=0;index<list.count();index++) connectToServer(list[index]);
    // maybe the user wants to see the prefs dialog anyway
    if(preferences.getShowServerList())
    {
      openPrefsDialog();
    }
  }
  // no autoconnect server, so show the prefs dialog (with exit functionality)
  else
  {
    prefsDialog=new PrefsDialog(&preferences,true);

    connect(prefsDialog,SIGNAL (connectToServer(int)),this,SLOT (connectToServer(int)) );
    connect(prefsDialog,SIGNAL (cancelClicked()),this,SLOT (quitKonversation()) );
    connect(prefsDialog,SIGNAL (prefsChanged()),this,SLOT (saveOptions()) );

    prefsDialog->show();

    connect(&preferences,SIGNAL (requestServerConnection(int)),this,SLOT (connectToAnotherServer(int)) );
    connect(&preferences,SIGNAL (requestSaveOptions()),this,SLOT (saveOptions()) );
  }

  // prepare dcop interface
  dcopObject=new KonvDCOP;
  identDCOP=new KonvIdentDCOP;
  prefsDCOP=new KonvPrefsDCOP;
  if(dcopObject)
  {
    connect(dcopObject,SIGNAL (dcopSay(const QString&,const QString&,const QString&)),
                    this,SLOT (dcopSay(const QString&,const QString&,const QString&)) );
    connect(dcopObject,SIGNAL (dcopInfo(const QString&)),
                    this,SLOT (dcopInfo(const QString&)) );
    connect(dcopObject,SIGNAL (dcopInsertRememberLine()),
                    this,SLOT(insertRememberLine()));
    connect(dcopObject,SIGNAL(dcopConnectToServer(const QString&, int)),
                    this,SLOT(dcopConnectToServer(const QString&, int)));
  }

  // Sound object used to play sound...
  m_sound = new Konversation::Sound(this);

  // take care of user style changes, setting back colors and stuff
  connect(KApplication::kApplication(),SIGNAL (appearanceChanged()),this,SLOT (appearanceChanged()) );

}

KonversationApplication::~KonversationApplication()
{
  saveOptions(false);

  delete dcopObject;
  delete prefsDCOP;
  delete identDCOP;
}

void KonversationApplication::dcopSay(const QString& server,const QString& target,const QString& command)
{
  Server* lookServer=serverList.first();
  while(lookServer)
  {
    if(lookServer->getServerName()==server)
    {
      lookServer->dcopSay(target,command);
//      break; // leave while loop
//FIXME:   <muesli> there's a reason for not breaking this loop, here (which would spent only some
//                  cpu cycles, anyways): i'm connected to two bouncers at the same time, which are
//                  also named the same (same ip, no dns). if a dcopSay gets emerged, it will always
//                  get the _same_ server name as its parameter (both are named the same). although
//                  the channel it gets sent to, is on the second server, it will always try to send
//                  this information to a channel on the first server, which i didn't even join.
//                  this is def. a quick-fix, we should probably handle server-id's instead of -names.
    }
    lookServer=serverList.next();
  }
}

void KonversationApplication::dcopInfo(const QString& string)
{
  Server* lookServer=serverList.first();
  if(lookServer) lookServer->dcopInfo(string);
}

void KonversationApplication::insertRememberLine()
{
  mainWindow->insertRememberLine();
}

void KonversationApplication::connectToServer(int id)
{
  if(connectToAnotherServer(id))
  {
    // to prevent doubleClicked() to crash the dialog
    // FIXME: Seems to have a race, though
    // only close the dialog when we didn't use autoconnect
    if(prefsDialog) prefsDialog->delayedDestruct();
    prefsDialog=0;
  }
}

bool KonversationApplication::connectToAnotherServer(int id)
{
  ServerEntry* chosenServer=preferences.getServerEntryById(id);
  Identity* identity=preferences.getIdentityByName(chosenServer->getIdentity());

  // sanity check for identity
  QString check;

  if(identity->getIdent().isEmpty())
  {
    check+=i18n("Please fill in your <b>ident</b>.<br>");
  }
  if(identity->getRealName().isEmpty())
  {
    check+=i18n("Please fill in your <b>Real name</b>.<br>");
  }
  if(identity->getNickname(0).isEmpty())
  {
    check+=i18n("Please provide at least one <b>Nickname</b>.<br>");
  }
  if(!check.isEmpty())
  {
    KMessageBox::information(0,
                             i18n("<qt>Your identity \"%1\" is not set up correctly:<br>%2</qt>")
                                  .arg(chosenServer->getIdentity())
                                  .arg(check),i18n("Check Identity Settings")
                            );
    if(prefsDialog)
      prefsDialog->openPage(Preferences::IdentityPage);
    else
      openPrefsDialog(Preferences::IdentityPage);

    return false;
  }

  // identity ok, carry on

  mainWindow->show();

  // Check if a server window with same name and port is already open
  Server* newServer=serverList.first();
  while(newServer)
  {
    if(chosenServer->getServerName()==newServer->getServerName() &&
       chosenServer->getPort()==newServer->getPort() &&
       chosenServer->getIdentity()==newServer->getIdentity()->getName())
    {
      QString autoJoinChannel=chosenServer->getChannelName();

      if(newServer->isConnected() || newServer->isConnecting())
      {
        if(!autoJoinChannel.isEmpty())
          newServer->queue("JOIN "+autoJoinChannel+" "+chosenServer->getChannelKey());
      }
      else
      {
        if(!autoJoinChannel.isEmpty())
        {
          newServer->setAutoJoin(true);
          newServer->setAutoJoinChannel(newServer->getAutoJoinChannel()+" "+autoJoinChannel);
          newServer->setAutoJoinChannelKey(newServer->getAutoJoinChannelKey()+" "+chosenServer->getChannelKey());
        }
        else newServer->setAutoJoin(false);

        newServer->connectToIRCServer();
      }
      return true;
    }

    newServer=serverList.next();
  } // endwhile
  // We came this far, so generate a new server

  newServer=new Server(mainWindow,id);

  connect(mainWindow,SIGNAL (startNotifyTimer(int)),newServer,SLOT (startNotifyTimer(int)) );
  connect(mainWindow,SIGNAL (quitServer()),newServer,SLOT (quitServer()) );

  connect(newServer,SIGNAL (nicksNowOnline(Server*,const QStringList&,bool)),mainWindow,SLOT (setOnlineList(Server*,const QStringList&,bool)) );

  connect(newServer,SIGNAL (deleted(Server*)),this,SLOT (removeServer(Server*)) );

  connect(newServer, SIGNAL(multiServerCommand(const QString&, const QString&)),
    this, SLOT(sendMultiServerCommand(const QString&, const QString&)));
  connect(newServer, SIGNAL(awayInsertRememberLine()), this, SLOT(insertRememberLine()));

  serverList.append(newServer);

  return true;
}

void KonversationApplication::quickConnectToServer(const QString& hostName, const QString& port, const QString& nick, const QString& password)
{
  //used for the quick connect dialog and /server command

  Server* newServer = new Server(mainWindow, hostName, port, nick, password);

  connect(mainWindow,SIGNAL (startNotifyTimer(int)),newServer,SLOT (startNotifyTimer(int)) );
  connect(mainWindow,SIGNAL (quitServer()),newServer,SLOT (quitServer()) );

  connect(newServer,SIGNAL (nicksNowOnline(Server*,const QStringList&,bool)),mainWindow,SLOT (setOnlineList(Server*,const QStringList&,bool)) );

  connect(newServer,SIGNAL (deleted(Server*)),this,SLOT (removeServer(Server*)) );

  connect(newServer, SIGNAL(multiServerCommand(const QString&, const QString&)),
    this, SLOT(sendMultiServerCommand(const QString&, const QString&)));
  connect(newServer, SIGNAL(awayInsertRememberLine()), this, SLOT(insertRememberLine()));

  serverList.append(newServer);

}

Server* KonversationApplication::getServerByName(const QString& name)
{
  Server* lookServer=serverList.first();
  while(lookServer)
  {
    if(lookServer->getServerName()==name) return lookServer;
    lookServer=serverList.next();
  }
  return 0;
}

void KonversationApplication::removeServer(Server* server)
{
  serverList.setAutoDelete(false);     // don't delete items when they are removed
  if(!serverList.remove(server))
    kdDebug() << "Could not remove " << server->getServerName() << endl;
}

void KonversationApplication::quitKonversation()
{
  if(prefsDialog) delete prefsDialog;
  prefsDialog=0;

  this->exit();
}

void KonversationApplication::readOptions()
{
  // get standard config file
  KConfig* config=kapp->config();

  // Read configuration and provide the default values
  config->setGroup("General Options");

  // Command char setting
  preferences.setCommandChar(config->readEntry("CommandChar",preferences.getCommandChar()));

  // Pre shell command setting
  preferences.setPreShellCommand(config->readEntry("PreShellCommand",preferences.getPreShellCommand()));

  // Tray icon settings
  preferences.setShowTrayIcon(config->readBoolEntry("ShowTrayIcon",preferences.getShowTrayIcon()));
  preferences.setTrayNotify(config->readBoolEntry("TrayNotify",preferences.getTrayNotify()));
  preferences.setShowBackgroundImage(config->readBoolEntry("ShowBackgroundImage",preferences.getShowBackgroundImage()));

  // Window geometries
  QSize* logfileReaderSize=new QSize(500,300);
  preferences.setNicksOnlineSize(config->readSizeEntry("NicksOnlineGeometry"));
  preferences.setNicknameSize(config->readSizeEntry("NicknameGeometry"));
  preferences.setLogfileReaderSize(config->readSizeEntry("LogfileReaderGeometry",logfileReaderSize));
  preferences.setMultilineEditSize(config->readSizeEntry("MultilineEditGeometry"));
  delete logfileReaderSize;

  preferences.setLogfileBufferSize(config->readNumEntry("LogfileBufferSize",preferences.getLogfileBufferSize()));
  preferences.setScrollbackMax(config->readUnsignedNumEntry("ScrollbackMax",preferences.getScrollbackMax()));

  // Double click actions
  preferences.setChannelDoubleClickAction(config->readEntry("ChannelDoubleClickAction",preferences.getChannelDoubleClickAction()));
  preferences.setNotifyDoubleClickAction(config->readEntry("NotifyDoubleClickAction",preferences.getNotifyDoubleClickAction()));

  // Beep
  preferences.setBeep(config->readBoolEntry("Beep",preferences.getBeep()));

  // Raw log window
  preferences.setRawLog(config->readBoolEntry("RawLog",preferences.getRawLog()));

  // Custom CTCP Version Reply
  preferences.setVersionReply(config->readEntry("VersionReply",preferences.getVersionReply()));

  // Reconnection timeout
  preferences.setMaximumLagTime(config->readNumEntry("MaximumLag",preferences.getMaximumLagTime()));

  preferences.setRedirectToStatusPane(config->readBoolEntry("RedirectServerAndAppMsgToStatusPane",
    preferences.getRedirectToStatusPane()));

  //User interface
  preferences.setShowMenuBar(config->readBoolEntry("ServerWindowMenuBarStatus", preferences.getShowMenuBar()));

  // Appearance
  config->setGroup("Appearance");
  // Fonts
  preferences.setTextFontRaw(config->readEntry("TextFont",preferences.getTextFont().rawName()));
  preferences.setListFontRaw(config->readEntry("ListFont",preferences.getListFont().rawName()));
  preferences.setTimestamping(config->readBoolEntry("Timestamping",preferences.getTimestamping()));
  preferences.setShowDate(config->readBoolEntry("ShowDate",preferences.getShowDate()));
  preferences.setTimestampFormat(config->readEntry("TimestampFormat",preferences.getTimestampFormat()));
  preferences.setShowQuickButtons(config->readBoolEntry("ShowQuickButtons",preferences.getShowQuickButtons()));
  preferences.setShowModeButtons(config->readBoolEntry("ShowModeButtons",preferences.getShowModeButtons()));
  preferences.setCloseButtonsOnTabs(config->readBoolEntry("CloseButtonsOnTabs",preferences.getCloseButtonsOnTabs()));
  preferences.setCloseButtonsAlignRight(config->readBoolEntry("CloseButtonsAlignRight",preferences.getCloseButtonsAlignRight()));

  preferences.setAutoUserhost(config->readBoolEntry("AutoUserhost",preferences.getAutoUserhost()));

  preferences.setUseSpacing(config->readBoolEntry("UseSpacing",preferences.getUseSpacing()));
  preferences.setSpacing(config->readNumEntry("Spacing",preferences.getSpacing()));
  preferences.setMargin(config->readNumEntry("Margin",preferences.getMargin()));

  preferences.setUseParagraphSpacing(config->readBoolEntry("UseParagraphSpacing",preferences.getUseParagraphSpacing()));
  preferences.setParagraphSpacing(config->readNumEntry("ParagraphSpacing",preferences.getParagraphSpacing()));

  QValueList<int> sizes;
  QString sizesString=config->readEntry("ChannelSplitter","10,1");
  sizes.append(sizesString.section(',',0,0).toInt());
  sizes.append(sizesString.section(',',1,1).toInt());
  preferences.setChannelSplitter(sizes);

  preferences.setBackgroundImageName(config->readEntry("BackgroundImage",preferences.getBackgroundImageName()));
  QStringList ircColorList = preferences.getIRCColorList();
  preferences.setIRCColorList(config->readListEntry("IRCColors"));

  if(preferences.getIRCColorList().empty()) {
    preferences.setIRCColorList(ircColorList);
  }

  preferences.setFilterColors(config->readBoolEntry("FilterColorCodes",preferences.getFilterColors()));  //FIXME

  preferences.setShowTabBarCloseButton(config->readBoolEntry("ShowTabBarCloseButton", preferences.getShowTabBarCloseButton()));

  preferences.setShowTopic(config->readBoolEntry("ShowTopic", preferences.getShowTopic()));

  preferences.setShowRememberLineInAllWindows(config->readBoolEntry("ShowRememberLineInAllWindows", preferences.getShowRememberLineInAllWindows()));
  preferences.setFocusNewQueries(config->readBoolEntry("FocusNewQueries", preferences.getFocusNewQueries()));

  // Colors are now handled in preferences

  // Led Colors
  config->setGroup("Led Colors");
  preferences.setOpLedColor(config->readNumEntry("OperatorColor",preferences.getOpLedColor()));
  preferences.setVoiceLedColor(config->readNumEntry("VoiceColor",preferences.getVoiceLedColor()));
  preferences.setNoRightsLedColor(config->readNumEntry("NoRightsColor",preferences.getNoRightsLedColor()));

  // Sorting
  config->setGroup("Sort Nicknames");
  if(config->readNumEntry("AdminValue",-1)!=-1)
  {
    // if there is an AdminValue, read the rest, otherwise keep the defaults. This way we
    // will actually kill peoples' sorting options once while upgrading but they will get
    // the new extended modes in return
    preferences.setAdminValue(config->readNumEntry("AdminValue",preferences.getAdminValue()));
    preferences.setOwnerValue(config->readNumEntry("OwnerValue",preferences.getOwnerValue()));
    preferences.setOpValue(config->readNumEntry("OperatorValue",preferences.getOpValue()));
    preferences.setHalfopValue(config->readNumEntry("HalfopValue",preferences.getHalfopValue()));
    preferences.setVoiceValue(config->readNumEntry("VoiceValue",preferences.getVoiceValue()));
    preferences.setNoRightsValue(config->readNumEntry("NoRightsValue",preferences.getNoRightsValue()));
  }

  preferences.setSortByStatus(config->readBoolEntry("SortByStatus",preferences.getSortByStatus()));
  preferences.setSortCaseInsensitive(config->readBoolEntry("SortCaseInsensitive",preferences.getSortCaseInsensitive()));

  // Identity list
  QStringList identityList=config->groupList().grep(QRegExp("Identity [0-9]+"));
  if(identityList.count())
  {
    preferences.clearIdentityList();

    for(unsigned int index=0;index<identityList.count();index++)
    {
      Identity* newIdentity=new Identity();

      config->setGroup(identityList[index]);

      QString n=config->readEntry("Name");

      newIdentity->setName(config->readEntry("Name"));

      newIdentity->setIdent(config->readEntry("Ident"));
      newIdentity->setRealName(config->readEntry("Realname"));

      newIdentity->setNicknameList(config->readListEntry("Nicknames"));

      newIdentity->setBot(config->readEntry("Bot"));
      newIdentity->setPassword(config->readEntry("Password"));

      newIdentity->setInsertRememberLineOnAway(config->readBoolEntry("InsertRememberLineOnAway"));
      newIdentity->setShowAwayMessage(config->readBoolEntry("ShowAwayMessage"));
      newIdentity->setAwayMessage(config->readEntry("AwayMessage"));
      newIdentity->setReturnMessage(config->readEntry("ReturnMessage"));

      newIdentity->setPartReason(config->readEntry("PartReason"));
      newIdentity->setKickReason(config->readEntry("KickReason"));

      newIdentity->setCodecName(config->readEntry("Codec"));

      newIdentity->setAwayNick(config->readEntry("AwayNick"));

      preferences.addIdentity(newIdentity);

    } // endfor

  }
  else
  {
    // Default user identity for pre 0.10 preferences files
    config->setGroup("User Identity");
    preferences.setIdent(config->readEntry("Ident",preferences.getIdent()));
    preferences.setRealName(config->readEntry("Realname",preferences.getRealName()));

    QString nickList=config->readEntry("Nicknames",preferences.getNicknameList().join(","));
    preferences.setNicknameList(QStringList::split(",",nickList));

    preferences.setShowAwayMessage(config->readBoolEntry("ShowAwayMessage",preferences.getShowAwayMessage()));
    preferences.setAwayMessage(config->readEntry("AwayMessage",preferences.getAwayMessage()));
    preferences.setUnAwayMessage(config->readEntry("UnAwayMessage",preferences.getUnAwayMessage()));

    config->deleteGroup("User Identity");
  }
  // OnScreen Display
  config->setGroup("OSD");
  preferences.setOSDUsage(config->readBoolEntry("UseOSD",preferences.getOSDUsage()));
  preferences.setOSDShowOwnNick(config->readBoolEntry("ShowOwnNick",preferences.getOSDShowOwnNick()));
  preferences.setOSDShowChannel(config->readBoolEntry("ShowChannel",preferences.getOSDShowChannel()));
  preferences.setOSDShowQuery(config->readBoolEntry("ShowQuery",preferences.getOSDShowQuery()));
  preferences.setOSDShowChannelEvent(config->readBoolEntry("ShowChannelEvent",preferences.getOSDShowChannelEvent()));
  preferences.setOSDFontRaw(config->readEntry("OSDFont",preferences.getOSDFont().rawName()));
  preferences.setOSDUseCustomColors(config->readBoolEntry("OSDUseCustomColors", preferences.getOSDUseCustomColors()));
  preferences.setOSDDuration(config->readNumEntry("OSDDuration",preferences.getOSDDuration()));
  preferences.setOSDScreen(config->readNumEntry("OSDScreen",preferences.getOSDScreen()));
  preferences.setOSDDrawShadow(config->readBoolEntry("OSDDrawShadow",preferences.getOSDDrawShadow()));
  preferences.setOSDOffsetX(config->readNumEntry("OffsetX",preferences.getOSDOffsetX()));
  preferences.setOSDOffsetY(config->readNumEntry("OffsetY",preferences.getOSDOffsetY()));
  preferences.setOSDAlignment(config->readNumEntry("Alignment",preferences.getOSDAlignment()));
  // if osd object exists
  if(osd && preferences.getOSDUsage())
  {
    osd->setEnabled(true);
    osd->setFont(preferences.getOSDFont());
    osd->setDuration(preferences.getOSDDuration());
    osd->setScreen(preferences.getOSDScreen());
    osd->setShadow(preferences.getOSDDrawShadow());
    osd->setOffset(preferences.getOSDOffsetX(),preferences.getOSDOffsetY());
    osd->setAlignment((OSDWidget::Alignment)preferences.getOSDAlignment());

    if(preferences.getOSDUseCustomColors())
    {
      QString osdTextColor = config->readEntry("OSDTextColor");
      if(osdTextColor.isEmpty())
        preferences.setOSDTextColor(preferences.getOSDTextColor().name());
      else
        preferences.setOSDTextColor("#" + osdTextColor);

      osd->setTextColor(preferences.getOSDTextColor());

      QString osdBackgroundColor = config->readEntry("OSDBackgroundColor");
      if(osdBackgroundColor.isEmpty())
        preferences.setOSDBackgroundColor(preferences.getOSDBackgroundColor().name());
      else
        preferences.setOSDBackgroundColor("#" + osdBackgroundColor);

      osd->setBackgroundColor(preferences.getOSDBackgroundColor());
    }
  }

  // Server List
  config->setGroup("Server List");

  int index=0;
  // Remove all default entries if there is at least one Server in the preferences file
  if(config->hasKey("Server0")) preferences.clearServerList();
  // Read all servers
  while(config->hasKey(QString("Server%1").arg(index)))
  {
    preferences.addServer(config->readEntry(QString("Server%1").arg(index++)));
  }

  // Notify Settings and lists.  Must follow Server List.
  config->setGroup("Notify List");
  preferences.setNotifyDelay(config->readNumEntry("NotifyDelay",20));
  preferences.setUseNotify(config->readBoolEntry("UseNotify",true));
//  QString notifyList=config->readEntry("NotifyList",QString::null);
//  preferences.setNotifyList(QStringList::split(' ',notifyList));
  preferences.setOpenWatchedNicksAtStartup(config->readBoolEntry("OnStartup", preferences.getOpenWatchedNicksAtStartup()));
  index = 0;
  QMap<QString, QStringList> notifyList;
  QMap<QString, QString> notifyGroups = config->entryMap("Notify Group Lists");
  if (!notifyGroups.empty())
  {
    QMapConstIterator<QString, QString> groupItEnd = notifyGroups.constEnd();
    for (QMapConstIterator<QString, QString> groupIt = notifyGroups.constBegin();
      groupIt != groupItEnd; ++groupIt)
      notifyList[groupIt.key()] = QStringList::split(" ", groupIt.data(), false);
  }
  else
  {
    // Retrieve old Notify List.
    config->setGroup("Notify List");
    QString oldNotifyNicknames = config->readEntry("NotifyList", QString::null);
    if (!oldNotifyNicknames.isEmpty())
    {
      QStringList oldNotifyNicknameList = QStringList::split(" ", oldNotifyNicknames, false);
      // Build a list of unique server group names.
      QPtrList<ServerEntry> serverEntries = preferences.getServerList();
      QStringList groupNames;
      for(unsigned int index=0;index<serverEntries.count();index++)
      {
        QString name=serverEntries.at(index)->getGroupName();
        if (!groupNames.contains(name)) groupNames.append(name);
      }
      // Apply the old Notify List to all groups.
      for (QStringList::Iterator groupIt = groupNames.begin(); groupIt != groupNames.end(); ++groupIt)
        notifyList[*groupIt] = oldNotifyNicknameList;
    }
  }
  preferences.setNotifyList(notifyList);

  // Quick Buttons List
  config->setGroup("Button List");
  // Read all buttons and overwrite default entries
  QStringList buttonList(preferences.getButtonList());
  for(index=0;index<8;index++)
  {
    QString buttonKey(QString("Button%1").arg(index));
    if(config->hasKey(buttonKey)) buttonList[index]=config->readEntry(buttonKey);
  }
  // Put back the changed button list
  preferences.setButtonList(buttonList);

  // Hilight List
  config->setGroup("Hilight List");
  preferences.setHilightSoundEnabled(config->readBoolEntry("HilightSoundsEnabled",
    preferences.getHilightSoundEnabled()));
  preferences.setHilightNick(config->readBoolEntry("HilightNick",preferences.getHilightNick()));
  QString hilight=config->readEntry("HilightNickColor");
  if(hilight.isEmpty())
    preferences.setHilightNickColor(preferences.getHilightNickColor().name());
  else
    preferences.setHilightNickColor("#"+hilight);

  preferences.setHilightOwnLines(config->readBoolEntry("HilightOwnLines",preferences.getHilightOwnLines()));
  hilight=config->readEntry("HilightOwnLinesColor");
  if(hilight.isEmpty())
    preferences.setHilightOwnLinesColor(preferences.getHilightOwnLinesColor().name());
  else
    preferences.setHilightOwnLinesColor("#"+hilight);

  if(config->hasKey("Hilight")) { // Stay compatible with versions < 0.14
    hilight=config->readEntry("Hilight");
    QStringList hiList=QStringList::split(' ',hilight);

    unsigned int hiIndex;
    for(hiIndex=0;hiIndex<hiList.count();hiIndex+=2)
    {
      preferences.addHilight(hiList[hiIndex],false,"#"+hiList[hiIndex+1],QString::null,QString::null);
    }

    config->deleteEntry("Hilight");
  } else {
    int i = 0;

    while(config->hasGroup(QString("Highlight%1").arg(i))) {
      config->setGroup(QString("Highlight%1").arg(i));
      preferences.addHilight(config->readEntry("Pattern"),
                             config->readBoolEntry("RegExp"),
                             config->readColorEntry("Color"),
                             config->readPathEntry("Sound"),
                             config->readEntry("AutoText"));
      i++;
    }
  }

  // Ignore List
  config->setGroup("Ignore List");
  // Remove all default entries if there is at least one Ignore in the preferences file
  if(config->hasKey("Ignore0")) preferences.clearIgnoreList();
  // Read all ignores
  index=0;
  while(config->hasKey(QString("Ignore%1").arg(index)))
  {
    preferences.addIgnore(config->readEntry(QString("Ignore%1").arg(index++)));
  }

  // Aliases
  config->setGroup("Aliases");
  QStringList newList=config->readListEntry("AliasList");
  if(!newList.isEmpty()) preferences.setAliasList(newList);

  // Nick Completion
  config->setGroup("Nick Completion");
  preferences.setNickCompletionMode(config->readNumEntry("Mode", preferences.getNickCompletionMode()));
  preferences.setNickCompleteSuffixStart(config->readEntry("SuffixStart",preferences.getNickCompleteSuffixStart()));
  preferences.setNickCompleteSuffixMiddle(config->readEntry("SuffixMiddle",preferences.getNickCompleteSuffixMiddle()));
  preferences.setPrefixCharacter(config->readEntry("PrefixCharacter",preferences.getPrefixCharacter()));

  // DCC Settings
  config->setGroup("DCC Settings");
  preferences.setDccBufferSize(config->readNumEntry("BufferSize",preferences.getDccBufferSize()));
  preferences.setDccMethodToGetOwnIp(config->readNumEntry("MethodToGetOwnIp",preferences.getDccMethodToGetOwnIp()));
  preferences.setDccSpecificOwnIp(config->readEntry("SpecificOwnIp",preferences.getDccSpecificOwnIp()));
  preferences.setDccSpecificSendPorts(config->readBoolEntry("SpecificSendPorts",preferences.getDccSpecificSendPorts()));
  preferences.setDccSendPortsFirst(config->readNumEntry("SendPortsFirst",preferences.getDccSendPortsFirst()));
  preferences.setDccSendPortsLast(config->readNumEntry("SendPortsLast",preferences.getDccSendPortsLast()));
  preferences.setDccSpecificChatPorts(config->readBoolEntry("SpecificChatPorts",preferences.getDccSpecificChatPorts()));
  preferences.setDccChatPortsFirst(config->readNumEntry("ChatPortsFirst",preferences.getDccChatPortsFirst()));
  preferences.setDccChatPortsLast(config->readNumEntry("ChatPortsLast",preferences.getDccChatPortsLast()));
  preferences.setDccAddPartner(config->readBoolEntry("AddPartner",preferences.getDccAddPartner()));
  preferences.setDccCreateFolder(config->readBoolEntry("CreateFolder",preferences.getDccCreateFolder()));
  preferences.setDccAutoGet(config->readBoolEntry("AutoGet",preferences.getDccAutoGet()));
  preferences.setDccAutoResume(config->readBoolEntry("AutoResume",preferences.getDccAutoResume()));
  preferences.setDccFastSend(config->readBoolEntry("FastSend",preferences.getDccFastSend()));
  preferences.setDccSendTimeout(config->readNumEntry("SendTimeout",preferences.getDccSendTimeout()));

  // Path settings
  config->setGroup("Path Settings");
  preferences.setLogPath(config->readPathEntry("LogfilePath",preferences.getLogPath()));
  preferences.setDccPath(config->readPathEntry("DccPath",preferences.getDccPath()));

  // Miscellaneous Flags
  config->setGroup("Flags");

  preferences.setLog(config->readBoolEntry("Log",preferences.getLog()));
  preferences.setLowerLog(config->readBoolEntry("LowerLog",preferences.getLowerLog()));
  preferences.setLogFollowsNick(config->readBoolEntry("LogFollowsNick",preferences.getLogFollowsNick()));

  preferences.setTabPlacement(static_cast<Preferences::TabPlacement>(config->readNumEntry("TabPlacement",static_cast<int>(preferences.getTabPlacement()))));
  preferences.setBlinkingTabs(config->readBoolEntry("BlinkingTabs",preferences.getBlinkingTabs()));
  preferences.setBringToFront(config->readBoolEntry("BringToFront",preferences.getBringToFront()));

  preferences.setHideUnimportantEvents(config->readBoolEntry("HideUnimportantEvents",preferences.getHideUnimportantEvents()));
  preferences.setDisableExpansion(config->readBoolEntry("DisableExpansion", preferences.getDisableExpansion()));

  preferences.setAutoReconnect(config->readBoolEntry("AutoReconnect",preferences.getAutoReconnect()));
  preferences.setAutoRejoin(config->readBoolEntry("AutoRejoin",preferences.getAutoRejoin()));
  preferences.setAutojoinOnInvite(config->readBoolEntry("AutojoinOnInvite",preferences.getAutojoinOnInvite()));

  preferences.setFixedMOTD(config->readBoolEntry("FixedMOTD")); // Default is false
  preferences.setShowServerList(config->readBoolEntry("ShowServerList",preferences.getShowServerList()));

  preferences.setColorInputFields(config->readBoolEntry("InputFieldsBackgroundColor",preferences.getColorInputFields()));

  // Web Browser
  config->setGroup("Web Browser Settings");
  preferences.setWebBrowserUseKdeDefault(config->readBoolEntry("UseKdeDefault",preferences.getWebBrowserUseKdeDefault()));
  preferences.setWebBrowserCmd(config->readEntry("WebBrowserCmd",preferences.getWebBrowserCmd()));

  // Channel Encodings
  QMap<QString,QString> channelEncodingsEntry=config->entryMap("Channel Encodings");
  QRegExp re("^(.+) ([^\\s]+)$");
  QStringList channelEncodingsEntryKeys=channelEncodingsEntry.keys();
  for(unsigned int i=0; i<channelEncodingsEntry.count(); ++i)
    if(re.search(channelEncodingsEntryKeys[i]) > -1)
      preferences.setChannelEncoding(re.cap(1),re.cap(2),channelEncodingsEntry[channelEncodingsEntryKeys[i]]);
}

void KonversationApplication::saveOptions(bool updateGUI)
{
  KConfig* config=kapp->config();

  config->setGroup("General Options");

  config->writeEntry("CommandChar",preferences.getCommandChar());

  config->writeEntry("NicksOnlineGeometry",preferences.getNicksOnlineSize());
  config->writeEntry("NicknameGeometry",preferences.getNicknameSize());
  config->writeEntry("LogfileReaderGeometry",preferences.getLogfileReaderSize());
  config->writeEntry("MultilineEditGeometry",preferences.getMultilineEditSize());

  config->writeEntry("LogfileBufferSize",preferences.getLogfileBufferSize());
  config->writeEntry("ScrollbackMax",preferences.getScrollbackMax());

  config->writeEntry("ShowTrayIcon",preferences.getShowTrayIcon());
  config->writeEntry("TrayNotify",preferences.getTrayNotify());

  config->writeEntry("ShowBackgroundImage",preferences.getShowBackgroundImage());

  if ( preferences.getChannelDoubleClickAction() == "/QUERY %u%n" )
      config->deleteEntry("ChannelDoubleClickAction");
  else
      config->writeEntry("ChannelDoubleClickAction",preferences.getChannelDoubleClickAction());
  config->writeEntry("NotifyDoubleClickAction",preferences.getNotifyDoubleClickAction());

  config->writeEntry("Beep",preferences.getBeep());
  config->writeEntry("RawLog",preferences.getRawLog());

  config->writeEntry("VersionReply",preferences.getVersionReply());

  config->writeEntry("MaximumLag",preferences.getMaximumLagTime());

  config->writeEntry("RedirectServerAndAppMsgToStatusPane", preferences.getRedirectToStatusPane());

  //User interface
  config->writeEntry("ServerWindowMenuBarStatus", preferences.getShowMenuBar());

  config->setGroup("Appearance");

  config->writeEntry("TextFont",preferences.getTextFont().toString());
  config->writeEntry("ListFont",preferences.getListFont().toString());
  config->writeEntry("Timestamping",preferences.getTimestamping());
  config->writeEntry("ShowDate", preferences.getShowDate());
  config->writeEntry("TimestampFormat",preferences.getTimestampFormat());
  config->writeEntry("ShowQuickButtons",preferences.getShowQuickButtons());
  config->writeEntry("ShowModeButtons",preferences.getShowModeButtons());
  config->writeEntry("CloseButtonsOnTabs",preferences.getCloseButtonsOnTabs());
  config->writeEntry("CloseButtonsAlignRight",preferences.getCloseButtonsAlignRight());

  config->writeEntry("AutoUserhost",preferences.getAutoUserhost());

  config->writeEntry("UseSpacing",preferences.getUseSpacing());
  config->writeEntry("Spacing",preferences.getSpacing());
  config->writeEntry("Margin",preferences.getMargin());

  config->writeEntry("UseParagraphSpacing",preferences.getUseParagraphSpacing());
  config->writeEntry("ParagraphSpacing",preferences.getParagraphSpacing());

  QString sizesString(QString::number(preferences.getChannelSplitter()[0])+","+QString::number(preferences.getChannelSplitter()[1]));
  config->writeEntry("ChannelSplitter",sizesString);
  config->writeEntry("BackgroundImage",preferences.getBackgroundImageName());
  config->writeEntry("IRCColors", preferences.getIRCColorList());
  config->writeEntry("ShowTabBarCloseButton", preferences.getShowTabBarCloseButton());

  config->writeEntry("ShowTopic", preferences.getShowTopic());

  config->writeEntry("ShowRememberLineInAllWindows", preferences.getShowRememberLineInAllWindows());
  config->writeEntry("FocusNewQueries", preferences.getFocusNewQueries());
  config->writeEntry("FilterColorCodes", preferences.getFilterColors());

  // Colors are now handled in preferences

  config->setGroup("Sort Nicknames");
  config->writeEntry("AdminValue",preferences.getAdminValue());
  config->writeEntry("OwnerValue",preferences.getOwnerValue());
  config->writeEntry("OperatorValue",preferences.getOpValue());
  config->writeEntry("HalfopValue",preferences.getHalfopValue());
  config->writeEntry("VoiceValue",preferences.getVoiceValue());
  config->writeEntry("NoRightsValue",preferences.getNoRightsValue());
  config->writeEntry("SortByStatus",preferences.getSortByStatus());
  config->writeEntry("SortCaseInsensitive",preferences.getSortCaseInsensitive());

  config->setGroup("Led Colors");
  config->writeEntry("OperatorColor", preferences.getOpLedColor());
  config->writeEntry("VoiceColor", preferences.getVoiceLedColor());
  config->writeEntry("NoRightsColor", preferences.getNoRightsLedColor());


  // Clean up identity list
  QStringList identities=config->groupList().grep(QRegExp("Identity [0-9]+"));
  if(identities.count())
  {
    // remove old identity list from preferences file to keep numbering under control
    for(unsigned int index=0;index<identities.count();index++)
      config->deleteGroup(identities[index]);
  }

  QPtrList<Identity> identityList=preferences.getIdentityList();
  for(unsigned int index=0;index<identityList.count();index++)
  {
    Identity* identity=identityList.at(index);
    config->setGroup(QString("Identity %1").arg(index));

    config->writeEntry("Name",identity->getName());
    config->writeEntry("Ident",identity->getIdent());
    config->writeEntry("Realname",identity->getRealName());
    config->writeEntry("Nicknames",identity->getNicknameList());
    config->writeEntry("Bot",identity->getBot());
    config->writeEntry("Password",identity->getPassword());
    config->writeEntry("InsertRememberLineOnAway", identity->getInsertRememberLineOnAway());
    config->writeEntry("ShowAwayMessage",identity->getShowAwayMessage());
    config->writeEntry("AwayMessage",identity->getAwayMessage());
    config->writeEntry("ReturnMessage",identity->getReturnMessage());
    config->writeEntry("PartReason",identity->getPartReason());
    config->writeEntry("KickReason",identity->getKickReason());
    config->writeEntry("Codec",identity->getCodecName());
    config->writeEntry("AwayNick", identity->getAwayNick());
  } // endfor

  config->setGroup("Notify List");

  config->writeEntry("NotifyDelay",preferences.getNotifyDelay());
  config->writeEntry("UseNotify",preferences.getUseNotify());
//  config->writeEntry("NotifyList",preferences.getNotifyString());
  config->writeEntry("OnStartup", preferences.getOpenWatchedNicksAtStartup());

  config->deleteGroup("Notify Group Lists");
  config->setGroup("Notify Group Lists");
  QMap<QString, QStringList> notifyList = preferences.getNotifyList();
  QMapConstIterator<QString, QStringList> groupItEnd = notifyList.constEnd();
  for (QMapConstIterator<QString, QStringList> groupIt = notifyList.constBegin();
    groupIt != groupItEnd; ++groupIt)
    config->writeEntry(groupIt.key(), groupIt.data().join(" "));

  config->deleteGroup("Server List");
  config->setGroup("Server List");

  int index=0;
  QString serverEntry=preferences.getServerByIndex(0);

  while(!serverEntry.isEmpty())
  {
    config->writeEntry(QString("Server%1").arg(index),serverEntry);
    serverEntry=preferences.getServerByIndex(++index);
  }

  config->setGroup("Button List");

  for(index=0;index<8;index++)
  {
    QStringList buttonList(preferences.getButtonList());
    config->writeEntry(QString("Button%1").arg(index),buttonList[index]);
  }

  // Write all hilight entries
  config->setGroup("Hilight List");

  config->writeEntry("HilightNick",preferences.getHilightNick());
  config->writeEntry("HilightNickColor",preferences.getHilightNickColor().name().mid(1));
  config->writeEntry("HilightOwnLines",preferences.getHilightOwnLines());
  config->writeEntry("HilightOwnLinesColor",preferences.getHilightOwnLinesColor().name().mid(1));
  config->writeEntry("HilightSoundsEnabled", preferences.getHilightSoundEnabled());

  QPtrList<Highlight> hiList=preferences.getHilightList();
  int i = 0;

  for(Highlight* hl = hiList.first(); hl; hl = hiList.next()) {
    config->setGroup(QString("Highlight%1").arg(i));
    config->writeEntry("Pattern", hl->getPattern());
    config->writeEntry("RegExp", hl->getRegExp());
    config->writeEntry("Color", hl->getColor());
    config->writePathEntry("Sound", hl->getSoundURL().prettyURL());
    config->writeEntry("AutoText", hl->getAutoText());
    i++;
  }

  // Remove unused entries...
  while(config->hasGroup(QString("Highlight%1").arg(i))) {
    config->deleteGroup(QString("Highlight%1").arg(i));
    i++;
  }

  // OnScreen Display
  config->setGroup("OSD");
  config->writeEntry("UseOSD",preferences.getOSDUsage());
  config->writeEntry("OSDUseCustomColors",preferences.getOSDUseCustomColors());
  config->writeEntry("ShowOwnNick",preferences.getOSDShowOwnNick());
  config->writeEntry("ShowChannel",preferences.getOSDShowChannel());
  config->writeEntry("ShowQuery",preferences.getOSDShowQuery());
  config->writeEntry("ShowChannelEvent",preferences.getOSDShowChannelEvent());
  config->writeEntry("OSDFont",preferences.getOSDFont().toString());
  config->writeEntry("OSDTextColor",preferences.getOSDTextColor().name().mid(1));
  config->writeEntry("OSDBackgroundColor",preferences.getOSDBackgroundColor().name().mid(1));
  config->writeEntry("OSDDuration",preferences.getOSDDuration());
  config->writeEntry("OSDScreen",preferences.getOSDScreen());
  config->writeEntry("OSDDrawShadow",preferences.getOSDDrawShadow());
  config->writeEntry("OffsetX",preferences.getOSDOffsetX());
  config->writeEntry("OffsetY",preferences.getOSDOffsetY());
  config->writeEntry("Alignment",preferences.getOSDAlignment());

  // Ignore List
  config->deleteGroup("Ignore List");
  config->setGroup("Ignore List");
  QPtrList<Ignore> ignoreList=preferences.getIgnoreList();
  Ignore* item=ignoreList.first();
  index=0;
  while(item)
  {
    config->writeEntry(QString("Ignore%1").arg(index),QString("%1,%2").arg(item->getName()).arg(item->getFlags()));
    item=ignoreList.next();
    index++;
  }

  // Aliases
  config->setGroup("Aliases");
  config->writeEntry("AliasList",preferences.getAliasList());

  // Nick Completion
  config->setGroup("Nick Completion");
  config->writeEntry("Mode", preferences.getNickCompletionMode());
  config->writeEntry("SuffixStart",preferences.getNickCompleteSuffixStart());
  config->writeEntry("SuffixMiddle",preferences.getNickCompleteSuffixMiddle());

  // DCC Settings
  config->setGroup("DCC Settings");
  config->writeEntry("AddPartner",preferences.getDccAddPartner());
  config->writeEntry("CreateFolder",preferences.getDccCreateFolder());
  config->writeEntry("BufferSize",preferences.getDccBufferSize());
  config->writeEntry("MethodToGetOwnIp",preferences.getDccMethodToGetOwnIp());
  config->writeEntry("SpecificOwnIp",preferences.getDccSpecificOwnIp());
  config->writeEntry("SpecificSendPorts",preferences.getDccSpecificSendPorts());
  config->writeEntry("SendPortsFirst",preferences.getDccSendPortsFirst());
  config->writeEntry("SendPortsLast",preferences.getDccSendPortsLast());
  config->writeEntry("SpecificChatPorts",preferences.getDccSpecificChatPorts());
  config->writeEntry("ChatPortsFirst",preferences.getDccChatPortsFirst());
  config->writeEntry("ChatPortsLast",preferences.getDccChatPortsLast());
  config->writeEntry("AutoGet",preferences.getDccAutoGet());
  config->writeEntry("AutoResume",preferences.getDccAutoResume());
  config->writeEntry("FastSend",preferences.getDccFastSend());
  config->writeEntry("SendTimeout",preferences.getDccSendTimeout());

 // Path Settings
  config->setGroup("Path Settings");
  config->writePathEntry("DccPath",preferences.getDccPath());
  config->writePathEntry("LogfilePath",preferences.getLogPath());

  // Flags
  config->setGroup("Flags");

  config->writeEntry("Log",preferences.getLog());
  config->writeEntry("LowerLog",preferences.getLowerLog());
  config->writeEntry("LogFollowsNick",preferences.getLogFollowsNick());

  config->writeEntry("TabPlacement",static_cast<int>(preferences.getTabPlacement()));
  config->writeEntry("BlinkingTabs",preferences.getBlinkingTabs());
  config->writeEntry("BringToFront",preferences.getBringToFront());

  config->writeEntry("HideUnimportantEvents",preferences.getHideUnimportantEvents());
  config->writeEntry("DisableExpansion",preferences.getDisableExpansion());

  config->writeEntry("AutoReconnect",preferences.getAutoReconnect());
  config->writeEntry("AutoRejoin",preferences.getAutoRejoin());
  config->writeEntry("AutojoinOnInvite",preferences.getAutojoinOnInvite());

  config->writeEntry("FixedMOTD",preferences.getFixedMOTD());
  config->writeEntry("ShowServerList",preferences.getShowServerList());

  config->writeEntry("InputFieldsBackgroundColor",preferences.getColorInputFields());

  // Web Browser settings
  config->setGroup("Web Browser Settings");
  config->writeEntry("UseKdeDefault", preferences.getWebBrowserUseKdeDefault());
  config->writeEntry("WebBrowserCmd", preferences.getWebBrowserCmd());

  // Channel Encodings
  config->setGroup("Channel Encodings");
  QStringList channelEncodingsServerList=preferences.getChannelEncodingsServerList();
  channelEncodingsServerList.sort();
  for(unsigned int i=0; i<channelEncodingsServerList.count(); ++i)
  {
    QStringList channelEncodingsChannelList=preferences.getChannelEncodingsChannelList(channelEncodingsServerList[i]);
    channelEncodingsChannelList.sort();
    for(unsigned int j=0; j<channelEncodingsChannelList.count(); ++j)
      if(!preferences.getChannelEncoding(channelEncodingsServerList[i],channelEncodingsChannelList[j]).isEmpty())
        config->writeEntry(channelEncodingsServerList[i]+" "+channelEncodingsChannelList[j],preferences.getChannelEncoding(channelEncodingsServerList[i],channelEncodingsChannelList[j]));
  }

  config->sync();

  emit prefsChanged();

  if(updateGUI) appearanceChanged();
}

void KonversationApplication::appearanceChanged()
{
  Server* lookServer=serverList.first();
  while(lookServer)
  {
    // TODO: updateFonts() also updates the background color and more stuff! We must finally
    // find a way to do all this with signals / slots!
    lookServer->updateFonts();
    lookServer->updateChannelQuickButtons();

    lookServer->setShowQuickButtons(preferences.getShowQuickButtons());
    lookServer->setShowModeButtons(preferences.getShowModeButtons());
    lookServer->setShowTopic(preferences.getShowTopic());

    lookServer=serverList.next();
  }

  mainWindow->updateTabPlacement();
  mainWindow->setShowTabBarCloseButton(preferences.getShowTabBarCloseButton());
}

// FIXME: use KURL maybe?
void KonversationApplication::storeUrl(const QString& who,const QString& newUrl)
{
  QString url(newUrl);
  // clean up URL to help KRun() in URL catcher interface
  if(url.startsWith("www.")) url="http://"+url;
  else if(url.startsWith("ftp.")) url="ftp://"+url;

  url=url.replace("&amp;","&");

  // check that we don't add the same URL twice
  deleteUrl(who,url);
  urlList.append(who+" "+url);
  emit catchUrl(who,url);
}

const QStringList& KonversationApplication::getUrlList()
{
  return urlList;
}

void KonversationApplication::deleteUrl(const QString& who,const QString& url)
{
  urlList.remove(who+" "+url);
}

void KonversationApplication::clearUrlList()
{
  urlList.clear();
}

void KonversationApplication::openPrefsDialog()
{
  if(prefsDialog==0)
  {
    prefsDialog=new PrefsDialog(&preferences,false);

    connect(prefsDialog,SIGNAL (connectToServer(int)),this,SLOT (connectToAnotherServer(int)) );
    connect(prefsDialog,SIGNAL (cancelClicked()),this,SLOT (closePrefsDialog()) );
    connect(prefsDialog,SIGNAL (prefsChanged()),this,SLOT (saveOptions()) );

    prefsDialog->show();
  }
  else
  {
    prefsDialog->show();
    prefsDialog->raise();
    prefsDialog->setActiveWindow();
  }
}

void KonversationApplication::openPrefsDialog(Preferences::Pages page)
{
  openPrefsDialog();
  prefsDialog->openPage(page);
}

void KonversationApplication::openQuickConnectDialog()
{
	quickConnectDialog = new QuickConnectDialog(mainWindow);
	connect(quickConnectDialog, SIGNAL(connectClicked(const QString&, const QString&, const QString&, const QString&)),
		      this, SLOT(quickConnectToServer(const QString&, const QString&, const QString&, const QString&)));
	quickConnectDialog->show();
}

void KonversationApplication::syncPrefs()
{
  kapp->config()->sync();
}

void KonversationApplication::closePrefsDialog()
{
  delete prefsDialog;
  prefsDialog=0;
}

bool KonversationApplication::emitDCOPSig(const QString &appId, const QString &objId, const QString &signal, QByteArray &data)
{
  kdDebug() << "emitDCOPSig (" << signal << ")" << endl;
  //dcopObject->emitDCOPSignal(signal, data);
  QByteArray replyData;
  QCString replyType;
  if (!KApplication::dcopClient()->call(appId.ascii(), objId.ascii(), signal.ascii() /*must have prototype*/,
                                        data, replyType, replyData)) {
    qDebug("there was some error using DCOP.");
    return true; // Keep processing filters
  } else {
    QDataStream reply(replyData, IO_ReadOnly);
    if (replyType == "bool") {
      bool result;
      reply >> result;
      return result;
    } else {
      qDebug("doIt returned an unexpected type of reply!");
      return true; // Keep processing
    }
  }
}

QPtrList<IRCEvent> KonversationApplication::retrieveHooks (EVENT_TYPE a_type)
{
  QPtrList<IRCEvent> ret_value;
  IRCEvent *e;

  for (e = dcopObject->registered_events.first(); e; e = dcopObject->registered_events.next()) {
    if (e->type == a_type) {
      ret_value.append(e);
      }
    }
  return ret_value;
}

void KonversationApplication::sendMultiServerCommand(const QString& command, const QString& parameter)
{
  for(Server* server = serverList.first(); server; server = serverList.next()) {
    server->executeMultiServerCommand(command, parameter);
  }
}

void KonversationApplication::dcopConnectToServer(const QString& url, int port)
{
  quickConnectToServer(url, QString::number(port));
}

Konversation::Sound* KonversationApplication::sound()
{
  return m_sound;
}

// Returns list of pointers to Servers.
const QPtrList<Server> KonversationApplication::getServerList() { return serverList; }

void KonversationApplication::splitNick_Server(QString nick_server, QString &ircnick, QString &serverOrGroup) {
  //kaddresbook uses the utf seperator 0xE120, so treat that as a seperator as well
  nick_server = nick_server.replace(QChar(0xE120), "@");
  ircnick = nick_server.section("@",0,0);
  serverOrGroup = nick_server.section("@",1);
}

NickInfoPtr KonversationApplication::getNickInfo(const QString &ircnick, const QString &serverOrGroup) {
  NickInfoPtr nickInfo;
  QString lserverOrGroup = serverOrGroup.lower();
  for(Server* lookServer = serverList.first(); lookServer; lookServer = serverList.next()) {
    if(lserverOrGroup.isEmpty()
       || lookServer->getServerName().lower()==lserverOrGroup
       || lookServer->getServerGroup().lower()==lserverOrGroup)
    {
      nickInfo = lookServer->getNickInfo(ircnick);
      if(nickInfo) return nickInfo; //If we found one
    }
  }
  return 0;
}

#include "konversationapplication.moc"
