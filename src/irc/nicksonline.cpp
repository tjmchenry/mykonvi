/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2014 Travis McHenry <me@travisjmchenry.com>
*/

#include "nicksonline.h"

#include "application.h"
#include "images.h"
#include "preferences.h"
#include "editnotifydialog.h"
#include "server.h"
#include "connectionmanager.h"
#include "query.h"

#include <QMultiHash>
#include <KMenu>
#include <KLineEdit>

NicksOnlineFilterModel::NicksOnlineFilterModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    Application* konvApp = static_cast<Application*>(kapp);
    m_connectionManager = konvApp->getConnectionManager();
    m_nickListModel = m_connectionManager->getNickListModel();

    m_hostmask = false;
    m_column = 1;

    m_whatsThis = QString(); // i18n("placeholder");
    m_onlineIcon = KIcon("im-user");
    m_offlineIcon = KIcon("im-user-offline");

    m_notifyTimer = new QTimer();
    m_notifyTimer->setSingleShot(true);

    updateMinimumRowHeight();

    connect(m_notifyTimer, SIGNAL(timeout()), this, SLOT(notifyCheck()));
    connect(m_connectionManager, SIGNAL(connectedServerGroupsChanged(int,int)), this, SLOT(updateNotifyConnection(int,int)));
    //TODO get these signals from input filter instead
    connect(m_nickListModel, SIGNAL(nickOnline(int,int,QString)), this, SLOT(nickOnline(int,int,QString)));
    connect(m_nickListModel, SIGNAL(nickOffline(int,int,Nick2*)), this, SLOT(nickOffline(int,int,Nick2*)));
    connect(this, SIGNAL(requestWhois(int,QString)), this, SLOT(slotRequestWhois(int,QString)));
}

NicksOnlineFilterModel::~NicksOnlineFilterModel()
{
}

QVariant NicksOnlineFilterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount(index.parent()) || index.column() >= columnCount(index.parent()))
        return QVariant();

    QModelIndex srcIndex = mapToSource(index);

    if (!index.parent().isValid()) //top level item
    {
        if (role == Qt::DisplayRole && srcIndex.internalId() < 0)
        {
            ServerGroupModel* source = static_cast<ServerGroupModel*>(sourceModel());
            Konversation::ServerGroupSettingsPtr serverGroup = source->getServerGroupByIndex(srcIndex.row());
            int sgId = serverGroup->id();

            QStringList serverNames = QStringList();

            if (m_connectionManager->getConnectedServerGroups().count(sgId) > 0)
            {
                QMultiHash<int,int>::const_iterator i = m_connectionManager->getConnectedServerGroups().constFind(sgId); 
                while (i != m_connectionManager->getConnectedServerGroups().constEnd() && i.key() == sgId)
                {
                    Server* server = m_connectionManager->getServerByConnectionId(i.value());

                    if (server)
                        serverNames.append(server->getServerName());

                    ++i;
                }
            }

            switch (index.column())
            {
                case 0:
                    return srcIndex.data(role).toString();
                case 1:
                    if (!serverNames.isEmpty())
                        return serverNames.join(", ");
                    else
                        return i18n("Disconnected");
                default:
                    return QVariant();
            }
        }
    }
    else
    {
        if (role == Qt::WhatsThisRole)
        {
            return m_whatsThis;
        }
        else if (role == Qt::SizeHintRole)
        {
            //TODO Width here is arbitrary, find some way to get a meaningful width.
            return QSize(30, m_minimumRowHeight);
        }

        if (srcIndex.parent().isValid())
        {
            QString nickString = srcIndex.sibling(srcIndex.row(), 1).data(Qt::DisplayRole).toString();

            int sgId = srcIndex.sibling(srcIndex.row(), 0).internalId();

            QHash<int, QString> serverNames = QHash<int, QString>();
            Nick2* nick;
            bool foundNick = false;

            if (m_connectionManager->getConnectedServerGroups().count(sgId) > 0)
            {
                QMultiHash<int,int>::const_iterator i = m_connectionManager->getConnectedServerGroups().constFind(sgId);

                while (i != m_connectionManager->getConnectedServerGroups().constEnd() && i.key() == sgId)
                {
                    Server* server = m_connectionManager->getServerByConnectionId(i.value());

                    if (server)
                    {
                        if (m_nickListModel->isNickOnline(i.value(), nickString))
                        {
                            serverNames.insert(i.value(), server->getServerName());

                            if (!foundNick)
                            {
                                nick = m_nickListModel->getNick(i.value(), nickString);
                                foundNick = true;
                            }
                        }
                        else if (isWatchedNickOnline(sgId, i.value(), nickString))
                        {
                            serverNames.insert(i.value(), server->getServerName());

                            if (!foundNick)
                            {
                                nick = getWatchedNick(sgId, i.value(), nickString);
                                foundNick = true;
                            }
                        }
                    }

                    ++i;
                }
            }

            if (!serverNames.isEmpty())
            {
                if (role == Qt::DisplayRole)
                {
                    switch (index.column())
                    {
                        case 0:
                            return nick->getBestPersonName(); //TODO special cases for what to display here
                        case 1:
                            if (!nick->getPrettyInfo().isEmpty())
                                return nick->getPrettyInfo();
                            else
                                return QStringList(serverNames.values()).join(", ");
                        default:
                            return QVariant();
                    }
                }
                else if (role == Qt::ToolTipRole)
                {
                    return nick->getQueryTooltip();
                }
                else if (role == Qt::DecorationRole && index.column() == 0)
                {
                    return m_onlineIcon;
                }
            }
            else
            {
                if (role == Qt::DisplayRole)
                {
                    switch (index.column())
                    {
                        case 0:
                            return nickString; //TODO special cases for what to display here
                        case 1:
                            return i18nc("(Offline) nickname details (e.g. real name from address book)", "(Offline) %1", QString());
                            //TODO get info from kpeople to put in place of that qstring..
                        default:
                            return QVariant();
                    }
                }
                else if (role == Qt::DecorationRole && index.column() == 0)
                {
                    return m_offlineIcon;
                }
            }
        }
    }

    return QSortFilterProxyModel::data(index, role);
}

