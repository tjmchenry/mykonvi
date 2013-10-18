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

#include <QSortFilterProxyModel>

class Nick2;
class Channel;

enum Roles {NickRole = Qt::UserRole, HostmaskRole};

class NickListModel : public QAbstractListModel
{
    Q_OBJECT

    public:
        explicit NickListModel(Server* server);
        ~NickListModel();

        void clear();
        void insertNick(Nick2* item);
        void fastInsertNick(Nick2* item);
        void addNickToChannel(const QString& nick, const QString& channel);

        void removeNick(const QString& nick);
        void removeNickFromChannel(const QString& nick, const QString& channel);
        void removeAllNicksFromChannel(const QString& channel);

        uint getHashPosition(const QString& nick) const;

        int columnCount(const QModelIndex& parent = QModelIndex()) const;
        int rowCount(const QModelIndex& parent = QModelIndex()) const;
        QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;

        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
        QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

        QHash<int, QByteArray> roleNames() const;

        bool isNickOnline(const QString& nick) const;
        bool isNickIdentified(const QString& nick) const;
        QStringList getNickChannels(const QString& nick) const;
        bool isNickInChannel(const QString& nick, const QString& channel) const;
        bool isNickAnyTypeOfOp(const QString& nick, const QString& channel) const;
        QString getNickHostmask(const QString& nick) const;
        void setNickHostmask(const QString& nick, const QString& hostmask);
        void setNewNickname(const QString& nick, const QString& newNick);
        uint getNickActivity(const QString& nick, const QString& channel) const;
        void setNickMoreActive(const QString& nick, const QString& channel);
        void setAllChannelNicksMoreActive(const QString& channel);
        uint getNickTimestamp(const QString& nick, const QString& channel) const;
        uint getNickStatusValue(const QString& nick, const QString& channel) const;

        void setNickMode(const QString& nick, const QString& channel, unsigned int mode);
        void setNickMode(const QString& nick, const QString& channel, char mode, bool state);
        void setNickAway(const QString& nick, bool away);

        void setHostmaskColumn(bool hostmask) { m_hostmask = hostmask; }

    public slots:
        void slotReset();


    private:
        QHash<QString, Nick2*> m_nickHash;
        Server* m_server;
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
        explicit ChannelNickListFilterModel(Channel* channel);
        ~ChannelNickListFilterModel();

        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

        bool isNickInChannel(const QString& nick) const;

        void insertNick(Nick2* item);
        void removeNick(const QString& nick);
        void removeAllNicks();

        void setAllNicksLessActive();

        NickListModel* sourceNickModel() const;

        bool nickTimestampLessThan(const Nick2* left, const Nick2* right) const;
        bool nickLessThan(const Nick2* left, const Nick2* right) const;
        bool nickHostmaskLessThan(const Nick2* left, const Nick2* right) const;
        bool nickStatusLessThan(const Nick2* left, const Nick2* right) const;
        bool nickActivityLessThan(const Nick2* left, const Nick2* right) const;

        bool isNickAnyTypeOfOp(const QString& nick) const;

    public slots:
        void nickCompletion(IRCInput* inputBar);
        void endNickCompletion();

    protected:
        bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const;
        bool lessThan(const QModelIndex& left, const QModelIndex& right) const;

        QString completeNick(const QString& pattern, bool& complete, QStringList& found, bool skipNonAlfaNum, bool caseSensitive);

    private:
        Channel* m_channel;
        QString m_channelName;
        int m_completionPosition;
};

#endif