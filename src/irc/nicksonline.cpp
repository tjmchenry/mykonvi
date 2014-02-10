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

    m_timer = new QTimer();
    m_timer->setSingleShot(true);

    updateMinimumRowHeight();

    connect(m_timer, SIGNAL(timeout()), this, SLOT(isonCheck()));
    connect(m_connectionManager, SIGNAL(connectedServerGroupsChanged(int, int)), this, SLOT(updateNotifyConnection(int, int)));
    //TODO get these signals from input filter instead
    connect(m_nickListModel, SIGNAL(nickOnline(int, int, QString)), this, SLOT(nickOnline(int, int, QString)));
    connect(m_nickListModel, SIGNAL(nickOffline(int, int, QString)), this, SLOT(nickOffline(int, int, QString)));
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

            if (m_connectionManager->getConnectedServerGroups().count(sgId) > 0)
            {
                QMultiHash<int,int>::const_iterator i = m_connectionManager->getConnectedServerGroups().constFind(sgId);
                while (i != m_connectionManager->getConnectedServerGroups().constEnd() && i.key() == sgId)
                {
                    Server* server = m_connectionManager->getServerByConnectionId(i.value());

                    if (server && (m_nickListModel->isNickOnline(i.value(), nickString) || (isNickWatched(sgId, i.value(), nickString) && isWatchedNickOnline(sgId, i.value(), nickString))))
                        serverNames.insert(i.value(), server->getServerName());

                    ++i;
                }
            }

            if (!serverNames.isEmpty())
            {
                if (role == Qt::DecorationRole && index.column() == 0)
                {
                    return m_onlineIcon;
                }

                //This will get the nick object for the first connection
                Nick2* nick;
                bool foundNick = false;

                QHash<int, QString>::const_iterator i;
                for (i = serverNames.constBegin(); i != serverNames.constEnd(); ++i)
                {
                    if (m_nickListModel->isNickOnline(i.key(), nickString))
                    {
                        foundNick = true;
                        nick = m_nickListModel->getNick(serverNames.keys().first(), nickString);
                        break;
                    }
                }

                if (foundNick)
                {
                    QString info = QString();
                    if (role == Qt::DisplayRole)
                    {
                        switch (index.column())
                        {
                            case 0:
                                return nick->getBestPersonName(); //TODO special cases for what to display here
                            case 1:
                                if (nick->isAway())
                                {
                                    info += i18n("Away");
                                    if (!nick->getAwayMessage().isEmpty())
                                        info += " (" + nick->getAwayMessage() + ')';
                                }

                                if (!nick->getHostmask().isEmpty())
                                    info += ' ' + nick->getHostmask();

                                if (!nick->getRealName().isEmpty())
                                    info += " (" + nick->getRealName() + ')';

                                if (!nick->getNetServer().isEmpty())
                                {
                                    info += i18n( " online via %1", nick->getNetServer() );
                                    if (!nick->getNetServerInfo().isEmpty())
                                        info += " (" + nick->getNetServerInfo() + ')';
                                }

                                if (!nick->getOnlineSince().isNull())
                                    info += i18n(" since %1", nick->getPrettyOnlineSince());

                                //TODO run a whois if this is empty...

                                return info;
                            default:
                                return QVariant();
                        }
                    }
                    else if (role == Qt::ToolTipRole)
                    {
                        return nick->getQueryTooltip();
                    }
                }
                else
                {
                    if (role == Qt::DisplayRole)
                    {
                        switch (index.column())
                        {
                            case 0:
                                return nickString;
                            case 1:
                                return QStringList(serverNames.values()).join(", ");
                            default:
                                return QVariant();
                        }
                    }
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

void NicksOnlineFilterModel::notifyResponse(int cId, const QString& newIson)
{
    QStringList newIsonList = newIson.split(' ', QString::SkipEmptyParts);

    Server* server = m_connectionManager->getServerByConnectionId(cId);
    int sgId = -1;

    if (server->getServerGroup())
        sgId = server->getServerGroup()->id();
    else
        return;

    if (!m_isonList.contains(cId) || m_isonList[cId] != newIsonList)
    {
        QStringList::const_iterator i;

        for (i = newIsonList.constBegin(); i != newIsonList.constEnd(); ++i)
        {
            if (!m_isonList[cId].contains(*i) && isNickWatched(sgId, cId, *i))
            {
                nickOnline(sgId, cId, *i);
            }
        }

        for (i = m_isonList[cId].constBegin(); i != m_isonList[cId].constEnd(); ++i)
        {
            if (!newIsonList.contains(*i) && isNickWatched(sgId, cId, *i))
            {
                nickOffline(sgId, cId, *i);
            }
        }

        m_isonList.insert(cId, newIsonList);
    }

    m_timer->start(Preferences::self()->notifyDelay() * 1000);
}

void NicksOnlineFilterModel::setWatchedNickOnline(int sgId, int cId, const QString& nick, bool online)
{
    if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId))
    {
        if (m_watchedNicks[sgId][cId].contains(0) && m_watchedNicks[sgId][cId][0].contains(nick))
        {
            m_watchedNicks[sgId][cId][0][nick] = online;
        }
        else if (m_watchedNicks[sgId][cId].contains(1) && m_watchedNicks[sgId][cId][1].contains(nick))
        {
            m_watchedNicks[sgId][cId][1][nick] = online;
        }
        else if (m_watchedNicks[sgId][cId].contains(2) && m_watchedNicks[sgId][cId][2].contains(nick))
        {
            m_watchedNicks[sgId][cId][2][nick] = online;
        }
    }
}