QVariant NicksOnlineFilterModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(orientation);

    if (role == Qt::DisplayRole)
    {
        switch (section)
        {
            case 0:
                return i18n("Network/Nickname/Channel");
            case 1:
                return i18n("Additional Information");
            default:
                return QVariant();
        }
    }

    return QVariant();
}

bool NicksOnlineFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
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

int NicksOnlineFilterModel::columnCount(const QModelIndex& parent) const
{
    //TODO change to just return 2 always if the column count for both stays the same.
    if (parent.isValid())
        return 2;

    return 2;
}

bool NicksOnlineFilterModel::filterAcceptsColumn(int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);

    if (column > 1)
        return false;

    return true;
}

bool NicksOnlineFilterModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        if (parent.row() < Preferences::serverGroupList().count() && Preferences::serverGroupByIndex(parent.row())->notifyList().count() > row &&
            sourceModel()->index(row, m_column, parent).data(Qt::DisplayRole).toString().contains(filterRegExp()))
            return true;
        else
            return false;
    }

    return true;
}

void NicksOnlineFilterModel::slotRequestWhois(int cId, const QString& nick)
{
    Server* server = m_connectionManager->getServerByConnectionId(cId);

    if (server && !m_whoisRequested.contains(cId, nick))
    {
        m_whoisRequested.insert(cId, nick);

        server->requestWhois(nick);
    }
}

void NicksOnlineFilterModel::whoisReceived(int cId, const QString& nick)
{
    if (m_whoisRequested.contains(cId, nick))
            m_whoisRequested.remove(cId, nick);
}

void NicksOnlineFilterModel::isonResponse(int cId, int sgId, const QString& newIson)
{
    if (sgId < 0)
        return;

    QStringList newIsonList = newIson.split(' ', QString::SkipEmptyParts);

    if (!m_isonList.contains(cId) || m_isonList[cId] != newIsonList)
    {
        QStringList::const_iterator i;

        for (i = newIsonList.constBegin(); i != newIsonList.constEnd(); ++i)
        {
            if (!m_isonList[cId].contains(*i, Qt::CaseInsensitive) && isNickWatched(sgId, cId, *i))
            {
                nickOnline(sgId, cId, *i);
            }
        }

        for (i = m_isonList[cId].constBegin(); i != m_isonList[cId].constEnd(); ++i)
        {
            if (!newIsonList.contains(*i, Qt::CaseInsensitive) && isNickWatched(sgId, cId, *i))
            {
                nickOffline(sgId, cId, getWatchedNick(sgId, cId, *i));
            }
        }

        m_isonList.insert(cId, newIsonList);
    }

    m_notifyTimer->start(Preferences::self()->notifyDelay() * 1000);
}

void NicksOnlineFilterModel::watchedNicksOnline(int sgId, int cId, const QStringList& onlineList)
{
    for (int i = 0; i < onlineList.count(); i++)
    {
        QString nick;
        QString hostmask = QString();
        QString onlineSince = QString();
        if (onlineList.at(i).contains(' ') && onlineList.at(i).count(' ') > 3)
        {
            nick = onlineList.at(i).section(' ', 0, 0);
            hostmask = onlineList.at(i).section(' ', 2, 2);
            onlineSince = onlineList.at(i).section(' ', 3, 3);
        }
        else if (onlineList.at(i).contains('!'))
        {
            nick = onlineList.at(i).section('!', 0, 0);
            hostmask = onlineList.at(i).section('!', 1, 1);
        }
        else
            nick = onlineList.at(i);

        nickOnline(sgId, cId, nick);

        if (isNickOnline(cId, nick))
        {
            if (!hostmask.isEmpty())
                getNick(cId, nick)->setHostmask(hostmask);

            if (!onlineSince.isEmpty())
            {
                bool ok = false;
                QDateTime signonTime;
                uint online = onlineSince.toUInt(&ok);

                if (ok)
                {
                    signonTime.setTime_t(online);
                    getNick(cId, nick)->setOnlineSince(signonTime);
                }
            }
        }
    }
}

void NicksOnlineFilterModel::watchedNicksOffline(int sgId, int cId, const QStringList& offlineList)
{
    for (int i = 0; i < offlineList.count(); i++)
    {
        // If the nick isn't online then the user should have already been informed of the change
        // so we do nothing.
        if (isNickOnline(cId, offlineList.at(i)))
            nickOffline(sgId, cId, getNick(cId, offlineList.at(i)));
    }
}

void NicksOnlineFilterModel::slotRequestWatchedNicksList(int cId, int type)
{
    Server* server = m_connectionManager->getServerByConnectionId(cId);

    if (server && !server->getWatchTypeSupport().isEmpty())
    {
        if (server->getWatchTypeSupport().contains("MONITOR"))
        {
            server->queue("MONITOR L", Server::LowPriority);
        }

        if (server->getWatchTypeSupport().contains("WATCH"))
        {
            server->queue("WATCH L", Server::LowPriority);
        }
    }
}

void NicksOnlineFilterModel::watchedNicksListReceived(int sgId, int cId, int type)
{
    Q_UNUSED(sgId);
}

void NicksOnlineFilterModel::watchedNicksList(int sgId, int cId, int type, const QStringList& monitorList)
{
    // in case the user has watched nicks on their own, that are in the notify list, before turning on the notify list
    // we need to make sure that 1) we don't send unescessary watch commands and 2) we verify the status of already watched nicks
}

void NicksOnlineFilterModel::endOfWatchedNicksList(int sgId, int cId, int type)
{
    // start watching nicks that aren't being watched
    // delete the temporary watched list
}

