/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  preferences.h  -  description
  begin:     Tue Feb 5 2002
  copyright: (C) 2002 by Dario Abatianni
  email:     eisfuchs@tigress.com

  $Id$
*/

#include <qobject.h>
#include <qlist.h>
#include <qsize.h>
#include <qstringlist.h>

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include "serverentry.h"
#include "ignore.h"

/*
  @author Dario Abatianni
*/

class Preferences : public QObject
{
  Q_OBJECT

  public:
    Preferences();
    ~Preferences();

    int serverWindowToolBarPos;
    int serverWindowToolBarStatus;
    int serverWindowToolBarIconText;
    int serverWindowToolBarIconSize;
    bool serverWindowStatusBarStatus;

    int addServer(const QString& serverString);
    void removeServer(int id);
    QString getServerByIndex(unsigned int);
    QString getServerById(int);
    ServerEntry* getServerEntryById(int id);
    int getServerIdByIndex(unsigned int);
    void clearServerList();
    void changeServerProperty(int id,int property,const QString& value);
    void updateServer(int id,const QString& newDefinition);

    void setLog(bool state);
    bool getLog();

    void setBlinkingTabs(bool blink);
    bool getBlinkingTabs();

    /* Geometry functions */
    QSize getServerWindowSize();
    QSize& getHilightSize();
    QSize& getButtonsSize();
    QSize& getIgnoreSize();
    QSize& getNicknameSize();
    void setServerWindowSize(QSize newSize);
    void setHilightSize(QSize newSize);
    void setButtonsSize(QSize newSize);
    void setIgnoreSize(QSize newSize);
    void setNicknameSize(QSize newSize);

    int getNotifyDelay();
    void setNotifyDelay(int delay);
    bool getUseNotify();
    void setUseNotify(bool use);
    QString getNotifyList();
    void setNotifyList(QStringList newList);

    QStringList& getHilightList();
    void setHilightList(QStringList& newList);
    void addHilight(QString& newHilight);
    QString getHilightColor();
    void setHilightColor(const QString& color);

    QStringList getButtonList();
    void setButtonList(QStringList newList);

    void addIgnore(QString newIgnore);
    void clearIgnoreList();
    QPtrList<Ignore> getIgnoreList();
    void setIgnoreList(QPtrList<Ignore> newList);

    QString getPartReason();
    void setPartReason(QString newReason);

    QString getKickReason();
    void setKickReason(QString newReason);

    QString getNickname(int index);
    QStringList getNicknameList();
    void setNickname(int index,QString newName);
    void setNicknameList(QStringList newList);

    QString ident;
    QString realname;
    QString logPath;

  signals:
    void requestServerConnection(int number);
    void requestSaveOptions();

  protected:
    bool log;
    bool blinkingTabs; /* Do we want the LEDs on the tabs to blink? */

    int notifyDelay;
    bool useNotify;
    QStringList notifyList;

    /* Geometries */
    QSize serverWindowSize;
    QSize buttonsSize;
    QSize hilightSize;
    QSize ignoreSize;
    QSize nicknameSize;

    QList<ServerEntry> serverList;
    QStringList hilightList;
    QString hilightColor;

    QStringList buttonList;

    QPtrList<Ignore> ignoreList;

    QStringList nicknameList;

    QString partReason;
    QString kickReason;
};

#endif
