/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2004 Peter Simonsson <psn@linux.se>
  Copyright (C) 2006-2008 Eike Hein <hein@kde.org>
*/

#ifndef INPUTFILTER_H
#define INPUTFILTER_H

#include "ignore.h"

#include <QObject>
#include <QStringList>
#include <QMap>

class Server;
class Query;
class NicksOnlineFilterModel;
class NickListModel;
class QDateTime;

class InputFilter : public QObject
{
    Q_OBJECT

    public:
        InputFilter();
        ~InputFilter();

        void setServer(Server* newServer);
        void parseLine(const QString &line);

        void reset();                             // reset AutomaticRequest, WhoRequestList

        // use this when the client does automatics, like userhost for finding hostmasks
        void setAutomaticRequest(const QString& command, const QString& name, bool yes);
        int getAutomaticRequest(const QString& command, const QString& name);
        void addWhoRequest(const QString& name);  // called from Server::send()
                                                  // to avoid duplicate requests
        bool isWhoRequestUnderProcess(const QString& name);
        void setLagMeasuring(bool yes);
        bool getLagMeasuring();

    signals:
        void welcome(const QString& ownHost);

        void serverStatusMessage(const QString& type, const QString& message);
        void serverFrontmostMessage(const QString& type, const QString& message);
        void isonResponse(int sgId, int cId, const QString &nicksOnline);
        void watchedNicksOnline(int sgId, int cId, const QStringList& onlineNicks);
        void watchedNicksOffline(int sId, int cId, const QStringList& offlineNicks);
        void watchedNicksList(int sgId, int cId, int type, const QStringList& watchedNicks);
        void endOfWatchedNicksList(int sgId, int cId, int type);

        // Bool is used to indicate if we can be certain if this is the last RPL_ISUPPORT line coming in
        // meaning we could be sure that we have gathered all properties.
        void endOfISupport(int cId, bool last);

        void watchedNicksListFull(int sgId, int cid, int type, int limit, const QStringList& unWatchedNicks);
                                                  // will be connected to Server::startReverseDccSendTransfer()
        void startReverseDccSendTransfer(const QString &sourceNick, const QStringList &dccArgument);
                                                  // will be connected to Server::startReverseDccChat()
        void startReverseDccChat(const QString &sourceNick, const QStringList &dccArgument);
                                                  // will be connected to Server::addDccGet()
        void addDccGet(const QString &sourceNick, const QStringList &dccArgument);
                                                  // will be connected to Server::resumeDccGetTransfer()
        void resumeDccGetTransfer(const QString &sourceNick, const QStringList &dccArgument);
                                                  // will be connected to Server::resumeDccSendTransfer()
        void resumeDccSendTransfer(const QString &sourceNick, const QStringList &dccArgument);
                                                  // will be connected to Server::rejectDccSendTransfer()
        void rejectDccSendTransfer(const QString &sourceNick, const QStringList &dccArgument);
                                                  // will be connected to Server::rejectDccChat()
        void rejectDccChat(const QString &sourceNick);
                                                  // will be connected to Server::userhost()
        void userhost(const QString& nick,const QString& hostmask,bool away,bool ircOp);
                                                  // will be connected to Server::setTopicAuthor()
        void topicAuthor(const QString& channel, const QString& author, QDateTime t);
        void endOfWho(const QString& target);
        void endOfWhois(int cId, const QString& nick);
        void endOfNames(const QString& target);
        void addChannelListPanel();
        void addToChannelList(const QString& channel,int users,const QString& topic);
        void endOfChannelList();

        void invitation(const QString& nick,const QString& channel);

        void addDccChat(const QString& nick,const QStringList& arguments);

    protected:
        void parseClientCommand(const QString &prefix, const QString &command, QStringList &parameterList);
        void parseServerCommand(const QString &prefix, const QString &command, QStringList &parameterList);
        void parseModes(const QString &sourceNick, const QStringList &parameterList);
        void parsePrivMsg(const QString& prefix, QStringList& parameterList);

        bool isAChannel(const QString &check);
        bool isIgnore(const QString &pattern, Ignore::Type type);

        Server* m_server;
                                                  // automaticRequest[command][channel or nick]=count
        QMap< QString, QMap< QString, int > > m_automaticRequest;
        QStringList m_whoRequestList;
        bool m_lagMeasuring;

        /// Used when handling MOTD
        bool m_connecting;

    private:
        NickListModel* m_nickListModel;
        NicksOnlineFilterModel* m_nicksOnlineModel;
        int m_connectionId;
        int m_serverGroupId;

};
#endif