void NicksOnlineFilterModel::nickOnline(int sgId, int cId, const QString& nick)
{
    if (sgId < 0)
        return;

    if (Preferences::serverGroupHash().contains(sgId))
    {
        QStringList notifyList = Preferences::serverGroupById(sgId)->notifyList();

        if (notifyList.contains(nick, Qt::CaseInsensitive))
        {
            if (isWatchedNickOnline(sgId, cId, nick)) // Nick is watched and online
            {
                if (m_nickListModel->isNickOnline(cId, nick)) // Nick was online, but we now share a channel
                {
                    // If a nick object exists, then we can remove it from our watch list, because we share a channel with them.
                    removeNotifyNick(sgId, cId, nick);
                }
            }
            else if(isNickWatched(sgId, cId, nick)) // Nick is watched and previously offline
            {
                getWatchedNick(sgId, cId, nick)->setPrintedOnline(true);

                m_connectionManager->getServerByConnectionId(cId)->announceWatchedNickOnline(nick);
            }
            else if (m_nickListModel->isNickOnline(cId, nick)) // Nick is not watched, and is now online
                m_connectionManager->getServerByConnectionId(cId)->announceWatchedNickOnline(nick);

            QRegExp pattern(nick);
            pattern.setCaseSensitivity(Qt::CaseInsensitive);

            QModelIndex parent = NicksOnlineFilterModel::index(Preferences::serverGroupList().indexOf(Preferences::serverGroupById(sgId)), m_column, QModelIndex());
            QModelIndex firstIndex = NicksOnlineFilterModel::index(notifyList.indexOf(pattern), 0, parent);
            QModelIndex lastIndex = firstIndex.sibling(firstIndex.row(), 1);

            if ((m_nickListModel->isNickOnline(cId, nick) && m_nickListModel->getNick(cId, nick)->getPrettyInfo().isEmpty()) ||
                (isWatchedNickOnline(sgId, cId, nick) && getWatchedNick(sgId, cId, nick)->getPrettyInfo().isEmpty()))
                emit requestWhois(cId, nick);

            //TODO when we can dep Qt 5 we can specify what roles have changed.
            emit dataChanged(firstIndex, lastIndex); //, QVector<int>() << Qt::DisplayRole);
        }
    }
}

void NicksOnlineFilterModel::nickOffline(int sgId, int cId, Nick2* nick)
{
    QString nickname = nick->getNickname();

    //if the nick is in the watched list, and it is offline, then we can safely declare it offline.
    if (isNickWatched(sgId, cId, nickname))
    {
        if (Preferences::serverGroupHash().contains(sgId))
        {
            QStringList notifyList = Preferences::serverGroupById(sgId)->notifyList();

            if (notifyList.contains(nickname, Qt::CaseInsensitive))
            {
                // Since it's now really offline, all of the information stored in the nick is now invalid, replace it
                Nick2* newNick = new Nick2(cId, nickname);

                connect(newNick, SIGNAL(nickChanged(int,QString,QVector<int>,QVector<int>)), this, SLOT(slotNickChanged(int,QString,QVector<int>,QVector<int>)));

                replaceNotifyNick(sgId, cId, newNick);

                QRegExp pattern(nickname);
                pattern.setCaseSensitivity(Qt::CaseInsensitive);

                QModelIndex parent = NicksOnlineFilterModel::index(Preferences::serverGroupList().indexOf(Preferences::serverGroupById(sgId)), m_column, QModelIndex());
                QModelIndex firstIndex = NicksOnlineFilterModel::index(notifyList.indexOf(pattern), 0, parent);
                QModelIndex lastIndex = firstIndex.sibling(firstIndex.row(), 1);
                //TODO when we can dep Qt 5 we can specify what roles have changed.
                emit dataChanged(firstIndex, lastIndex); //, QVector<int>() << Qt::DisplayRole);

                m_connectionManager->getServerByConnectionId(cId)->announceWatchedNickOffline(nickname);
            }
        }
        else
        {
            //TODO fetch meta contacts
        }
    }
    else // if it's not watched, then it could have just left all shared channels, add it to be sure.
    {
        nick->setPrintedOnline(true);
        addNotifyNick(sgId, cId, nick);
    }
}

bool NicksOnlineFilterModel::isNickWatched(int sgId, int cId, const QString& nick) const
{
    QString lcNick = nick.toLower();

    if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId) &&  m_watchedNicks[sgId][cId].contains(lcNick))
        return true;

    return false;
}

bool NicksOnlineFilterModel::isNickActivelyWatched(int cId, const QString& nick) const
{
    QString addNick = "+" + nick;
    if (m_activelyWatchedNicks.contains(cId) &&
        ((m_activelyWatchedNicks[cId].contains(0) && (m_activelyWatchedNicks[cId][0].contains(nick) || m_activelyWatchedNicks[cId][0].contains(addNick))) ||
        (m_activelyWatchedNicks[cId].contains(1) && (m_activelyWatchedNicks[cId][1].contains(nick) || m_activelyWatchedNicks[cId][1].contains(addNick))) ||
        (m_activelyWatchedNicks[cId].contains(2) && m_activelyWatchedNicks[cId][2].contains(nick))))
        return true;

    return false;
}

bool NicksOnlineFilterModel::isNickOnline(int cId, const QString& nick) const
{
    return m_nickListModel->isNickOnline(cId, nick) || isWatchedNickOnline(cId, nick);
}

bool NicksOnlineFilterModel::isWatchedNickOnline(int cId, const QString& nick) const
{
    Server* server = m_connectionManager->getServerByConnectionId(cId);

    if (server && server->getServerGroup())
        return isWatchedNickOnline(server->getServerGroup()->id(), cId, nick);

    return false;
}

bool NicksOnlineFilterModel::isWatchedNickOnline(int sgId, int cId, const QString& nick) const
{
    QString lcNick = nick.toLower();

    if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId) && m_watchedNicks[sgId][cId].contains(lcNick))
        return m_watchedNicks[sgId][cId][lcNick]->getPrintedOnline();

    return false;
}

Nick2* NicksOnlineFilterModel::getNick(int cId, const QString& nick) const
{
    if (isNickOnline(cId, nick))
    {
        if (m_nickListModel->isNickOnline(cId, nick))
            return m_nickListModel->getNick(cId, nick);
        else
            return getWatchedNick(cId, nick);
    }

    return NULL;
}

