/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2008 Eike Hein <hein@kde.org>
*/

#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include "server.h"
#include "identity.h"
#include "nicksonline.h"

#include <QObject>
#include <QSet>


class ConnectionSettings;
class NickListModel;
class NicksOnlineFilterModel;

class ConnectionManager : public QObject
{
    Q_OBJECT

    public:
        explicit ConnectionManager(QObject* parent = 0);
        ~ConnectionManager();

        uint connectionCount() const { return m_connectionList.count(); }

        QList<Server*> getServerList() const;

        enum NameMatchFlags
        {
            MatchByName,
            MatchByIdThenName
        };

        Server* getServerByConnectionId(int connectionId);
        Server* getServerByName(const QString& name, NameMatchFlags flags = MatchByName);

        NickListModel* getNickListModel();

        NicksOnlineFilterModel* getNicksOnlineFilterModel();

        QMultiHash<int, int> getConnectedServerGroups() { return m_connectedServerGroups; }

    public slots:
        void connectTo(Konversation::ConnectionFlag flag,
                       const QString& target,
                       const QString& port = "",
                       const QString& password = "",
                       const QString& nick = "",
                       const QString& channel = "",
                       bool useSSL = false);

        void connectTo(Konversation::ConnectionFlag flag, int serverGroupId);
        void connectTo(Konversation::ConnectionFlag flag, const QList<KUrl>& list);
        void connectTo(Konversation::ConnectionFlag flag, ConnectionSettings settings);

        void quitServers();
        void reconnectServers();

        void involuntaryQuitServers();
        void reconnectInvoluntary();


    signals:
        void connectionListChanged();

        void connectionChangedState(Server* server, Konversation::ConnectionState state);

        void connectionChangedAwayState(bool away);

        void connectedServerGroupsChanged(int sgId, int cId);

        void requestReconnect(Server* server);

        void identityOnline(int identityId);
        void identityOffline(int identityId);

        void closeServerList();


    private slots:
        void delistConnection(int connectionId);

        void handleConnectionStateChange(Server* server, Konversation::ConnectionState state);

        void handleReconnect(Server* server);


    private:
        void enlistConnection(int connectionId, Server* server);

        void decodeIrcUrl(const QString& url, ConnectionSettings& settings);

        void decodeAddress(const QString& address,
                           ConnectionSettings& settings,
                           bool checkIfServerGroup = true);

        bool reuseExistingConnection(ConnectionSettings& settings, bool interactive);
        bool validateIdentity(IdentityPtr identity, bool interactive = true);

        QMap<int, Server*> m_connectionList;
        QMultiHash<int, int> m_connectedServerGroups;
        NicksOnlineFilterModel* m_nicksOnlineModel;
        NickListModel* m_nickListModel;
        QSet<uint> m_activeIdentities;
        bool m_overrideAutoReconnect;

        enum ConnectionDupe { SameServer, SameServerGroup };
};

#endif
