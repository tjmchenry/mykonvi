/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2004, 2007 Peter Simonsson <psn@linux.se>
  Copyright (C) 2006-2008 Eike Hein <hein@kde.org>
*/

#ifndef KONVERSATIONSERVERGROUPSETTINGS_H
#define KONVERSATIONSERVERGROUPSETTINGS_H

#include "serversettings.h"
#include "identity.h"


#include <ksharedptr.h>


namespace Konversation
{

    class ChannelSettings
    {
        public:
            ChannelSettings();
            ChannelSettings(const ChannelSettings& settings);
            explicit ChannelSettings(const QString& name);
            ChannelSettings(const QString& name, const QString& password);
            ChannelSettings(const QString& name, const QString& password, bool enableNotifications);
            ~ChannelSettings();

            void setName(const QString& name) { m_name = name; }
            QString name() const { return m_name; }

            void setPassword(const QString& password) { m_password = password; }
            QString password() const { return m_password; }

            void setNotificationsEnabled(bool enable) { m_enableNotifications = enable; }
            bool enableNotifications() const { return m_enableNotifications; }

            bool operator==(const ChannelSettings& channel) const;

        private:
            QString m_name;
            QString m_password;

            bool m_enableNotifications;
    };

    class ServerGroupSettings;
    typedef KSharedPtr<ServerGroupSettings> ServerGroupSettingsPtr;
    typedef QHash<int,ServerGroupSettingsPtr> ServerGroupHash;
    typedef QList<ServerGroupSettingsPtr> ServerGroupList;
    typedef QList<ServerSettings> ServerList;
    typedef QList<ChannelSettings> ChannelList;

    class ServerGroupSettings : public KShared
    {
        public:
            explicit ServerGroupSettings();
            explicit ServerGroupSettings(int id);
            explicit ServerGroupSettings(const ServerGroupSettings& settings);
            explicit ServerGroupSettings(const QString& name);
            ~ServerGroupSettings();

            ServerGroupSettings& operator=(const ServerGroupSettings& settings);

            void setName(const QString& name) { m_name = name; }
            QString name() const { return m_name; }

            void setServerList(const ServerList& list);
            void addServer(const ServerSettings& settings) { m_serverList.append(settings); }
            void insertServer(int index, const ServerSettings& settings) { m_serverList.insert(index, settings); }
            void moveServer(int from, int to) { m_serverList.move(from, to); }
            void removeServer(const ServerSettings& settings);
            void removeServerByIndex(int index);
            ServerList serverList() const { return m_serverList; }
            ServerSettings serverByIndex(int index) const;

            void setNotifyList(const QStringList& list);
            void addNotify(const QString& nick) { m_notifyList.append(nick); }
            void insertNotify(int index, const QString& nick) { m_notifyList.insert(index, nick); }
            void moveNotify(int from, int to) { m_notifyList.move(from, to); }
            void removeNotify(const QString& nick);
            void removeNotifyByIndex(int index);
            bool isNotify(const QString& nick) const { return m_notifyList.contains(nick); }
            QStringList notifyList() const { return m_notifyList; }
            QString notifyByIndex(int index) const;

            void setIdentityId(int identityId) { m_identityId = identityId; }
            int identityId() const { return m_identityId; }
            IdentityPtr identity() const;

            void setChannelList(const ChannelList& list);
            void addChannel(const ChannelSettings& channel) { m_channelList.append(channel); }
            void addChannel(const ChannelSettings& channel, const ChannelSettings& before);
            //FIXME not sure if these will work, do the tabs (or anything) rely on indexes staying the same?
            void insertChannel(int index, const ChannelSettings& channel) { m_channelList.insert(index, channel); }
            void moveChannel(int from, int to) { m_channelList.move(from, to); }
            void removeChannel(const ChannelSettings& channel);
            ChannelList channelList() const { return m_channelList; }
            ChannelSettings channelByIndex(int index) const;

            void setConnectCommands(const QString& commands) { m_connectCommands = commands; }
            QString connectCommands() const { return m_connectCommands; }

            void setAutoConnectEnabled(bool enabled) { m_autoConnect = enabled; }
            bool autoConnectEnabled() const { return m_autoConnect; }

            int id() const { return m_id; }

            void clearChannelHistory();
            void setChannelHistory(const ChannelList& list) { m_channelHistory = list; }
            void appendChannelHistory(const ChannelSettings& channel);
            ChannelList channelHistory() const { return m_channelHistory; }
            ChannelSettings channelByNameFromHistory(const QString& channelName);

            void setNotificationsEnabled(bool enable) { m_enableNotifications = enable; }
            bool enableNotifications() const { return m_enableNotifications; }

            void setExpanded(bool enable) { m_expanded = enable; }
            bool expanded() const { return m_expanded; }

        private:
            static int s_availableId;
            QString m_name;
            ServerList m_serverList;
            QStringList m_notifyList;
            int m_identityId;
            ChannelList m_channelList;
            ChannelList m_channelHistory;
            QString m_connectCommands;
            bool m_autoConnect;
            QString m_group;
            int m_id;
            bool m_enableNotifications;
            bool m_expanded;
    };

}
#endif