Nick2* NicksOnlineFilterModel::getWatchedNick(int cId, const QString& nick) const
{
    Server* server = m_connectionManager->getServerByConnectionId(cId);

    if (server && server->getServerGroup())
    {
        int sgId = server->getServerGroup()->id();

        return getWatchedNick(sgId, cId, nick);
    }

    return NULL;
}

Nick2* NicksOnlineFilterModel::getWatchedNick(int sgId, int cId, const QString& nick) const
{
    if (isNickWatched(sgId, cId, nick))
    {
        QString lcNick = nick.toLower();

        if (m_watchedNicks[sgId][cId].contains(lcNick))
            return m_watchedNicks[sgId][cId][lcNick];
    }

    return NULL;
}

void NicksOnlineFilterModel::replaceNotifyNick(int sgId, int cId, Nick2* nick)
{
    if (isNickWatched(sgId, cId, nick->getNickname()))
    {
        QString lcNick = nick->getLoweredNickname();
        Nick2* oldNick;

        if (m_watchedNicks[sgId][cId].contains(lcNick))
        {
            oldNick = m_watchedNicks[sgId][cId][lcNick];
            m_watchedNicks[sgId][cId][lcNick] = nick;
        }

        if (oldNick)
        {
            oldNick->disconnect(this);
            delete oldNick;
        }
    }
}

void NicksOnlineFilterModel::addNotifyNick(int sgId, const QString& nick)
{
    if (m_watchedNicks.contains(sgId))
    {
        WatchedNickConnections::const_iterator i;

        for (i = m_watchedNicks[sgId].constBegin(); i != m_watchedNicks[sgId].constEnd(); ++i)
        {
            Nick2* nickObj;

            if (m_nickListModel->isNickOnline(i.key(), nick))
                nickObj = m_nickListModel->getNick(i.key(), nick);
            else
                nickObj = new Nick2(i.key(), nick);

            connect(nickObj, SIGNAL(nickChanged(int,QString,QVector<int>,QVector<int>)), this, SLOT(slotNickChanged(int,QString,QVector<int>,QVector<int>)));

            addNotifyNick(sgId, i.key(), nickObj);
        }
    }
}

void NicksOnlineFilterModel::addNotifyNick(int sgId, int cId, Nick2* nick)
{
    //Do nothing if we know this nick is online.
    if (nick->isInAnyChannel())
        return;

    bool monitor = false;
    int mCount = 0;
    bool watch = false;
    int wCount = 0;
    int type = 2;

    Server* server = m_connectionManager->getServerByConnectionId(cId);

    if (server && !server->getWatchTypeSupport().isEmpty())
    {
        QHash<QString, int> watchTypes = server->getWatchTypeSupport();

        if (watchTypes.contains("MONITOR"))
        {
            monitor = true;
            mCount = watchTypes["MONITOR"];
        }

        if (watchTypes.contains("WATCH"))
        {
            watch = true;
            wCount = watchTypes["WATCH"];
        }
    }


        // monitor is supported AND (there are no nicks for this connection id yet AND monitor supports at least 1) OR
    if (monitor && ((!m_activelyWatchedNicks.contains(cId) && mCount > 0) ||
        // (there are no nicks for monitor yet AND monitor supports at least 1) OR
       (!m_activelyWatchedNicks[cId].contains(0) && mCount > 0) ||
        // (there are nicks for this connection id AND monitor supports more nicks then are currently there)
       (m_activelyWatchedNicks[cId].contains(0) && mCount > m_activelyWatchedNicks[cId][0].count()))) // using [key] creates the index, but in this case that is fine
    {
        type = 0;
    }
    else if (watch && ((!m_activelyWatchedNicks.contains(cId) && wCount > 0) ||
            (!m_activelyWatchedNicks[cId].contains(1) && wCount > 0) ||
            (m_activelyWatchedNicks[cId].contains(1) && wCount > m_activelyWatchedNicks[cId][1].count())))
    {
        type = 1;
    }

    //MONITOR + target[,target2]*
    //WATCH +target[ +target2]
    if (!m_watchedNicks.contains(sgId))
    {
        WatchedNicks wNicks = WatchedNicks();
        wNicks.insert(nick->getLoweredNickname(), nick);
        WatchedNickConnections wConnections = WatchedNickConnections();
        wConnections.insert(cId, wNicks);

        m_watchedNicks.insert(sgId, wConnections);
    }
    else if (m_watchedNicks.contains(sgId) && !m_watchedNicks[sgId].contains(cId))
    {
        WatchedNicks wNicks = WatchedNicks();
        wNicks.insert(nick->getLoweredNickname(), nick);

        m_watchedNicks[sgId].insert(cId, wNicks);
    }
    else if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId))
    {
        m_watchedNicks[sgId][cId].insert(nick->getLoweredNickname(), nick);
    }

    if (type == 2)
        m_activelyWatchedNicks[cId][type].append(nick->getNickname());
    else
    {
        QString addNick = "+" + nick->getNickname();

        m_activelyWatchedNicks[cId][type].append(addNick);
    }

    m_notifyChanged[cId] = true;

    if (!m_notifyTimer->isActive())
    {
        m_notifyTimer->start(1000);
    }
}

void NicksOnlineFilterModel::removeNotifyNick(int sgId, const QString& nick)
{
    if (m_watchedNicks.contains(sgId))
    {
        WatchedNickConnections::const_iterator i;

        for (i = m_watchedNicks[sgId].constBegin(); i != m_watchedNicks[sgId].constEnd(); ++i)
        {
            removeNotifyNick(sgId, i.key(), nick);
        }
    }
}

void NicksOnlineFilterModel::removeNotifyNick(int sgId, int cId, const QString& nick)
{
    if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId))
    {
        QString lcNick = nick.toLower();

        if (m_watchedNicks[sgId][cId].contains(lcNick))
        {
            m_watchedNicks[sgId][cId].remove(lcNick);

            removeActivelyWatchedNick(cId, nick);
        }
    }
}

