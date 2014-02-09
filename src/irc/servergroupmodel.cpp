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
        int replace = 0; // types of replace: 0 = no row change, 1 = insert rows, 2 = remove rows
        int newRows = 0;
        QModelIndex parent;
        bool nameChanged = false;
        bool identityChanged = false;

        if (m_serverGroupHash.contains(serverGroupId))
        {
            if (m_serverGroupHash[serverGroupId] != serverGroup)
            {
                parent = ServerGroupModel::index(m_serverGroupList.indexOf(m_serverGroupHash[serverGroupId]), 0);

                int oldRows = qMax(m_serverGroupHash[serverGroupId]->serverList().count(), m_serverGroupHash[serverGroupId]->notifyList().count());
                oldRows = qMax(oldRows, m_serverGroupHash[serverGroupId]->channelList().count());

                int newRows = qMax(serverGroup->serverList().count(), serverGroup->notifyList().count());
                newRows = qMax(newRows, serverGroup->channelList().count());

                if (oldRows < newRows) // insert rows
                {
                    replace = 1;
                    beginInsertRows(parent, oldRows, (newRows - 1));
                }
                else if (oldRows > newRows)
                {
                    replace = 2;
                    beginRemoveRows(parent, newRows, (oldRows - 1));
                }

                if (m_serverGroupHash[serverGroupId]->name() != serverGroup->name())
                    nameChanged = true;

                if (m_serverGroupHash[serverGroupId]->identityId() != serverGroup->identityId())
                    identityChanged = true;
            }
            else // if they're the same we can end here
                return;

            m_serverGroupList.replace(m_serverGroupList.indexOf(m_serverGroupHash[serverGroupId]), serverGroup);
        }
        else
        {
            replace = 1;
            beginInsertRows(QModelIndex(), m_serverGroupList.count(), m_serverGroupList.count());

            m_serverGroupList.append(serverGroup);
        }

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

        bool channelsChanged = false;

        if (parent.isValid() && m_channelListHash[serverGroupId] != channels)
            channelsChanged = true;

        m_channelListHash[serverGroupId] = channels;

        QModelIndex lastParent;

        if (nameChanged)
            lastParent = parent;

        if (identityChanged)
            lastParent = parent.sibling(parent.row(), 1);

        if (channelsChanged)
            lastParent = parent.sibling(parent.row(), 2);

        QModelIndex firstIndex = ServerGroupModel::index(0, 0, parent);
        QModelIndex lastIndex;

        if (parent.isValid())
        {
            if (serverGroup->serverList().count() == newRows)
                lastIndex = ServerGroupModel::index((newRows - 1), 0, parent);

            if (serverGroup->notifyList().count() == newRows)
                lastIndex = ServerGroupModel::index((newRows - 1), 1, parent);

            if (serverGroup->channelList().count() == newRows)
                lastIndex = ServerGroupModel::index((newRows - 1), 2, parent);
        }

        switch (replace)
        {
            case 0:
                break;
            case 1:
                endInsertRows();
                break;
            case 2:
                endRemoveRows();
                break;
        }

        // These have to be separate because dataChanged can't handle ranges with different parents
        if (lastParent.isValid())
            emit dataChanged(parent, lastParent);

        if (lastIndex.isValid())
            emit dataChanged(firstIndex, lastIndex);
    }
}

void ServerGroupModel::removeServerGroup(int serverGroupId)
{
    if (m_serverGroupHash.contains(serverGroupId))
    {
        int position = m_serverGroupList.indexOf(m_serverGroupHash[serverGroupId]);

        beginRemoveRows(QModelIndex(), position, position);
        m_serverGroupList.removeOne(m_serverGroupHash[serverGroupId]);
        m_serverGroupHash.remove(serverGroupId);
        endRemoveRows();
    }
}

