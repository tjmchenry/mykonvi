/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2013 Travis McHenry <me@travisjmchenry.com>
*/

#ifndef NICKLISTMODEL_H
#define NICKLISTMODEL_H

#include "channel.h"
#include "server.h"
#include "images.h"
#include "nick2.h"
#include "ircinput.h"
#include "connectionmanager.h"

#include <QSortFilterProxyModel>

class Nick2;
class Channel;
class ConnectionManager;

typedef QHash<QString, Nick2*> NickHash;

Q_DECLARE_METATYPE(NickHash);
Q_DECLARE_METATYPE(ChannelHash*);
Q_DECLARE_METATYPE(const ChannelHash*);

class NickListModel : public QAbstractListModel
{
    Q_OBJECT

    public:
        explicit NickListModel(QObject *parent = 0);
        ~NickListModel();

        enum Roles {NickRole = Qt::UserRole, LoweredNickRole, HostmaskRole, LoweredHostmaskRole, AwayRole, ChannelPropertiesRole, ChannelsRole};

        void clear();

        void addServer(int connectionId);
        void removeServer(int connectionId);

        void insertNick(int connectionId, Nick2* item);
        void insertNicksFromNames(int connectionId, const QString& channel, const QStringList& namesList);
        void addNickToChannel(int connectionId, const QString& channel, const QString& nick);

        void removeNick(int connectionId, const QString& nick);
        void removeNickFromChannel(int connectionId, const QString& channel, const QString& nick);
        void removeAllNicksFromChannel(int connectionId, const QString& channel);

        Nick2* getNick(int connectionId, const QString& nick);

        bool isNickOnline(int connectionId, const QString& nick) const;
        bool isNotifyNick(int cId, const QString& nick) const;
        void setNewNickname(int connectionId, const QString& nick, const QString& newNick);
        void setAllChannelNicksLessActive(int connectionId, const QString& channel);

        int columnCount(const QModelIndex& parent = QModelIndex()) const;
        int rowCount(const QModelIndex& parent = QModelIndex()) const;
        QPersistentModelIndex serverIndex(int connectionId);
        QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;
        QModelIndex parent(const QModelIndex& index) const;

        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
        QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

        Qt::ItemFlags flags(const QModelIndex& index) const;
        QHash<int, QByteArray> roleNames() const;
        bool hasChildren(const QModelIndex& index) const;

        void setHostmaskColumn(bool hostmask) { m_hostmask = hostmask; }

    signals:
        void nickOnline(int sgId, int connectionId, const QString& nick);
        void nickOffline(int sgId, int connectionId, Nick2* nick);

    protected:
        QStringList getSharedChannels(int connectionId, const QString& nick);

    protected slots:
        void slotNickChanged(int cId, const QString& nick, QVector<int> columnsChanged, QVector<int> rolesChanged);

    private:
        QHash<int, QList<Nick2*> > m_nickLists;
        QHash<int, NickHash> m_nickHashes;
        QMap<int, QPersistentModelIndex> m_servers;
        int m_connectionId;
        ConnectionManager* m_connectionManager;
        bool m_hostmask;
        QString m_whatsThis;
        QTimer* m_delayedResetTimer;
        QPixmap m_icon;

        void updateMinimumRowHeight();
        int m_minimumRowHeight;
};

class ChannelNickListFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    public:
        explicit ChannelNickListFilterModel(int connectionId, Channel* channel);
        ~ChannelNickListFilterModel();

        enum Roles {ActivityRole = NickListModel::ChannelsRole + 1, /*ModesRole,*/ StatusValueRole, TimestampRole};

        QModelIndex serverIndex() const;
        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

        void insertNick(Nick2* item);
        void removeNick(const QString& nick);
        void removeAllNicks();

        Nick2* getNick(const QString& nick);

        void setAllNicksLessActive();

        NickListModel* sourceNickModel() const;

        bool nickTimestampLessThan(const QModelIndex& left, const QModelIndex& right) const;
        bool nickLessThan(const QModelIndex& left, const QModelIndex& right) const;
        bool nickHostmaskLessThan(const QModelIndex& left, const QModelIndex& right) const;
        bool nickStatusLessThan(const QModelIndex& left, const QModelIndex& right) const;
        bool nickActivityLessThan(const QModelIndex& left, const QModelIndex& right) const;

    public slots:
        void nickCompletion(IRCInput* inputBar);
        void endNickCompletion();

    protected:
        QVariant getProperty(const QModelIndex& sourceIndex, const QString& property) const;
        bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const;
        bool lessThan(const QModelIndex& srcLeft, const QModelIndex& srcRight) const;

        QString completeNick(const QString& pattern, bool& complete, QStringList& found, bool skipNonAlfaNum, bool caseSensitive);

    private:
        Channel* m_channel;
        QString m_channelName;
        int m_connectionId;
        int m_completionPosition;
};

#endif