void NicksOnlineFilterModel::removeActivelyWatchedNick(int cId, const QString& nick)
{
    if (isNickActivelyWatched(cId, nick))
    {
        QString addNick = "+" + nick;
        QString delNick = "-" + nick;

        if (m_activelyWatchedNicks[cId][0].contains(nick) || m_activelyWatchedNicks[cId][0].contains(delNick))
        {
            m_activelyWatchedNicks[cId][0].removeAll(nick);
            m_activelyWatchedNicks[cId][0].removeAll(addNick);
            m_activelyWatchedNicks[cId][0].append(delNick);
            m_notifyChanged[cId] = true;
        }

        if (m_activelyWatchedNicks[cId][1].contains(nick) || m_activelyWatchedNicks[cId][1].contains(delNick))
        {
            m_activelyWatchedNicks[cId][1].removeAll(nick);
            m_activelyWatchedNicks[cId][1].removeAll(addNick);
            m_activelyWatchedNicks[cId][1].append(delNick);
            m_notifyChanged[cId] = true;
        }

        if (m_activelyWatchedNicks[cId][2].contains(nick))
        {
            m_activelyWatchedNicks[cId][2].removeAll(nick);

            // If we are watching the nick with ISON, don't put them online/offline
            // just because we removed it from our watching list
            if (m_isonList[cId].contains(nick))
                m_isonList[cId].removeAll(nick);
        }

        if (m_notifyChanged[cId] && !m_notifyTimer->isActive())
            m_notifyTimer->start(1000);
    }
}

void NicksOnlineFilterModel::notifyCheck()
{
    //   cId,       type, nick
    QHash<int, QHash<int, QStringList> >::iterator i;

    if (!m_activelyWatchedNicks.isEmpty())
    {
        for (i = m_activelyWatchedNicks.begin(); i != m_activelyWatchedNicks.end(); ++i)
        {
            QHash<int, QString>::const_iterator j;

            if (m_notifyChanged[i.key()])
            {
                if (i.value().contains(0) && !i.value()[0].isEmpty())
                {
                    requestMonitor(i.key(), i.value()[0]);
                    i.value()[0].clear();
                }

                if (i.value().contains(1) && !i.value()[1].isEmpty())
                {
                    requestWatch(i.key(), i.value()[1]);
                    i.value()[1].clear();
                }

                m_notifyChanged[i.key()] = false;
            }

            if (i.value().contains(2) && !i.value()[2].isEmpty())
                requestIson(i.key(), i.value()[2]);
        }
    }
}

// have server delete watch types when connection is lost

void NicksOnlineFilterModel::requestMonitor(int cId, const QStringList& nicks)
{
    if (!nicks.isEmpty())
    {
        Server* server = m_connectionManager->getServerByConnectionId(cId);

        QStringList addNicks = QStringList();
        QStringList delNicks = QStringList();

        QStringList::const_iterator i;

        for (i = nicks.constBegin(); i != nicks.constEnd(); ++i)
        {
            if ((*i).startsWith('+'))
            {
                server->getInputFilter()->setAutomaticRequest("MONITOR +", (*i).mid(1), true);
                addNicks.append((*i).mid(1));
            }
            else if ((*i).startsWith('-'))
            {
                server->getInputFilter()->setAutomaticRequest("MONITOR -", (*i).mid(1), true);
                delNicks.append((*i).mid(1));
            }
        }

        QString mod = "+";
        QStringList modNicks = addNicks;
        QStringList messages = QStringList();

        for (int j = 0; j < 2; j++)
        {
            if (j == 1)
            {
                mod = "-";
                modNicks = delNicks;
            }

            if (modNicks.isEmpty())
                continue;

            // message must be less than 512 bytes, 'MONITOR + <list><cr><lf>' leaving 500 for nicks and their separation
            if (modNicks.join(",").length() > 500)
            {
                QString message = QString();

                for (i = modNicks.constBegin(); i != modNicks.constEnd(); ++i)
                {
                    if (message.isEmpty())
                        message = "MONITOR " + mod + " " + (*i);
                    // current length, + nick, + comma(1), + end(2)
                    else if ((message.length() + (*i).length() + 3) > 500)
                    {
                        messages.append(message);

                        message = "MONITOR " + mod + " " + (*i);
                    }
                    else
                        message += "," + (*i);

                    m_activelyWatchedNicks[cId][0].removeAll(mod + (*i));

                    if (mod == "+")
                        m_activelyWatchedNicks[cId][0].append((*i));

                }

                if (!message.isEmpty())
                    messages.append(message);
            }
            else
            {
                messages.append("MONITOR " + mod + " " + modNicks.join(","));

                for (i = modNicks.constBegin(); i != modNicks.constEnd(); ++i)
                {
                    m_activelyWatchedNicks[cId][0].removeAll(mod + (*i));

                    if (mod == "+")
                        m_activelyWatchedNicks[cId][0].append((*i));
                }
            }
        }

        if (!messages.isEmpty())
            server->queueList(messages, Server::LowPriority);
    }


}

void NicksOnlineFilterModel::requestWatch(int cId, const QStringList& nicks)
{
    if (!nicks.isEmpty())
    {
        Server* server = m_connectionManager->getServerByConnectionId(cId);

        QStringList modNicks = QStringList();
        QStringList::const_iterator i;

        for (i = nicks.constBegin(); i != nicks.constBegin(); ++i)
        {
            if ((*i).startsWith('+'))
            {
                server->getInputFilter()->setAutomaticRequest("WATCH +", (*i).mid(1), true);
                modNicks.append(*i);
            }
            else if ((*i).startsWith('-'))
            {
                server->getInputFilter()->setAutomaticRequest("WATCH -", (*i).mid(1), true);
                modNicks.append(*i);
            }
        }

        // message must be less than 512 bytes, 'WATCH +nick1[ -nick2]<cr><lf>' leaving 504 for nicks, their modifier, and their separation
        QStringList messages = QStringList();

        if (modNicks.join(" ").length() > 504)
        {
            QString message = QString();

            for (i = modNicks.constBegin(); i != modNicks.constEnd(); ++i)
            {
                if (message.isEmpty())
                    message = "WATCH " + (*i);
                // current length, + nick, + space(1), + end(2)
                else if ((message.length() + (*i).length() + 3) > 504)
                {
                    messages.append(message);

                    message = "WATCH " + (*i);
                }
                else
                    message += " " + (*i);

                m_activelyWatchedNicks[cId][1].removeAll(*i);

                if ((*i).startsWith('+'))
                    m_activelyWatchedNicks[cId][1].append((*i).mid(1));
            }

            if (!message.isEmpty())
                messages.append(message);
        }
        else
        {
            messages.append("WATCH " + modNicks.join(" "));

            for (i = modNicks.constBegin(); i != modNicks.constEnd(); ++i)
            {
                m_activelyWatchedNicks[cId][1].removeAll(*i);

                if ((*i).startsWith('+'))
                    m_activelyWatchedNicks[cId][1].append((*i).mid(1));
            }
        }

        server->queueList(messages, Server::LowPriority);
    }
}