void NicksOnlineFilterModel::nickOnline(int sgId, int cId, const QString& nick)
{
    if (Preferences::serverGroupHash().contains(sgId))
    {
        QStringList notifyList = Preferences::serverGroupById(sgId)->notifyList();

        if (notifyList.contains(nick))
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
                setWatchedNickOnline(sgId, cId, nick, true);
                m_connectionManager->getServerByConnectionId(cId)->announceWatchedNickOnline(nick);
            }
            else if (m_nickListModel->isNickOnline(cId, nick)) // Nick is not watched, and is now online
                m_connectionManager->getServerByConnectionId(cId)->announceWatchedNickOnline(nick);

            QModelIndex parent = NicksOnlineFilterModel::index(Preferences::serverGroupList().indexOf(Preferences::serverGroupById(sgId)), m_column, QModelIndex());
            QModelIndex firstIndex = NicksOnlineFilterModel::index(notifyList.indexOf(nick), 0, parent);
            QModelIndex lastIndex = firstIndex.sibling(firstIndex.row(), 1);

            //TODO when we can dep Qt 5 we can specify what roles have changed.
            emit dataChanged(firstIndex, lastIndex); //, QVector<int>() << Qt::DisplayRole);
        }
    }
}

void NicksOnlineFilterModel::nickOffline(int sgId, int cId, const QString& nick)
{
    //if the nick is in the watched list, and it is offline, then we can safely declare it offline.
    if (isNickWatched(sgId, cId, nick))
    {
        if (Preferences::serverGroupHash().contains(sgId))
        {
            QStringList notifyList = Preferences::serverGroupById(sgId)->notifyList();

            if (notifyList.contains(nick))
            {
                setWatchedNickOnline(sgId, cId, nick, false);

                QModelIndex parent = NicksOnlineFilterModel::index(Preferences::serverGroupList().indexOf(Preferences::serverGroupById(sgId)), m_column, QModelIndex());
                QModelIndex firstIndex = NicksOnlineFilterModel::index(notifyList.indexOf(nick), 0, parent);
                QModelIndex lastIndex = firstIndex.sibling(firstIndex.row(), 1);
                //TODO when we can dep Qt 5 we can specify what roles have changed.
                emit dataChanged(firstIndex, lastIndex); //, QVector<int>() << Qt::DisplayRole);

                m_connectionManager->getServerByConnectionId(cId)->announceWatchedNickOffline(nick);
            }
        }
        else
        {
            //TODO fetch meta contacts
        }
    }
    else // if it's not watched, then it could have just left all shared channels, add it to be sure.
    {
        addNotifyNick(sgId, cId, nick, true);
    }
}

bool NicksOnlineFilterModel::isNickWatched(int sgId, int cId, const QString& nick) const
{
    if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId) && 
        ((m_watchedNicks[sgId][cId].contains(0) && m_watchedNicks[sgId][cId][0].contains(nick)) ||
        (m_watchedNicks[sgId][cId].contains(1) && m_watchedNicks[sgId][cId][1].contains(nick)) ||
        (m_watchedNicks[sgId][cId].contains(2) && m_watchedNicks[sgId][cId][2].contains(nick))))
        return true;

    return false;
}

bool NicksOnlineFilterModel::isWatchedNickOnline(int sgId, int cId, const QString& nick) const
{
    if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId) &&
        (((m_watchedNicks[sgId][cId].contains(0) && m_watchedNicks[sgId][cId][0].contains(nick)) && m_watchedNicks[sgId][cId][0][nick]) ||
        ((m_watchedNicks[sgId][cId].contains(1) && m_watchedNicks[sgId][cId][1].contains(nick)) && m_watchedNicks[sgId][cId][1][nick]) ||
        ((m_watchedNicks[sgId][cId].contains(2) && m_watchedNicks[sgId][cId][2].contains(nick)) && m_watchedNicks[sgId][cId][2][nick])))
        return true;

    return false;
}