void ServerGroupModel::removeServer(int serverGroupId, int index)
{
    if (m_serverGroupHash.contains(serverGroupId) && m_serverGroupHash[serverGroupId]->serverList().count() > index)
    {
        int position = m_serverGroupList.indexOf(m_serverGroupHash[serverGroupId]);
        QModelIndex parent = ServerGroupModel::index(position, 0);

        beginRemoveRows(parent, index, index);
        m_serverGroupHash[serverGroupId]->removeServerByIndex(index);
        endRemoveRows();
    }
}

void ServerGroupModel::removeNotify(int serverGroupId, int index)
{
    if (m_serverGroupHash.contains(serverGroupId) && m_serverGroupHash[serverGroupId]->notifyList().count() > index)
    {
        int position = m_serverGroupList.indexOf(m_serverGroupHash[serverGroupId]);
        QModelIndex parent = ServerGroupModel::index(position, 0);

        beginRemoveRows(parent, index, index);
        m_serverGroupHash[serverGroupId]->removeNotifyByIndex(index);
        endRemoveRows();
    }
}

bool ServerGroupModel::addNotify(int serverGroupId, const QString& nick)
{
    if (m_serverGroupHash.contains(serverGroupId))
    {
        int position = m_serverGroupList.indexOf(m_serverGroupHash[serverGroupId]);
        int newIndex = m_serverGroupHash[serverGroupId]->notifyList().count();
        QModelIndex parent = ServerGroupModel::index(position, 0);

        beginInsertRows(parent, newIndex, newIndex);
        m_serverGroupHash[serverGroupId]->addNotify(nick);
        endInsertRows();

        return true;
    }

    return false;
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
        || column >= columnCount(parent) || row > rowCount(parent))
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

    QList<int>::const_iterator i;
    int pos = 0;

    // Don't allow dragging and dropping of servergroups and servers in the same selection
    if (serverGroupIds.contains(-1) && serverGroupIds.count(-1) != serverGroupIds.count())
    {
        for (i = serverGroupIds.constBegin(); i != serverGroupIds.constEnd(); ++i)
        {
            // If this is a server group, and it has a valid index/row
            if (*i < 0 && (rows.at(pos) > 0 && rows.at(pos) < m_serverGroupList.count()))
            {
                Konversation::ServerGroupSettingsPtr serverGroup = m_serverGroupList.at(rows.at(pos));

                // Remove all children of this servergroup from the list
                if (serverGroupIds.contains(serverGroup->id()))
                {
                    while (!serverGroupIds.isEmpty() && (serverGroupIds.indexOf(serverGroup->id()) != -1))
                    {
                        int index = serverGroupIds.indexOf(serverGroup->id());
                        serverGroupIds.removeAt(index);
                        rows.removeAt(index);
                        columns.removeAt(index);
                    }
                }
            }

            pos++;
        }

        // If there are still servers and servergroups in this list then this is an invalid selection, return false.
        if (serverGroupIds.contains(-1) && serverGroupIds.count(-1) != serverGroupIds.count())
            return false;
    }

    // don't allow moving the last server in a server group
    if (!serverGroupIds.contains(-1)) // If there aren't server groups, then we're moving servers
    {
        pos = 0;

        for (i = serverGroupIds.constBegin(); i != serverGroupIds.constEnd(); ++i)
        {
            if (columns.at(pos) == 0 && m_serverGroupHash.contains(*i) && m_serverGroupHash[*i]->serverList().count() <= serverGroupIds.count(*i))
                return false;

            pos++;
        }
    }

    pos = 0;

    for (i = serverGroupIds.constBegin(); i != serverGroupIds.constEnd(); ++i)
    {
        if (parent.isValid()) // dropped on child item
        {
            QModelIndex newParent;

            if (parent.parent().isValid())
                newParent = parent.parent();
            else
                newParent = parent;

            Konversation::ServerGroupSettingsPtr serverGroup;

            if (*i >= 0 && m_serverGroupHash.contains(*i))
                serverGroup = m_serverGroupHash[*i];
            else if (*i < 0 && rows.at(pos) < m_serverGroupList.count())
                serverGroup = m_serverGroupList.at(rows.at(pos));

            if (serverGroup)
            {
                int sourceRow = rows.at(pos);

                if (*i < 0) // dropped servergroup on child item, drop below server group of child item
                {
                    int newRow = row;
                    int moveRow = row;

                    if (newRow < 0 || newRow >= m_serverGroupList.count()) // inserting at the very end
                    {
                        newRow = parent.row();

                        // Dropped on first server group, go above it
                        if (newRow == 0) moveRow = newRow;
                        // Dropped on last server group, go below it
                        else if (newRow == (m_serverGroupList.count() -1)) moveRow = parent.row() + 1;
                        // dropped somewhere in between, go above.
                        else moveRow = parent.row() - 1;
                    }

                    if (!beginMoveRows(QModelIndex(), sourceRow, sourceRow, QModelIndex(), moveRow))
                        return false;

                    m_serverGroupList.move(sourceRow, newRow);

                    endMoveRows();
                }
                else if (newParent.row() >= 0 && newParent.row() < m_serverGroupList.count()) // dropped child on child
                {
                    QModelIndex sourceParent = ServerGroupModel::index(m_serverGroupList.indexOf(serverGroup), columns.at(pos), QModelIndex());

                    Konversation::ServerGroupSettingsPtr newServerGroup = m_serverGroupList.at(newParent.row());

                    if (newServerGroup && serverGroup->id() != newServerGroup->id()) // dropped a child on another server group's child item
                    {
                        int newRow = row;
                        int moveRow = row;

                        if (parent.parent().isValid())
                        {
                            switch (row)
                            {
                                case 0:
                                    newRow = parent.row();
                                    moveRow = newRow;
                                    break;
                                case -1:
                                case 1:
                                default:
                                    if (parent.row() == 0 && rowCount(parent.parent()) > 1)
                                    {
                                        newRow = parent.row();
                                        moveRow = newRow;
                                    }
                                    else
                                    {
                                        newRow = parent.row() + 1;
                                        moveRow = newRow;
                                    }
                                    break;
                            }
                        }

                        if (newRow < 0)
                        {
                            moveRow = 0;
                            newRow = 0;
                        }

                        if (columns.at(pos) == 0  && serverGroup->serverList().count() > sourceRow) // dropped a server on another server group's children
                        {
                            if (newRow >= newServerGroup->serverList().count()) // inserting at the end
                            {
                                moveRow = newServerGroup->serverList().count();
                                newRow = moveRow;
                            }

                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, moveRow))
                                return false;

                            newServerGroup->insertServer(newRow, serverGroup->serverByIndex(sourceRow));
                            serverGroup->removeServerByIndex(sourceRow);

                            endMoveRows();
                        }
                        else if (columns.at(pos) == 1 && serverGroup->notifyList().count() > sourceRow) // dropped a notify nick on another servergroup's children
                        {
                            if (newRow >= newServerGroup->notifyList().count()) // inserting at the end
                            {
                                moveRow = newServerGroup->notifyList().count();
                                newRow = moveRow;
                            }

                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, moveRow))
                                return false;

                            newServerGroup->insertNotify(newRow, serverGroup->notifyByIndex(sourceRow));
                            serverGroup->removeNotifyByIndex(sourceRow);

                            endMoveRows();
                        }
                        else // dropped a channel on another server group's children, or source had an invalid row/column number
                            return false;
                    }
                    else if (newServerGroup && serverGroup->id() == newServerGroup->id()) // dropped a child on a child of the same server group
                    {
                        int newRow = row;
                        int moveRow = row;

                        if (parent.parent().isValid())
                        {
                            switch (row)
                            {
                                case 0:
                                    newRow = parent.row();
                                    moveRow = newRow;
                                    break;
                                case -1:
                                case 1:
                                default:
                                    if (parent.row() == 0)
                                    {
                                        newRow = parent.row();
                                        moveRow = newRow;
                                    }
                                    else
                                    {
                                        newRow = parent.row();
                                        moveRow = newRow + 1;
                                    }
                                    break;
                            }
                        }

                        if (newRow < 0)
                        {
                            moveRow = 0;
                            newRow = 0;
                        }

                        if (columns.at(pos) == 0 && serverGroup->serverList().count() > sourceRow) // dropped a server on a child in the same server group
                        {
                            if (newRow >= serverGroup->serverList().count()) // inserting at the end
                            {
                                moveRow = serverGroup->serverList().count();
                                newRow = moveRow - 1;
                            }

                            if (sourceRow > newRow) // if moving up in the same parent, newrow will be placed after the moverow
                                moveRow = newRow;

                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, moveRow))
                                return false;

                            serverGroup->moveServer(sourceRow, newRow);

                            endMoveRows();
                        }
                        else if (columns.at(pos) == 1 && serverGroup->notifyList().count() > sourceRow) // dropped a notify nick on a child in the same server group
                        {
                            if (newRow >= serverGroup->notifyList().count()) // inserting at the end
                            {
                                moveRow = serverGroup->notifyList().count();
                                newRow = moveRow - 1;
                            }

                            if (sourceRow > newRow) // if moving up in the same parent, newrow will be placed after the moverow
                                moveRow = newRow;

                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, moveRow))
                                return false;

                            serverGroup->moveNotify(sourceRow, newRow);

                            endMoveRows();
                        }
                        else if (columns.at(pos) == 2 && serverGroup->channelList().count() > sourceRow) // dropped a channel on a child in the same server group
                        {
                            if (newRow >= serverGroup->channelList().count()) // inserting at the end
                            {
                                moveRow = serverGroup->channelList().count();
                                newRow = moveRow - 1;
                            }

                            if (sourceRow > newRow) // if moving up in the same parent, newrow will be placed after the moverow
                                moveRow = newRow;

                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, moveRow))
                                return false;

                            serverGroup->moveChannel(sourceRow, newRow);

                            endMoveRows();
                        }
                        else // source had an invalid row/column number
                            return false;
                    }
                    else // servergroup didn't exist, or server group target id was invalid, or server item source index was invalid
                        return false;
                }
                else // new parent row had an invalid row
                    return false;
            }
            else // server group id was invalid
                return false;
        }
        else // Server group item or invalid target
        {
            Konversation::ServerGroupSettingsPtr serverGroup;

            if (*i >= 0 && m_serverGroupHash.contains(*i))
                serverGroup = m_serverGroupHash[*i];
            else if (*i < 0 && rows.at(pos) < m_serverGroupList.count())
                serverGroup = m_serverGroupList.at(rows.at(pos));

            if (serverGroup)
            {
                if (*i < 0) // dropping server group on server group, insert above target
                {
                    int sourceRow = m_serverGroupList.indexOf(serverGroup);
                    int newRow = row;
                    int moveRow = row;

                    if (newRow < 0 || newRow >= m_serverGroupList.count()) // inserting at the very end
                    {
                        newRow = (m_serverGroupList.count() - 1);
                        moveRow = m_serverGroupList.count();
                    }

                    if (!beginMoveRows(QModelIndex(), sourceRow, sourceRow, QModelIndex(), moveRow))
                        return false;

                    m_serverGroupList.move(sourceRow, newRow);

                    endMoveRows();
                }
                else if (row >= 0 && row < m_serverGroupList.count()) // dropping child on server group, insert at top
                {
                    Konversation::ServerGroupSettingsPtr newServerGroup = m_serverGroupList.at(row);

                    QModelIndex sourceParent = ServerGroupModel::index(m_serverGroupList.indexOf(serverGroup), columns.at(pos), QModelIndex());
                    QModelIndex newParent = ServerGroupModel::index(row, column, QModelIndex());
                    int sourceRow = rows.at(pos);
                    int newRow = 0;

                    if (newServerGroup && serverGroup->id() != newServerGroup->id()) // dropping a child on a different server group
                    {
                        if (columns.at(pos) == 0 && serverGroup->serverList().count() > sourceRow) // dropping server on a different server group
                        {
                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, newRow))
                                return false;

                            newServerGroup->insertServer(newRow, serverGroup->serverByIndex(sourceRow));
                            serverGroup->removeServerByIndex(sourceRow);

                            endMoveRows();
                        }
                        else if (columns.at(pos) == 1 && serverGroup->notifyList().count() > sourceRow) // dropping a notify nick on a different server group
                        {
                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, newRow))
                                return false;

                            newServerGroup->insertNotify(newRow, serverGroup->notifyByIndex(sourceRow));
                            serverGroup->removeNotifyByIndex(sourceRow);

                            endMoveRows();
                        }
                        else // dropping a channel on a different server group, or source had invalid row/column numbers
                            return false;
                    }
                    else if (newServerGroup && serverGroup->id() == newServerGroup->id()) // dropping a child item on the same server group
                    {
                        if (columns.at(pos) == 0 && serverGroup->serverList().count() > sourceRow) // dropping server on the same server group
                        {
                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, newRow))
                                return false;

                            serverGroup->moveServer(sourceRow, newRow);

                            endMoveRows();
                        }
                        else if (columns.at(pos) == 1 && serverGroup->notifyList().count() > sourceRow) // dropping a notify nick on the same server group
                        {
                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, newRow))
                                return false;

                            serverGroup->moveNotify(sourceRow, newRow);

                            endMoveRows();
                        }
                        else if (columns.at(pos) == 2 && serverGroup->channelList().count() > sourceRow) // dropping a channel on the same server group
                        {
                            if (!beginMoveRows(sourceParent, sourceRow, sourceRow, newParent, newRow))
                                return false;

                            serverGroup->moveChannel(sourceRow, newRow);

                            endMoveRows();
                        }
                        else // source had invalid row/column
                            return false;
                    }
                    else // source sever group id is invalid
                        return false;
                }
                else // dropping server on invalid target
                {
                    //FIXME drop at bottom?
                    return false;
                }
            }
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
            // needs to be put into an int first or it will just be zero
            int serverGroupId = index.internalId();

            if (serverGroupId >= 0 || (serverGroupId < 0 && index.column() == 0))
            {
                serverGroupIdStream << serverGroupId;

                rowStream << index.row();

                columnStream << index.column();
            }
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
        Konversation::ServerGroupSettingsPtr serverGroup = m_serverGroupHash[index.internalId()];

        if (role == Qt::DisplayRole)
        {
            Konversation::ServerSettings server;
            QString serverName;

            switch (index.column())
            {
                case 0:
                    server = serverGroup->serverByIndex(index.row());
                    serverName = server.host();

                    if (server.port() != 6667)
                        serverName += ':' + QString::number(server.port());

                    if (server.SSLEnabled())
                        serverName += " (SSL)";

                    return serverName;
                case 1:
                    return serverGroup->notifyByIndex(index.row());
                case 2:
                    return serverGroup->channelByIndex(index.row()).name();
                default:
                    return QVariant();
            }
        }
        else if (role == ServerGroupIdRole)
        {
            return serverGroup->id();
        }
        else if (role == IsServerRole)
        {
            return 1;
        }
    }

    if (index.internalId() < 0 && index.row() < m_serverGroupList.count()) // server group item
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
        else if (role == ExpandedRole)
        {
            return serverGroup->expanded();
        }
        else if (role == IsServerRole)
        {
            return 0;
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

bool ServerGroupFilterModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        if (sourceModel()->rowCount(parent.sibling(parent.row(), m_column)) > row)
            return true;
        else
            return false;
    }

    return true;
}