void NicksOnlineFilterModel::requestIson(int cId, const QStringList& nicks)
{
    if (!nicks.isEmpty())
    {
        Server* server = m_connectionManager->getServerByConnectionId(cId);
        // message must be less than 512 bytes, 'ISON <list><cr><lf>' leaving 505 for nicks and their separation
        QStringList messages = QStringList();

        if (nicks.join(" ").length() > 505)
        {
            QString message = QString();

            QStringList::const_iterator i;

            for (i = nicks.constBegin(); i != nicks.constEnd(); ++i)
            {
                if (message.isEmpty())
                    message = "ISON " + (*i);
                // current length, + nick, + space(1), + end(2)
                else if ((message.length() + (*i).length() + 3) > 505)
                {
                    messages.append(message);

                    message = "ISON " + (*i);
                }
                else
                    message += " " + (*i);
            }

            if (!message.isEmpty())
                messages.append(message);
        }
        else
            messages.append("ISON " + nicks.join(" "));

        server->queueList(messages, Server::LowPriority);
    }
}

void NicksOnlineFilterModel::updateMinimumRowHeight()
{
    Images* images = Application::instance()->images();
    m_minimumRowHeight = images->getNickIcon(Images::Normal, false).height() + 2;
}

void NicksOnlineFilterModel::waitForISupport(int cId)
{
    if (m_iSupportTimerHash.contains(cId))
        m_iSupportTimerHash[cId]->start(3000);
    else
    {
        m_iSupportTimerHash[cId] = new QTimer();
        m_iSupportTimerHash[cId]->setSingleShot(true);

        connect(m_iSupportTimerHash[cId], SIGNAL(timeout()), this, SLOT(endOfISupport()));

        m_iSupportTimerHash[cId]->start(3000);
    }
}

void NicksOnlineFilterModel::endOfISupport(int cId, bool last)
{
    if (last)
    {
        if (cId < 0)
        {
            QHash<int, QTimer*>::iterator i = m_iSupportTimerHash.begin();

            while (i != m_iSupportTimerHash.end())
            {
                if (i.value()->isActive())
                    ++i;
                else
                {
                    addServerNotifyNicks(i.key());
                    i = m_iSupportTimerHash.erase(i);
                }
            }
        }
        else
        {
            addServerNotifyNicks(cId);

            if(m_iSupportTimerHash.contains(cId))
                m_iSupportTimerHash.remove(cId);
        }
    }
    else
    {
        // We recieved one line of RPL_ISUPPORT, wait another second to see if any more lines come in.
        if (m_iSupportTimerHash.contains(cId))
            m_iSupportTimerHash[cId]->start(1000);
    }
}

void NicksOnlineFilterModel::addServerNotifyNicks(int cId)
{
    Server* server = m_connectionManager->getServerByConnectionId(cId);

    if (server && server->getServerGroup())
    {
        int sgId = server->getServerGroup()->id();
        QStringList notifyList = server->getServerGroup()->notifyList();
        QStringList::const_iterator i;

        for (i = notifyList.constBegin(); i != notifyList.constEnd(); ++i)
        {
            if (!isNickWatched(sgId, cId, *i))
            {
                Nick2* nick = new Nick2(cId, *i);

                connect(nick, SIGNAL(nickChanged(int,QString,QVector<int>,QVector<int>)), this, SLOT(slotNickChanged(int,QString,QVector<int>,QVector<int>)));

                addNotifyNick(sgId, cId, nick);
            }
        }
    }
}

void NicksOnlineFilterModel::updateNotifyConnection(int sgId, int cId)
{
    if (m_connectionManager->getConnectedServerGroups().contains(sgId, cId) && (!m_watchedNicks.contains(sgId) || !m_watchedNicks[sgId].contains(cId)))
    {
        Server* server = m_connectionManager->getServerByConnectionId(cId);

        if (server && server->getServerGroup())
        {
            if (server->getWatchTypeSupport().isEmpty())
                waitForISupport(cId);
            else
                endOfISupport(cId, true);

            connect(server->getInputFilter(), SIGNAL(isonResponse(int,int,QString)), this, SLOT(isonResponse(int,int,QString)));
            connect(server->getInputFilter(), SIGNAL(watchedNicksOnline(int,int,QStringList)), this, SLOT(watchedNicksOnline(int,int,QStringList)));
            connect(server->getInputFilter(), SIGNAL(watchedNicksOffline(int,int,QStringList)), this, SLOT(watchedNicksOffline(int,int,QStringList)));
            connect(server->getInputFilter(), SIGNAL(watchedNicksList(int,int,int,QStringList)), this, SLOT(watchedNicksList(int,int,int,QStringList)));
            connect(server->getInputFilter(), SIGNAL(endOfWatchedNicksList(int,int,int)), this, SLOT(endOfWatchedNicksList(int,int,int)));
            connect(server->getInputFilter(), SIGNAL(endOfWatchedNicksList(int,int,int)), this, SLOT(watchedNicksListReceived(int,int,int)));
            connect(server->getInputFilter(), SIGNAL(endOfWhois(int,QString)), this, SLOT(whoisReceived(int,QString)));
        }
    }
    else if (!m_connectionManager->getConnectedServerGroups().contains(sgId) && m_watchedNicks.contains(sgId))
    {
        m_watchedNicks.remove(sgId);
    }
    else if (!m_connectionManager->getConnectedServerGroups().contains(sgId, cId) && m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId))
    {
        m_watchedNicks.remove(cId);
    }
}

