/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2013 Travis McHenry <me@travisjmchenry.com>
*/

#ifndef SERVERGROUPMODEL_H
#define SERVERGROUPMODEL_H

#include "servergroupsettings.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>


class ServerGroupModel : public QAbstractListModel
{
    Q_OBJECT

    public:
        ServerGroupModel(QObject* parent = 0);
        ~ServerGroupModel();

        enum Roles {ServerGroupIdRole = Qt::UserRole, ExpandedRole, IsServerRole, NickRole};

        void setServerGroupList(Konversation::ServerGroupList serverGroups);
        void addServerGroup(int serverGroupId, Konversation::ServerGroupSettingsPtr serverGroup);
        void removeServerGroup(int serverGroupId);
        void removeServer(int serverGroupId, int index);
        void removeNotify(int serverGroupId, int index);
        bool removeNotify(int serverGroupId, const QString& nick);
        bool addNotify(int serverGroupId, const QString& nick);

        Konversation::ServerGroupSettingsPtr getServerGroupById(int serverGroupId) const;
        Konversation::ServerGroupSettingsPtr getServerGroupByIndex(int serverGroupIndex) const;
        Konversation::ServerGroupHash getServerGroupHash() const;
        Konversation::ServerGroupList getServerGroupList() const;
        int getServerGroupIndexById(int id) const;

        Qt::DropActions supportedDropActions() const;
        bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent);
        QStringList mimeTypes() const;
        QMimeData* mimeData(const QModelIndexList &indexes) const;

        int rowCount(const QModelIndex& parent = QModelIndex()) const;
        int columnCount(const QModelIndex& parent = QModelIndex()) const;

        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
        QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

        QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;
        QModelIndex parent(const QModelIndex& index) const;

        Qt::ItemFlags flags(const QModelIndex& index) const;
        bool hasChildren(const QModelIndex& index) const;
        QHash<int, QByteArray> roleNames() const;

    signals:
        void addNotifyNick(int serverGroupId, const QString& nick);
        void removeNotifyNick(int serverGroupId, const QString& nick);

    private:
        Konversation::ServerGroupHash m_serverGroupHash;
        Konversation::ServerGroupList m_serverGroupList;
        QHash<int, QString> m_channelListHash;

};

class ServerGroupFilterModel : public QSortFilterProxyModel
{
    public:

        ServerGroupFilterModel(QObject* parent = 0, int column = 0);
        ~ServerGroupFilterModel();

        int getServerGroupIndexById(int id) const;

    protected:
        bool lessThan(const QModelIndex& left, const QModelIndex& right) const;
        bool filterAcceptsColumn(int column, const QModelIndex& parent) const;
        bool filterAcceptsRow(int row, const QModelIndex& parent) const;

    private:
        int m_column;

};

#endif