void NicksOnlineFilterModel::addNotifyNick(int sgId, int cId, const QString& nick, bool online)
{
    //TODO figure out a way to check and see if MONITOR/WATCH/ISON are supported and prefer them in that order
    bool monitor = false;
    int mCount = 10;
    bool watch = false;
    int wCount = 10;
    int type = 2;

        // monitor is supported AND (there are no nicks for this servergroup yet AND monitor supports at least 1) OR
    if (monitor && ((!m_watchedNicks.contains(sgId) && mCount > 0) || 
        // (there are no nicks for this connection ID yet AND monitor supports at least 1) OR
        (!m_watchedNicks[sgId].contains(cId) && mCount > 0) ||
        // (there are nicks for this connection id AND monitor supports more nicks then are currently there)
        (m_watchedNicks[sgId].contains(cId) && mCount > m_watchedNicks[sgId][cId][0].count()))) // using [key] creates the index, but in this case that is fine
    {
        type = 0;
    }
    else if (watch && ((!m_watchedNicks.contains(sgId) && wCount > 0) || 
        (!m_watchedNicks[sgId].contains(cId) && wCount > 0) ||
        (m_watchedNicks[sgId].contains(cId) && wCount > m_watchedNicks[sgId][cId][1].count())))
    {
        type = 1;
    }

    //TODO send the MONITOR command to the server
    //MONITOR + target[,target2]*

    //TODO send the WATCH command to the server
    //WATCH +target[ +target2]

    Server* server = m_connectionManager->getServerByConnectionId(cId);

    if (!m_watchedNicks.contains(sgId))
    {
        WatchedNicks wNicks = WatchedNicks();
        wNicks.insert(nick, online);
        WatchedNickList wNickList = WatchedNickList();
        wNickList.insert(type, wNicks);
        WatchedNickConnections wConnections = WatchedNickConnections();
        wConnections.insert(cId, wNickList);

        m_watchedNicks.insert(sgId, wConnections);
    }
    else if (m_watchedNicks.contains(sgId) && !m_watchedNicks[sgId].contains(cId))
    {
        WatchedNicks wNicks = WatchedNicks();
        wNicks.insert(nick, online);
        WatchedNickList wNickList = WatchedNickList();
        wNickList.insert(type, wNicks);

        m_watchedNicks[sgId].insert(cId, wNickList);
    }
    else if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId) && !m_watchedNicks[sgId][cId].contains(type))
    {
        WatchedNicks wNicks = WatchedNicks();
        wNicks.insert(nick, online);

        m_watchedNicks[sgId][cId].insert(type, wNicks);
    }
    else if (m_watchedNicks.contains(sgId) && m_watchedNicks[sgId].contains(cId) && m_watchedNicks[sgId][cId].contains(type))
    {
        m_watchedNicks[sgId][cId][type].insert(nick, online);
    }

    //TODO figure out a way to have the initial notify nicks added to monitor or watch
    // in the same message, rather than n times.
    if (type == 0)
        server->queue("MONITOR + " + nick, Server::LowPriority);
    else if (type == 1)
        server->queue("WATCH +" + nick, Server::LowPriority);
    else if (!m_timer->isActive())
    {
        m_timer->start(1000);
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
        Server* server = m_connectionManager->getServerByConnectionId(cId);

        if (m_watchedNicks[sgId][cId].contains(0) && m_watchedNicks[sgId][cId][0].contains(nick))
        {
            m_watchedNicks[sgId][cId][0].remove(nick);
            server->queue("MONITOR + " + nick, Server::LowPriority);
        }
        if (m_watchedNicks[sgId][cId].contains(1) && m_watchedNicks[sgId][cId][1].contains(nick))
        {
            m_watchedNicks[sgId][cId][1].remove(nick);
            server->queue("WATCH -" + nick, Server::LowPriority);
        }
        if (m_watchedNicks[sgId][cId].contains(2) && m_watchedNicks[sgId][cId][2].contains(nick))
        {
            m_watchedNicks[sgId][cId][2].remove(nick);
            // no need to send a removal command
        }

        // If there aren't any ISON's left turn off the timer

        if (!isWatchTypeEmpty(2))
            m_timer->stop();
    }
}