void NicksOnlineFilterModel::slotNickChanged(int cId, const QString& nick, QVector<int> columnsChanged, QVector<int> rolesChanged)
{
    Q_UNUSED(rolesChanged);

    if (!columnsChanged.contains(2) && !columnsChanged.contains(3))
        return;

    Server* server = m_connectionManager->getServerByConnectionId(cId);
    int sgId = -1;

    if (server && server->getServerGroup())
        sgId = server->getServerGroup()->id();
    else
        return;

    if (isNickWatched(sgId, cId, nick))
    {
        int position = Preferences::serverGroupModel()->getServerGroupIndexById(sgId);
        QModelIndex srcParent = sourceModel()->index(position, 0);

        QRegExp nickPattern(nick);
        nickPattern.setCaseSensitivity(Qt::CaseInsensitive);

        position = Preferences::notifyListByGroupId(sgId).indexOf(nickPattern);
        QModelIndex srcIndex = sourceModel()->index(position, 1, srcParent);

        QModelIndex firstIndex = mapFromSource(srcIndex);
        QModelIndex lastIndex;

        if (columnsChanged.contains(2) && columnsChanged.contains(3))
            lastIndex = firstIndex.sibling(firstIndex.row(), 1);
        else if (!columnsChanged.contains(2))
            lastIndex = firstIndex;
        else
        {
            firstIndex = firstIndex.sibling(firstIndex.row(), 1);
            lastIndex = firstIndex;
        }

        emit dataChanged(firstIndex, lastIndex); // , rolesChanged);
    }
}

NicksOnline::NicksOnline(QWidget* parent) : ChatWindow(parent)
{
    setType(ChatWindow::NicksOnline);
    setName(i18n("Watched Nicks Online"));

    setSpacing(0);
    m_toolBar = new KToolBar(this, true, true);
    m_addNickname = m_toolBar->addAction(KIcon("list-add-user"), i18n("&Add Nickname..."), this, SLOT(addNickname()));
    m_addNickname->setWhatsThis(i18n("Click to add a new nick to the list of nicknames that appear on this screen."));
    m_removeNickname = m_toolBar->addAction(KIcon("list-remove-user"), i18n("&Remove Nickname"), this, SLOT(removeNickname()));
    m_removeNickname->setWhatsThis(i18n("Click to remove a nick from the list of nicknames that appear on this screen."));
    m_toolBar->addSeparator();
    //TODO change this to create meta/super nick/ whatever we call it
    m_newContact = m_toolBar->addAction(KIcon("contact-new"), i18n("Create New C&ontact..."), this, SLOT(createContact()));
    m_editContact = m_toolBar->addAction(KIcon("document-edit"), i18n("Edit C&ontact..."), this, SLOT(editContact()));
    //m_editContact->setWhatsThis(i18n("Click to create, view, or edit the KAddressBook entry associated with the nickname selected above."));
    m_toolBar->addSeparator();
    m_changeAssociation = m_toolBar->addAction(KIcon("office-address-book"), i18n("&Change Association..."), this, SLOT(changeAssociation()));
    //m_changeAssociation->setWhatsThis(i18n("Click to associate the nickname selected above with an entry in KAddressBook."));
    m_toolBar->addSeparator();
    m_openQuery = m_toolBar->addAction(KIcon("office-address-book"), i18n("Open &Query"), this, SLOT(openQuery()));

    //UI Setup
    setupUi(this);

    Application* konvApp = static_cast<Application*>(kapp);
    m_nicksOnlineModel = konvApp->getConnectionManager()->getNicksOnlineFilterModel();

    m_nickSearchLine->setProxy(m_nicksOnlineModel);
    m_nicksOnlineView->setModel(m_nicksOnlineModel);

    m_nicksOnlineView->setSortingEnabled(true);
    m_nicksOnlineView->header()->setMovable(false);

    Preferences::restoreColumnState(m_nicksOnlineView, "NicksOnline ViewSettings");

    connect(m_nicksOnlineView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(doubleClicked(QModelIndex)));
    connect(m_nicksOnlineView->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(currentChanged(QModelIndex,QModelIndex)));
    connect(m_nicksOnlineView, SIGNAL(collapsed(QModelIndex)), this, SLOT(collapsed(QModelIndex)));
    connect(m_nicksOnlineView, SIGNAL(expanded(QModelIndex)), this, SLOT(expanded(QModelIndex)));
    connect(m_nicksOnlineView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextMenu(QPoint)));
}

NicksOnline::~NicksOnline()
{
}

QString NicksOnline::getTextInLine()
{
    return m_nickSearchLine->lineEdit()->text();
}

void NicksOnline::appendInputText(const QString& text, bool fromCursor)
{
    Q_UNUSED(fromCursor);
    m_nickSearchLine->setText(m_nickSearchLine->lineEdit()->text() + text);
}

//Used to disable functions when not connected
void NicksOnline::serverOnline(bool online)
{
    Q_UNUSED(online)
    //TODO find out if this is useful at all for watched nicks
}

void NicksOnline::addNickname()
{
    // open gui to add nick
    int sgId = -1;
    QModelIndex index = m_nicksOnlineView->selectionModel()->currentIndex();

    if (index.isValid())
    {
        if(index.parent().isValid())
            sgId = index.data(ServerGroupModel::ServerGroupIdRole).toInt();
        else if (Preferences::serverGroupList().count() > index.row())
            sgId = Preferences::serverGroupByIndex(index.row())->id();
    }

    EditNotifyDialog* end = new EditNotifyDialog(this, sgId);
    connect(end, SIGNAL(notifyChanged(int,QString)), this, SLOT(slotAddNickname(int,QString)));

    end->show();
}

