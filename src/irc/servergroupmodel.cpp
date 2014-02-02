/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2013 Travis McHenry <me@travisjmchenry.com>
*/

#include "servergroupmodel.h"

#include <QMimeData>

#include <KLocale>
#include <KDebug>

ServerGroupModel::ServerGroupModel(QObject* parent) : QAbstractListModel(parent)
{
}

ServerGroupModel::~ServerGroupModel()
{
}

void ServerGroupModel::setServerGroupList(Konversation::ServerGroupList serverGroups)
{
    m_serverGroupHash.clear();
    m_serverGroupList.clear();

    Konversation::ServerGroupList::const_iterator i;

    for(i = serverGroups.constBegin(); i != serverGroups.constEnd(); ++i)
    {
        addServerGroup((*i)->id(), (*i));
    }
}

Konversation::ServerGroupSettingsPtr ServerGroupModel::getServerGroupById(int serverGroupId) const
{
    if(m_serverGroupHash.contains(serverGroupId))
        return m_serverGroupHash[serverGroupId];

    kDebug() << "SGID: " << serverGroupId << " not found.";
    QString ids;

    QList<int>::const_iterator i;

    for (i = m_serverGroupHash.keys().constBegin(); i != m_serverGroupHash.keys().constEnd(); ++i)
        ids += " " + QString((*i));

    kDebug() << "IDs:" << ids;

    return Konversation::ServerGroupSettingsPtr();
}

Konversation::ServerGroupSettingsPtr ServerGroupModel::getServerGroupByIndex(int serverGroupIndex) const
{
    if(m_serverGroupList.count() > serverGroupIndex)
        return m_serverGroupList[serverGroupIndex];

    kDebug() << "Servergroup not found.";

    return Konversation::ServerGroupSettingsPtr();
}

Konversation::ServerGroupHash ServerGroupModel::getServerGroupHash() const
{
    return m_serverGroupHash;
}

Konversation::ServerGroupList ServerGroupModel::getServerGroupList() const
{
    return m_serverGroupList;
}

//Add is a bit of a misnomer, this will also replace existing servergroups
void ServerGroupModel::addServerGroup(int serverGroupId, Konversation::ServerGroupSettingsPtr serverGroup)
{
    if (serverGroup && serverGroupId >= 0)
    {
        if (m_serverGroupHash.contains(serverGroupId))
            m_serverGroupList.replace(m_serverGroupList.indexOf(m_serverGroupHash[serverGroupId]), serverGroup);
        else
            m_serverGroupList.append(serverGroup);

        m_serverGroupHash[serverGroupId] = serverGroup;

        QString channels;
        Konversation::ChannelList channelList = serverGroup->channelList();
        Konversation::ChannelList::const_iterator i;

        for (i = channelList.constBegin(); i != channelList.constEnd(); ++i)
        {
            if (i != channelList.constBegin())
                channels += ", ";

            channels += (*i).name();
        }

        m_channelListHash[serverGroupId] = channels;
    }
    //TODO send a signal to views that care that the model has changed
}

void ServerGroupModel::removeServerGroup(int serverGroupId)
{
    if (m_serverGroupHash.contains(serverGroupId))
    {
        m_serverGroupList.removeOne(m_serverGroupHash[serverGroupId]);
        m_serverGroupHash.remove(serverGroupId);

        //TODO send data changed
    }
}

int ServerGroupModel::getServerGroupIndexById(int id) const
{
    if (m_serverGroupHash.contains(id))
    {
        return m_serverGroupList.indexOf(m_serverGroupHash[id]);
    }

    return 0;
}

Qt::DropActions ServerGroupModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