bool NicksOnlineFilterModel::isWatchTypeEmpty(int type) const
{
    WatchedNickListHash::const_iterator i;

    for (i = m_watchedNicks.constBegin(); i != m_watchedNicks.constEnd(); ++i)
    {
        WatchedNickConnections::const_iterator j;

        for (j = i.value().constBegin(); j != i.value().constEnd(); ++j)
        {
            if (j.value().contains(type))
            {
                return true;
            }
        }
    }

    return false;
}

void NicksOnlineFilterModel::isonCheck()
{
    //run by a timer

    //iterate over all server groups and send out messages on connections

    QStringList isonList = QStringList();

    WatchedNickListHash::const_iterator i;

    for (i = m_watchedNicks.constBegin(); i != m_watchedNicks.constEnd(); ++i)
    {
        WatchedNickConnections::const_iterator j;

        for (j = i.value().constBegin(); j != i.value().constEnd(); ++j)
        {
            if (j.value().contains(2) && j.value()[2].count() > 0)
            {
                QStringList isonList = QStringList();

                WatchedNicks::const_iterator k;

                for (k = j.value()[2].constBegin(); k != j.value()[2].constEnd(); ++k)
                {
                    isonList.append(k.key());
                }

                if (!isonList.isEmpty())
                {
                    Server* server = m_connectionManager->getServerByConnectionId(j.key());
                    // message must be less than 512 bytes, 'ISON <list><cr><lf>' leaving 505 for nicks and their separation
                    QStringList messages = QStringList();

                    if (isonList.join(" ").length() > 505)
                    {
                        QString message = QString();

                        QStringList::const_iterator m;
                        for (m = isonList.constBegin(); m != isonList.constEnd(); ++m)
                        {
                            // current length, + nick, + space(1), + end(2)
                            if (message.isEmpty())
                                message = "ISON " + (*m);

                            if (message.isEmpty() || (message.length() + (*m).length() + 3) > 505)
                            {
                                messages.append(message);

                                message = "ISON " + (*m);
                            }
                            else
                                message += " " + (*m);
                        }

                        if (!message.isEmpty())
                            messages.append(message);
                    }
                    else
                        messages.append("ISON " + isonList.join(" "));

                    server->queueList(messages, Server::LowPriority);
                }
            }
        }
    }
}

void NicksOnlineFilterModel::updateMinimumRowHeight()
{
    Images* images = Application::instance()->images();
    m_minimumRowHeight = images->getNickIcon(Images::Normal, false).height() + 2;
}

void NicksOnlineFilterModel::updateNotifyConnection(int sgId, int cId)
{
    if (m_connectionManager->getConnectedServerGroups().contains(sgId) && !m_watchedNicks.contains(sgId))
    {    //TODO get servergroup, add notify nicks to m_watchedNicks
        Server* server = m_connectionManager->getServerByConnectionId(cId);

        if (server && server->getServerGroup())
        {
            QStringList notifyList = server->getServerGroup()->notifyList();
            QStringList::const_iterator i;

            for (i = notifyList.constBegin(); i != notifyList.constEnd(); ++i)
            {
                addNotifyNick(sgId, cId, *i);
            }
        }
    }
    else if (!m_connectionManager->getConnectedServerGroups().contains(sgId) && m_watchedNicks.contains(sgId))
    {
        m_watchedNicks.remove(sgId);
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

    connect(m_nicksOnlineView, SIGNAL(activated(QModelIndex)), this, SLOT(activated(QModelIndex)));
    connect(m_nicksOnlineView->selectionModel(), SIGNAL(currentChanged(QModelIndex, QModelIndex)), this, SLOT(currentChanged(QModelIndex, QModelIndex)));
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
            sgId = index.data(ServerGroupIdRole).toInt();
        else if (Preferences::serverGroupList().count() > index.row())
            sgId = Preferences::serverGroupByIndex(index.row())->id();
    }

    EditNotifyDialog* end = new EditNotifyDialog(this, sgId);
    connect(end, SIGNAL(notifyChanged(int, QString)), this, SLOT(slotAddNickname(int, QString)));

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

    int sgId = index.data(ServerGroupIdRole).toInt();

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

    QString nick = index.data(NickRole).toString();

    //TODO open query with this nick or reuse existing one
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

void NicksOnline::activated(const QModelIndex& index)
{
    // open a query with the target if it's a nick, no action if it's a server

    if (!index.isValid() || !index.parent().isValid())
        return;

    QString nick = index.data(NickRole).toString();
    Q_UNUSED(nick);
    //find out if it has multiple connections

        //if it does prompt user to choose

        //if it doesn't open query window or reuse existing window

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