void NicksOnline::slotAddNickname(int sgId, const QString& nick)
{
    if (Preferences::addNotify(sgId, nick))
    {
        int parentRow = Preferences::serverGroupList().indexOf(Preferences::serverGroupById(sgId));
        int childRow = Preferences::serverGroupById(sgId)->notifyList().indexOf(nick);
        QModelIndex parent = m_nicksOnlineModel->index(parentRow, 0);
        QModelIndex selectedRow = m_nicksOnlineModel->index(childRow, 0, parent);

        m_nicksOnlineView->selectionModel()->select(selectedRow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

        static_cast<Application*>(kapp)->saveOptions(true);
    }
}

void NicksOnline::removeNickname()
{
    QModelIndex index = m_nicksOnlineView->selectionModel()->currentIndex();

    if (!index.isValid() || !index.parent().isValid())
        return;

    int sgId = index.data(ServerGroupModel::ServerGroupIdRole).toInt();

    int selectedRow = -1;
    int notifyCount = Preferences::serverGroupById(sgId)->notifyList().count();

    if (index.row() == 0 && notifyCount > 1)
        selectedRow = 0;
    else if (index.row() > 0 && index.row() < (notifyCount - 1))
        selectedRow = index.row();
    else if (index.row() > 0 && index.row() >= (notifyCount - 1))
        selectedRow = index.row() - 1;

    QModelIndex select;

    if (selectedRow < 0)
        select = index.parent();
    else
        select = m_nicksOnlineModel->index(selectedRow, 0, index.parent());

    Preferences::removeNotify(sgId, index.row());

    m_nicksOnlineView->selectionModel()->select(select, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    static_cast<Application*>(kapp)->saveOptions(true);
}

void NicksOnline::createContact()
{
    //TODO create a kpeople contact
}

void NicksOnline::editContact()
{
    //TODO open a gui to edit a kpeople connected contact
}

void NicksOnline::changeAssociation()
{
    //TODO open a gui to edit associated nicks / meta contacts for this contact
}

void NicksOnline::openQuery()
{
    QModelIndex index = m_nicksOnlineView->selectionModel()->currentIndex();

    if (!index.isValid() || !index.parent().isValid())
        return;

    QString nick = index.data(ServerGroupModel::NickRole).toString();
    int sgId = index.data(ServerGroupModel::ServerGroupIdRole).toInt();

    Application* konvApp = static_cast<Application*>(kapp);
    ConnectionManager* conMan = konvApp->getConnectionManager();

    if (conMan->getConnectedServerGroups().contains(sgId))
    {
        Server* server = conMan->getServerByConnectionId(conMan->getConnectedServerGroups().value(sgId));

        class Query* query = server->addQuery(nick, true /*we initiated*/);

        emit showView(query);
    }
}

void NicksOnline::currentChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);

    // find out what type of index we're selecting
    if (!current.isValid() || !current.parent().isValid()) //none or server
    {
        m_addNickname->setEnabled(true);
        m_removeNickname->setEnabled(false);
        m_newContact->setEnabled(true);
        m_editContact->setEnabled(false);
        m_changeAssociation->setEnabled(false);
        m_openQuery->setEnabled(false);
    }
    else // nick
    {
        m_addNickname->setEnabled(true);
        m_removeNickname->setEnabled(true);
        m_newContact->setEnabled(true);
        m_editContact->setEnabled(false);
        m_changeAssociation->setEnabled(true);
        m_openQuery->setEnabled(true);
    }

    // TODO if it's a meta contact leave all

    // TODO handle selection of server and meta contact or server and nick or meta contact and nick

}

void NicksOnline::doubleClicked(const QModelIndex& index)
{
    // send double clicked signal with the target if it's a nick, no action if it's a server
    if (!index.isValid() || !index.parent().isValid())
        return;

    QString nick = index.data(ServerGroupModel::NickRole).toString();
    int sgId = index.data(ServerGroupModel::ServerGroupIdRole).toInt();

    int cId = -1;

    Application* konvApp = static_cast<Application*>(kapp);
    ConnectionManager* conMan = konvApp->getConnectionManager();

    if (conMan->getConnectedServerGroups().contains(sgId))
        cId = conMan->getConnectedServerGroups().value(sgId);

    if (cId >= 0)
        emit doubleClicked(cId, nick);

    //TODO if it's a meta contact, with multiple connected servers, ask which one
}

void NicksOnline::collapsed(const QModelIndex& index)
{
    Q_UNUSED(index);
    // record in the settings
    // TODO set those settings to allow multiple collapse/expands for server groups
}

void NicksOnline::expanded(const QModelIndex& index)
{
    Q_UNUSED(index);
    // record in the settings
    // TODO set those settings to allow multiple collapse/expands for server groups
}

void NicksOnline::contextMenu(const QPoint& pos)
{
    QModelIndex index = m_nicksOnlineView->indexAt(pos);
    if (!index.isValid()) return;

    KMenu* menu = new KMenu(this);

    if (index.parent().isValid())
    {
        menu->addAction(KIcon("list-add-user"), i18n("&Add Nickname..."), this, SLOT(addNickname()));
        menu->addAction(KIcon("list-remove-user"), i18n("&Remove Nickname"), this, SLOT(removeNickname()));
        menu->addSeparator();
        menu->addAction(KIcon("contact-new"), i18n("Create New C&ontact..."), this, SLOT(createContact()));
        menu->addAction(KIcon("document-edit"), i18n("Edit C&ontact..."), this, SLOT(editContact()));
        menu->addAction(KIcon("office-address-book"), i18n("&Change Association..."), this, SLOT(changeAssociation()));
        menu->addSeparator();
        menu->addAction(KIcon("office-address-book"), i18n("Open &Query"), this, SLOT(openQuery()));
    }
    else
    {
        menu->addAction(KIcon("list-add-user"), i18n("&Add Nickname..."), this, SLOT(addNickname()));
    }

    // TODO if meta contact allow remove/open query/edit contact/choose association

    menu->exec(QCursor::pos());

    delete menu;
}

#include "nicksonline.moc"