bool ServerGroupModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction)
        return true;

    if (!(data->hasFormat("application/servergroup.id") && data->hasFormat("application/servergroup.row") && data->hasFormat("application/servergroup.column"))
        || column >= columnCount(parent) || row >= rowCount(parent))
        return false;

    QByteArray serverGroupId = data->data("application/servergroup.id");
    QByteArray rowArray = data->data("application/servergroup.row");
    QByteArray columnArray = data->data("application/servergroup.column");

    QDataStream serverGroupIdStream(&serverGroupId, QIODevice::ReadOnly);
    QDataStream rowStream(&rowArray, QIODevice::ReadOnly);
    QDataStream columnStream(&columnArray, QIODevice::ReadOnly);

    QList<int> serverGroupIds;
    QList<int> columns;
    QList<int> rows;

    while (!serverGroupIdStream.atEnd() && !columnStream.atEnd() && !rowStream.atEnd())
    {
        int id, aRow, col;
        serverGroupIdStream >> id;
        rowStream >> aRow;
        columnStream >> col;

        serverGroupIds << id;
        rows << aRow;
        columns << col;
    }

    //Don't allow dragging and dropping of servergroups and servers in the same selection
    if (rows.contains(-1) && rows.count(-1) != rows.count()) //TODO additional checks to allow if it's the same servergroup
        return false;

    QList<int>::const_iterator i;
    int pos = 0;
    for (i = serverGroupIds.constBegin(); i != serverGroupIds.constEnd(); ++i)
    {
        if (parent.isValid()) // dropped on server item
        {
            if (rows.at(pos) < 0 && m_serverGroupHash.contains(*i)) // dropped servergroup on server item, drop below server group of child item 
            {
                m_serverGroupList.move(m_serverGroupList.indexOf(m_serverGroupHash[*i]), parent.row()+1);
            }
            else // dropped server on a server
            {
                Konversation::ServerGroupSettingsPtr serverGroup = m_serverGroupList.at(parent.row());
                if (serverGroup && serverGroup->id() != *i && m_serverGroupHash.contains(*i)) //dropped a child on another server group's child item
                {
                    if (columns.at(pos) == 0  && m_serverGroupHash.value(*i)->serverList().count() > rows.at(pos)) // dropped a server on another server group's children
                    {
                        int index = row;
                        if (index < 0) index = 0;
                        if (index > serverGroup->serverList().count()) index = serverGroup->serverList().count();
                        serverGroup->insertServer(index, m_serverGroupHash.value(*i)->serverByIndex(rows.at(pos)));
                        m_serverGroupHash[*i]->removeServerByIndex(rows.at(pos));
                    }
                    else if (columns.at(pos) == 1 && m_serverGroupHash.value(*i)->notifyList().count() > rows.at(pos)) // dropped a notify nick on another servergroup's children
                    {
                        int index = row;
                        if (index < 0) index = 0;
                        if (index > serverGroup->notifyList().count()) index = serverGroup->notifyList().count();
                        serverGroup->insertNotify(index, m_serverGroupHash.value(*i)->notifyByIndex(rows.at(pos)));
                        m_serverGroupHash[*i]->removeNotifyByIndex(rows.at(pos));
                    }
                    else // dropped a channel on another server group's children, or source had an invalid row/column number
                        return false;
                }
                else if (serverGroup && serverGroup->id() == *i) // dropped a child on a child of the same server group
                {
                    if (columns.at(pos) == 0 && serverGroup->serverList().count() > rows.at(pos)) // dropped a server on a child in the same server group
                    {
                        int index = row;
                        if (index < 0) index = 0;
                        if (index > serverGroup->serverList().count()) index = serverGroup->serverList().count();
                        serverGroup->moveServer(rows.at(pos), index);
                    }
                    else if (columns.at(pos) == 1 && serverGroup->notifyList().count() > rows.at(pos)) // dropped a notify nick on a child in the same server group
                    {
                        int index = row;
                        if (index < 0) index = 0;
                        if (index > serverGroup->notifyList().count()) index = serverGroup->notifyList().count();
                        serverGroup->moveNotify(rows.at(pos), index);
                    }
                    else if (columns.at(pos) == 2 && serverGroup->channelList().count() > rows.at(pos)) // dropped a channel on a child in the same server group
                    {
                        int index = row;
                        if (index < 0) index = 0;
                        if (index > serverGroup->channelList().count()) index = serverGroup->channelList().count();
                        serverGroup->moveChannel(rows.at(pos), index);
                    }
                    else // source had an invalid row/column number
                        return false;
                }
                else // servergroup didn't exist, or server group target id was invalid, or server item source index was invalid
                    return false;

                //TODO signal dataChanged for the affected rows (including resorted ones)
            }
        }
        else // Server group item
        {
            if (row > 0 && row < m_serverGroupList.size())
            {
                Konversation::ServerGroupSettingsPtr serverGroup = m_serverGroupList.at(row);

                if (rows.at(pos) < 0 && m_serverGroupHash.contains(*i)) // dropping server group on server group, insert above target
                {
                    m_serverGroupList.move(m_serverGroupList.indexOf(m_serverGroupHash[*i]), parent.row());
                }
                else // dropping server on server group, insert at top
                {
                    if (serverGroupIds.at(pos) != serverGroup->id() && m_serverGroupHash.contains(serverGroupIds.at(pos))) // dropping a child on a different server group
                    {
                        if (columns.at(pos) == 0 && m_serverGroupHash.value(serverGroupIds.at(pos))->serverList().count() > rows.at(pos)) // dropping server on a different server group
                        {
                            serverGroup->insertServer(0, m_serverGroupHash.value(*i)->serverByIndex(rows.at(pos)));
                            m_serverGroupHash[*i]->removeServerByIndex(rows.at(pos));

                            // TODO announce datachanged
                        }
                        else if (columns.at(pos) == 1 && m_serverGroupHash.value(serverGroupIds.at(pos))->notifyList().count() > rows.at(pos)) // dropping a notify nick on a differetn server group
                        {
                            serverGroup->insertNotify(0, m_serverGroupHash.value(*i)->notifyByIndex(rows.at(pos)));
                            m_serverGroupHash[*i]->removeNotifyByIndex(rows.at(pos));
                        }
                        else // dropping a channel on a different server group, or source had invalid row/column numbers
                            return false;
                    }
                    else if (serverGroupIds.at(pos) == serverGroup->id()) // dropping a child item on the same server group
                    {
                        if (columns.at(pos) == 0 && serverGroup->serverList().count() > rows.at(pos)) // dropping server on the same server group
                        {
                            serverGroup->moveServer(rows.at(pos), 0);

                            // TODO announce datachanged
                        }
                        else if (columns.at(pos) == 1 && serverGroup->notifyList().count() > rows.at(pos)) // dropping a notify nick on the same server group
                        {
                            serverGroup->moveNotify(rows.at(pos), 0);
                        }
                        else if (columns.at(pos) == 2 && serverGroup->channelList().count() > rows.at(pos)) // dropping a channel on the same server group
                        {
                            serverGroup->moveChannel(rows.at(pos), 0);
                        }
                        else // source had invalid row/column
                            return false;
                    }
                    else // source sever group id is invalid, or source server id was invalid
                        return false;
                }
            }
            else // server group was null
                return false;
        }

        pos++;
    }

    return true;
}

QStringList ServerGroupModel::mimeTypes() const
{
    QStringList types;
    types << "application/servergroup.id"; //FIXME is this valid?
    types << "application/servergroup.row";
    types << "application/servergroup.column";

    return types;
}

QMimeData* ServerGroupModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *mimeData = new QMimeData();
    QByteArray serverGroupIds;
    QByteArray rows;
    QByteArray columns;

    QDataStream serverGroupIdStream(&serverGroupIds, QIODevice::WriteOnly);
    QDataStream rowStream(&rows, QIODevice::WriteOnly);
    QDataStream columnStream(&columns, QIODevice::WriteOnly);

    foreach (const QModelIndex &index, indexes)
    {
        if (index.isValid())
        {
            int serverGroupId = index.internalId();
            serverGroupIdStream << serverGroupId;

            if (index.parent().isValid())
                rowStream << index.row();
            else
                rowStream << -1;

            columnStream << index.column();
        }
    }

    mimeData->setData("application/servergroup.id", serverGroupIds);
    mimeData->setData("application/servergroup.row", rows);
    mimeData->setData("application/servergroup.column", columns);

    return mimeData;
}

int ServerGroupModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() && !parent.parent().isValid() && m_serverGroupList.count() > parent.row())
    {
        switch (parent.column())
        {
            case 0:
                return m_serverGroupList[parent.row()]->serverList().count();
            case 1:
                return m_serverGroupList[parent.row()]->notifyList().count();
            case 2:
                return m_serverGroupList[parent.row()]->channelList().count();
            default:
                return 0;
        }
    }

    if (!parent.isValid())
        return m_serverGroupHash.count();

    return 0;
}

//use columns of children for servers/notify nicks/channels
int ServerGroupModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) // FIXME remove if I can't find anything else to show in columns (because both columncounts will be the same)
        return 3;

    return 3;
}

QVariant ServerGroupModel::headerData (int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(orientation);

    if (role == Qt::DisplayRole)
    {
        switch (section)
        {
            case 0:
                return i18n("Network");
            case 1:
                return i18n("Identity");
            case 2:
                return i18n("Channels");
            default:
                return QVariant();
        }
    }

    return QVariant();
}

QModelIndex ServerGroupModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column < 0)
        return QModelIndex();

    if (row >= rowCount(parent) || column >= columnCount(parent))
        return QModelIndex();

    if (!parent.isValid())
    {
        return createIndex(row, column, -1);
    }
    else
    {
        return createIndex(row, column, m_serverGroupList.at(parent.row())->id());
    }

    return QModelIndex();
}

QModelIndex ServerGroupModel::parent(const QModelIndex& index) const
{
    if (index.isValid() && index.internalId() >= 0 && m_serverGroupHash.contains(index.internalId()))
        return ServerGroupModel::index(m_serverGroupList.indexOf(m_serverGroupHash[index.internalId()]), index.column(), QModelIndex());

    return QModelIndex();
}

QVariant ServerGroupModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount(index.parent()))
        return QVariant();

    if (index.parent().isValid() && m_serverGroupHash.contains(index.internalId())) // child item
    {
        if (role == Qt::DisplayRole)
        {
            switch (index.column())
            {
                case 0:
                    return m_serverGroupHash[index.internalId()]->serverByIndex(index.row()).host();
                case 1:
                    return m_serverGroupHash[index.internalId()]->notifyByIndex(index.row());
                case 2:
                    return m_serverGroupHash[index.internalId()]->channelByIndex(index.row()).name();
                default:
                    return QVariant();
            }
        }
        else if (role == ServerGroupIdRole)
        {
            return index.internalId();
        }
    }

    if (index.internalId() < 0 && role == Qt::DisplayRole) // server group item
    {
        Konversation::ServerGroupSettingsPtr serverGroup = m_serverGroupList.at(index.row());

        if (role == Qt::DisplayRole)
        {
            switch (index.column())
            {
                case 0:
                    return serverGroup->name();
                case 1:
                    return serverGroup->identity()->getName();
                case 2:
                    return m_channelListHash[serverGroup->id()];
                default:
                    return QVariant();
            }
        }
        else if (role == ServerGroupIdRole)
        {
            return serverGroup->id();
        }
    }

    return QVariant();
}

Qt::ItemFlags ServerGroupModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags defaultFlags = QAbstractListModel::flags(index);

    if (index.isValid())
        return Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | defaultFlags;
    else
        return Qt::ItemIsDropEnabled | defaultFlags;
}

bool ServerGroupModel::hasChildren(const QModelIndex& index) const
{
    //TODO can this be replaced by a simple rowcount check?

    if (index.isValid() && !index.parent().isValid() && m_serverGroupList.count() > index.row()) // server group index
    {
        switch (index.column())
        {
            case 0:
                return (m_serverGroupList[index.row()]->serverList().count() > 0);
            case 1:
                return (m_serverGroupList[index.row()]->notifyList().count() > 0);
            case 2:
                return (m_serverGroupList[index.row()]->channelList().count() > 0);
            default:
                return false;
        }
    }
    else if(!index.isValid()) // root index
        return (m_serverGroupHash.count() > 0);

    return false;
}

QHash<int, QByteArray> ServerGroupModel::roleNames() const
{
    //TODO do rolenames
    QHash<int, QByteArray> roles;

    return roles;
}

ServerGroupFilterModel::ServerGroupFilterModel(QObject* parent, int column) : QSortFilterProxyModel(parent)
{
    m_column = column;
}

ServerGroupFilterModel::~ServerGroupFilterModel()
{
}

int ServerGroupFilterModel::getServerGroupIndexById(int id) const
{
    ServerGroupModel* source = static_cast<ServerGroupModel*>(sourceModel());
    int row = source->getServerGroupIndexById(id);

    QModelIndex srcIndex = source->index(row, 0);

    return mapFromSource(srcIndex).row();
}

bool ServerGroupFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    if (left.column() == 0 && right.column() == 0)
    {
        // Both servers of same server group
        if (left.parent().isValid() && right.parent().isValid() && left.internalId() == right.internalId())
        {
            if (sortOrder() == Qt::DescendingOrder)
                return (left.row() > right.row());
            else
                return (left.row() < right.row());
        }
        // Both server groups
        else if(!left.parent().isValid() && !right.parent().isValid())
        {
            return (left.row() < right.row());
        }
    }

    return QSortFilterProxyModel::lessThan(left, right);
}

bool ServerGroupFilterModel::filterAcceptsColumn(int column, const QModelIndex& parent) const
{
    if (parent.isValid() && column != m_column)
        return false;

    return true;
}
