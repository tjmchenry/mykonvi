/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2013 Travis McHenry <me@travisjmchenry.com>
*/

#include "nicklistmodel.h"
#include "application.h"
#include "preferences.h"
#include "connectionmanager.h"

#include <QTextCursor>

NickListModel::NickListModel(QObject* parent) : QAbstractListModel(parent)
{
    Application* konvApp = static_cast<Application*>(kapp);
    m_connectionManager = konvApp->getConnectionManager();

    m_hostmask = false;

    Images* images = Application::instance()->images();

    updateMinimumRowHeight();

    if(images->getNickIcon(Images::Normal, false).isNull())
    {
        m_whatsThis = i18n("<qt><p>This shows all the people in the channel.  The nick for each person is shown.<br />Usually an icon is shown showing the status of each person, but you do not seem to have any icon theme installed.  See the Konversation settings - <i>Configure Konversation</i> under the <i>Settings</i> menu.  Then view the page for <i>Themes</i> under <i>Appearance</i>.</p></qt>");

        m_icon = QPixmap();
    }
    else
    {
        m_whatsThis = i18n("<qt><p>This shows all the people in the channel.  The nick for each person is shown, with a picture showing their status.<br /></p>"
                "<table>"
                "<tr><th><img src=\"%1\"/></th><td>This person has administrator privileges.</td></tr>"
                "<tr><th><img src=\"%2\"/></th><td>This person is a channel owner.</td></tr>"
                "<tr><th><img src=\"%3\"/></th><td>This person is a channel operator.</td></tr>"
                "<tr><th><img src=\"%4\"/></th><td>This person is a channel half-operator.</td></tr>"
                "<tr><th><img src=\"%5\"/></th><td>This person has voice, and can therefore talk in a moderated channel.</td></tr>"
                "<tr><th><img src=\"%6\"/></th><td>This person does not have any special privileges.</td></tr>"
                "<tr><th><img src=\"%7\"/></th><td>This, overlaid on any of the above, indicates that this person is currently away.</td></tr>"
                "</table><p>"
                "The meaning of admin, owner and halfop varies between different IRC servers.</p><p>"
                "Hovering over any nick shows their current status. See the Konversation Handbook for more information."
                "</p></qt>",
                images->getNickIconPath(Images::Admin),
                images->getNickIconPath(Images::Owner),
                images->getNickIconPath(Images::Op),
                images->getNickIconPath(Images::HalfOp),
                images->getNickIconPath(Images::Voice),
                images->getNickIconPath(Images::Normal),
                images->getNickIconAwayPath());

        m_icon = images->getNickIcon(Images::Normal, false);
    }
}

NickListModel::~NickListModel()
{
}

void NickListModel::clear()
{
    m_nickHashes.clear();
    m_nickLists.clear();
    m_servers.clear();
}

void NickListModel::addServer(int connectionId)
{

    m_servers[connectionId] = QPersistentModelIndex();
    m_nickHashes.insert(connectionId, NickHash());
    m_nickLists.insert(connectionId, QList<Nick2*>());

    int position = m_servers.keys().indexOf(connectionId);
    QModelIndex index = NickListModel::createIndex(position, 0, -1);
    QPersistentModelIndex serverIndex = QPersistentModelIndex(index);

    m_servers[connectionId] = serverIndex;
}

void NickListModel::removeServer(int connectionId)
{
    if (m_servers.contains(connectionId))
    {
        m_servers.remove(connectionId);
        m_nickHashes.remove(connectionId);
        m_nickLists.remove(connectionId);
    }
}

/* insert without resetting the model */
void NickListModel::insertNick(int connectionId, Nick2* nick)
{
    if (m_servers.contains(connectionId))
    {
        uint position = m_nickLists[connectionId].count();
        beginInsertRows(m_servers[connectionId], position, position);
        m_nickLists[connectionId].append(nick);
        m_nickHashes[connectionId].insert(nick->getLoweredNickname(), nick);
        endInsertRows();

        connect(nick, SIGNAL(nickChanged(int,QString,QVector<int>,QVector<int>)), this, SLOT(slotNickChanged(int,QString,QVector<int>,QVector<int>)));

        //TODO have nicks added on join through input filter instead of current channel/server
        //mishmash. use signals / slots to connect them
        if (isNotifyNick(connectionId, nick->getNickname()))
        {
            //TODO this will need to be different for meta contacts..
            int sgId = m_connectionManager->getServerByConnectionId(connectionId)->getServerGroup()->id();
            emit nickOnline(sgId, connectionId, nick->getNickname());
        }
    }
}

void NickListModel::insertNicksFromNames(int connectionId, const QString& channel, const QStringList& namesList)
{
    if (m_servers.contains(connectionId))
    {
        QStringList::const_iterator i;

        for (i = namesList.constBegin(); i != namesList.constEnd(); ++i)
        {
            QString nickname = *i;
            bool admin = false;
            bool owner = false;
            bool op = false;
            bool halfop = false;
            bool voice = false;

            //This strips the mode chars off of the nick and sets the appropriate bools.
            m_connectionManager->getServerByConnectionId(connectionId)->mangleNicknameWithModes(nickname, admin, owner, op, halfop, voice);

            addNickToChannel(connectionId, channel, nickname);

            Nick2* nick = getNick(connectionId, nickname);

            nick->setMode(channel, admin, owner, op, halfop, voice);
        }
    }
}

void NickListModel::addNickToChannel(int connectionId, const QString& channel, const QString& nick)
{
    if (m_servers.contains(connectionId))
    {
        if (!isNickOnline(connectionId, nick))
        {
            Nick2* newNick;

            if (isNotifyNick(connectionId, nick) && m_connectionManager->getNicksOnlineFilterModel()->isWatchedNickOnline(connectionId, nick))
            {
                newNick = m_connectionManager->getNicksOnlineFilterModel()->getWatchedNick(connectionId, nick);
            }
            else
            {
                newNick = new Nick2(connectionId, nick);
            }

            insertNick(connectionId, newNick);
        }

        QString lcNick = nick.toLower();

        m_nickHashes[connectionId][lcNick]->addChannel(channel);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][lcNick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);
        emit dataChanged(index, index); //changes tooltips in existing channels, and all roles in new channel
    }
}

void NickListModel::removeNick(int connectionId, const QString& nick)
{
    if (isNickOnline(connectionId, nick))
    {
        QString lcNick = nick.toLower();

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][lcNick]);

        beginRemoveRows(m_servers[connectionId], position, position);
        Nick2* nickObject = m_nickHashes[connectionId].take(lcNick);
        m_nickLists[connectionId].removeAt(position);
        endRemoveRows();

        nickObject->disconnect(this);

        if (isNotifyNick(connectionId, nick))
        {
            int sgId = m_connectionManager->getServerByConnectionId(connectionId)->getServerGroup()->id();
            emit nickOffline(sgId, connectionId, nickObject);
        }
        else
            delete nickObject;

    }
}

void NickListModel::removeNickFromChannel(int connectionId, const QString& channel, const QString& nick)
{
    if (isNickOnline(connectionId, nick))
    {
        QString lcNick = nick.toLower();

        m_nickHashes[connectionId][lcNick]->removeChannel(channel);

        if(!m_nickHashes[connectionId][lcNick]->isInAnyChannel())
        {
            removeNick(connectionId, nick);
        }
        else
        {
            uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][lcNick]);
            QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);
            emit dataChanged(index, index); //changes tooltips in existing channels, and all roles in removed channel
        }
    }
}

void NickListModel::removeAllNicksFromChannel(int connectionId, const QString& channel)
{
    if (!m_nickHashes[connectionId].isEmpty())
    {
        NickHash::iterator i = m_nickHashes[connectionId].begin();

        while (i != m_nickHashes[connectionId].end())
        {
            i.value()->removeChannel(channel);

            uint position = m_nickLists[connectionId].indexOf(i.value());
            if (!i.value()->isInAnyChannel())
            {
                Nick2* nick = i.value();

                beginRemoveRows(m_servers[connectionId], position, position);
                i = m_nickHashes[connectionId].erase(i);
                m_nickLists[connectionId].removeAt(position);
                endRemoveRows();

                nick->disconnect(this);

                if (isNotifyNick(connectionId, nick->getNickname()))
                {
                    int sgId = m_connectionManager->getServerByConnectionId(connectionId)->getServerGroup()->id();
                    emit nickOffline(sgId, connectionId, nick);
                }
                else
                    delete nick;
            }
            else
            {
                QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);
                emit dataChanged(index, index); //changes tooltips in existing channels, and all roles in removed channel

                ++i;
            }
        }
    }
}

Nick2* NickListModel::getNick(int connectionId, const QString& nick)
{
    QString lcNick = nick.toLower();

    if (m_servers.contains(connectionId) && m_nickHashes[connectionId].contains(lcNick))
        return m_nickHashes[connectionId][lcNick];

    return 0;
}

QStringList NickListModel::getSharedChannels(int connectionId, const QString& nick)
{
    Server* server = m_connectionManager->getServerByConnectionId(connectionId);

    if (!server)
        return QStringList();

    QStringList channelList = getNick(connectionId, nick)->getChannels();
    QStringList ourChannelList = getNick(connectionId, server->getNickname())->getChannels();

    QStringList sharedChannelsList = QStringList();
    QStringList::ConstIterator i;

    for (i = ourChannelList.constBegin(); i != ourChannelList.constEnd(); ++i)
    {
        if (channelList.contains(*i))
            sharedChannelsList.append(*i);
    }

    return sharedChannelsList;
}

void NickListModel::setNewNickname(int connectionId, const QString& nick, const QString& newNick)
{
    QString lcNick = nick.toLower();
    QString lcNewNick = nick.toLower();

    if (!newNick.isEmpty() && isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][lcNick]->setNickname(newNick);
        m_nickHashes[connectionId][lcNewNick] = m_nickHashes[connectionId][lcNick];

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][lcNick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        m_nickHashes[connectionId].remove(lcNick);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

bool NickListModel::isNickOnline(int connectionId, const QString& nick) const
{
    if (m_nickHashes.contains(connectionId))
        return m_nickHashes[connectionId].contains(nick.toLower());

    return false;
}

bool NickListModel::isNotifyNick(int connectionId, const QString& nick) const
{
    Server* server = m_connectionManager->getServerByConnectionId(connectionId);

    if (server)
        return server->isWatchedNick(nick);

    return false;
}

void NickListModel::setAllChannelNicksLessActive(int connectionId, const QString& channel)
{
    if (!m_nickHashes[connectionId].isEmpty())
    {
        NickHash::const_iterator i = m_nickHashes[connectionId].constBegin();
        uint position = 0;

        for (i = m_nickHashes[connectionId].constBegin(); i != m_nickHashes[connectionId].constEnd(); ++i)
        {
            if (i.value()->isInChannel(channel))
                i.value()->lessActive(channel);

            position++;
        }
    }
}

void NickListModel::slotNickChanged(int connectionId, const QString& nick, QVector<int> columnsChanged, QVector<int> rolesChanged)
{
    Q_UNUSED(rolesChanged);

    if (!columnsChanged.contains(0) && !columnsChanged.contains(1))
        return;

    QString lcNick = nick.toLower();

    if (isNickOnline(connectionId, nick))
    {
        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][lcNick]);

        QModelIndex startIndex;
        QModelIndex lastIndex;

        if (columnsChanged.contains(0))
        {
            startIndex = NickListModel::index(position, 0, m_servers[connectionId]);

            if (!m_hostmask || !columnsChanged.contains(1))
                lastIndex = startIndex;
            else
                lastIndex = startIndex.sibling(startIndex.row(), 1);
        }
        else if (m_hostmask && columnsChanged.contains(1))
        {
            startIndex = NickListModel::index(position, 1, m_servers[connectionId]);
            lastIndex = startIndex;
        }

        if (startIndex.isValid() && lastIndex.isValid())
            //TODO when we can dep Qt 5 we can specify what roles have changed.
            emit dataChanged(startIndex, lastIndex); //, rolesChanged);
    }
}

QPersistentModelIndex NickListModel::serverIndex(int connectionId) const
{
    if (m_servers.contains(connectionId))
        return m_servers[connectionId];

    return QPersistentModelIndex();
}

int NickListModel::getConnectionIdFromRow(int row) const
{
    if (m_servers.count() > row)
        return m_servers.keys().at(row);

    return -1;
}

QModelIndex NickListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column < 0)
        return QModelIndex();

    if (row >= rowCount(parent) || column >= columnCount())
        return QModelIndex();

    if (parent == QModelIndex())
    {
        QMap<int, QPersistentModelIndex>::const_iterator i = m_servers.constBegin();
        int position = 0;
        while (i != m_servers.constEnd() && position < row)
        {
            position++;
            i++;
        }
        return i.value();
    }

    return createIndex(row, column, getConnectionIdFromRow(parent.row()));
}

QModelIndex NickListModel::parent(const QModelIndex& index) const
{
    if (index.isValid())
    {
        if (index.internalId() < 0)
            return QModelIndex();
        else
        {
            if (m_servers.contains(index.internalId()))
                return m_servers[index.internalId()];
        }
    }

    return QModelIndex();
}

QVariant NickListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    //FIXME there's gotta be a better way to hide the header
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);
    return QVariant();
}

int NickListModel::columnCount(const QModelIndex& /*parent*/) const
{
    if (m_hostmask) return 2;

    return 1;
}

int NickListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        if (parent.internalId() < 0)
        {
            int cId = getConnectionIdFromRow(parent.row());

            if (m_nickLists.contains(cId))
                return m_nickLists[cId].count();
            else
                return 0;
        }
        else
            return 0;
    }
    else
        return m_servers.count();
}

QVariant NickListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount(index.parent()))
        return QVariant();

    if (!index.parent().isValid() && index.internalId() < 0) //top level item
    {
        if (role == Qt::DisplayRole)
        {
            Server* server = m_connectionManager->getServerByConnectionId(getConnectionIdFromRow(index.row()));
            if (server)
                return server->getServerName();
            else
                return QVariant();
        }
        else
            return QVariant();
    }

    Nick2* nick = m_nickLists[index.internalId()].at(index.row());

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
            case 0:
                return nick->getNickname(); //TODO special cases for what to display here
            case 1:
                if (m_hostmask)
                    return nick->getHostmask();
                else
                    return QVariant();
            default:
                return QVariant();
        }
    }
    else if (role == Qt::SizeHintRole)
    {
        //TODO Width here is arbitrary, find some way to get a meaningful width.
        return QSize(30, m_minimumRowHeight);
    }
    else if (role == Qt::ToolTipRole)
    {
        return nick->getQueryTooltip();
    }
    else if (role == Qt::DecorationRole)
    {
        return m_icon;
    }
    else if (role == Qt::WhatsThisRole)
    {
        return m_whatsThis;
    }
    else if (role == NickRole)
    {
        return nick->getNickname();
    }
    else if (role == LoweredNickRole)
    {
        return nick->getLoweredNickname();
    }
    else if (role == HostmaskRole)
    {
        return nick->getHostmask();
    }
    else if (role == LoweredHostmaskRole)
    {
        return nick->getLoweredHostmask();
    }
    else if (role == AwayRole)
    {
        return nick->isAway();
    }
    else if (role == ChannelsRole)
    {
        return nick->getChannels();
    }
    else if (role == ChannelPropertiesRole)
    {
        return QVariant::fromValue(nick->getChannelProperties());
    }

    return QVariant();
}

Qt::ItemFlags NickListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return 0;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool NickListModel::hasChildren(const QModelIndex& index) const
{
    if (index.isValid() && !index.parent().isValid() && index.internalId() < 0)
        return (m_nickLists[getConnectionIdFromRow(index.row())].count() > 0);

    else if(!index.isValid())
        return (m_servers.count() > 0);

    return false;
}

QHash<int, QByteArray> NickListModel::roleNames() const
{
    //TODO do rolenames
    QHash<int, QByteArray> roles;

    roles[NickRole] = "nick";
    roles[LoweredNickRole] = "lowerednick";
    roles[HostmaskRole] = "hostmask";
    roles[LoweredHostmaskRole] = "loweredhostmask";
    roles[AwayRole] = "away";
    roles[ChannelsRole] = "channels";
    roles[ChannelPropertiesRole] = "channelproperties";

    return roles;
}

//TODO when would we need to call this?
void NickListModel::updateMinimumRowHeight()
{
    Images* images = Application::instance()->images();
    m_minimumRowHeight = images->getNickIcon(Images::Normal, false).height() + 2;
}

struct timestampLessThanSort
{
    ChannelNickListFilterModel* This;
    timestampLessThanSort(ChannelNickListFilterModel* newThis)
    {
        This = newThis;
    }
    bool operator()(const QModelIndex& left, const QModelIndex& right)
    {
        return This->nickTimestampLessThan(left, right);
    }
};

ChannelNickListFilterModel::ChannelNickListFilterModel(int connectionId, Channel* channel) : QSortFilterProxyModel(channel)
{
    //nick list model filters
    if (channel)
    {
        m_channel = channel;
        m_channelName = m_channel->getName();
    }
    else
    {
        //This means we're in a query, and our sortfilter stuff applies to -all- nicks.
        m_channel = 0;
        m_channelName = QString();
    }

    m_connectionId = connectionId;

    QSortFilterProxyModel::setDynamicSortFilter(true);
    QSortFilterProxyModel::sort(0, Qt::AscendingOrder);
}

ChannelNickListFilterModel::~ChannelNickListFilterModel()
{
}

void ChannelNickListFilterModel::insertNick(Nick2* item)
{
    if (sourceNickModel() && sourceNickModel()->isNickOnline(m_connectionId, item->getNickname()))
    {
        sourceNickModel()->insertNick(m_connectionId, item);

        sourceNickModel()->addNickToChannel(m_connectionId, m_channelName, item->getNickname());
    }
}

void ChannelNickListFilterModel::removeNick(const QString& nick)
{
    if (sourceNickModel())
        sourceNickModel()->removeNickFromChannel(m_connectionId, m_channelName, nick);
}

void ChannelNickListFilterModel::removeAllNicks()
{
    if (sourceNickModel())
        sourceNickModel()->removeAllNicksFromChannel(m_connectionId, m_channelName);
}

void ChannelNickListFilterModel::setAllNicksLessActive()
{
    if (sourceNickModel())
        sourceNickModel()->setAllChannelNicksLessActive(m_connectionId, m_channelName);
}

NickListModel* ChannelNickListFilterModel::sourceNickModel() const
{
        return static_cast<NickListModel*>(sourceModel());
}

QModelIndex ChannelNickListFilterModel::serverIndex() const
{
    return mapFromSource(sourceNickModel()->serverIndex(m_connectionId));
}

QVariant ChannelNickListFilterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount(serverIndex()))
        return QVariant();

    QModelIndex srcIndex = mapToSource(index);

    if (!srcIndex.isValid())
        return QVariant();

    if (m_channel && index.parent().isValid() && (role == Qt::ToolTipRole || role == Qt::DecorationRole || role == Qt::ForegroundRole || role >= ActivityRole))
    {
        switch (role)
        {
            case Qt::ToolTipRole:
                return getProperty(srcIndex, "tooltip");

            case Qt::DecorationRole:
                return getProperty(srcIndex, "icon");

            case Qt::ForegroundRole:
                if (srcIndex.data(NickListModel::AwayRole).toBool())
                    return qApp->palette("QListView").brush(QPalette::Disabled, QPalette::Text);
                else
                    return QVariant();

            case ActivityRole:
                return getProperty(srcIndex, "activity");

            case StatusValueRole:
                return getProperty(srcIndex, "statusValue");

            case TimestampRole:
                return getProperty(srcIndex, "timestamp");

            //case ModesRole:
              //  return getProperty(srcIndex, "modes");

            default:
                return srcIndex.data(role);
        }
    }

    return srcIndex.data(role);
}

QVariant ChannelNickListFilterModel::getProperty(const QModelIndex& sourceIndex, const QString& property) const
{
    ChannelProperties* properties = sourceIndex.data(NickListModel::ChannelPropertiesRole).value<const ChannelHash*>()->value(m_channelName);

    return properties->value(property);
}

Nick2* ChannelNickListFilterModel::getNick(const QString& nick)
{
    if (sourceNickModel())
        return sourceNickModel()->getNick(m_connectionId, nick);

    return NULL;
}

void ChannelNickListFilterModel::nickCompletion(IRCInput* inputBar)
{

    int pos, oldPos;
    QTextCursor cursor = inputBar->textCursor();

    pos = cursor.position();
    oldPos = inputBar->getOldCursorPosition();

    QString line = inputBar->toPlainText();
    QString newLine;
    // Check if completion position is out of range
    if (m_completionPosition >= rowCount(serverIndex())) m_completionPosition = 0;

    // Check, which completion mode is active
    char mode = inputBar->getCompletionMode();

    if (mode == 'c')
    {
        line.remove(oldPos, pos - oldPos);
        pos = oldPos;
    }

    // If the cursor is at beginning of line, insert last completion if the nick is still around
    if (pos == 0 && !inputBar->lastCompletion().isEmpty() && getNick(inputBar->lastCompletion())->isInChannel(m_channelName))
    {
        QString addStart(Preferences::self()->nickCompleteSuffixStart());
        newLine = inputBar->lastCompletion() + addStart;
        // New cursor position is behind nickname
        pos = newLine.length();
        // Add rest of the line
        newLine += line;
    }
    else
    {
        // remember old cursor position in input field
        inputBar->setOldCursorPosition(pos);
        // remember old cursor position locally
        oldPos = pos;
        // step back to last space or start of line
        while(pos && line[pos-1] != ' ') pos--;
        // copy search pattern (lowercase)
        QString pattern = line.mid(pos, oldPos - pos);
        // copy line to newLine-buffer
        newLine = line;

        // did we find any pattern?
        if(!pattern.isEmpty())
        {
            bool complete = false;
            QString foundNick;

            // try to find matching nickname in list of names
            if(Preferences::self()->nickCompletionMode() == 1 ||
                Preferences::self()->nickCompletionMode() == 2)
            { // Shell like completion
                QStringList found;

                foundNick = completeNick(pattern, complete, found,
                                        (Preferences::self()->nickCompletionMode() == 2),
                                         Preferences::self()->nickCompletionCaseSensitive());

                if(!complete && !found.isEmpty())
                {
                    if(Preferences::self()->nickCompletionMode() == 1)
                    {
                        QString nicksFound = found.join(" ");
                        m_channel->appendServerMessage(i18n("Completion"), i18n("Possible completions: %1.", nicksFound));
                    }
                    else
                    {
                        inputBar->showCompletionList(found);
                    }
                }
            } // Cycle completion
            else if(Preferences::self()->nickCompletionMode() == 0 && rowCount(serverIndex()) > 0)
            {
                if(mode == '\0') {
                    uint timestamp = 0;
                    int listPosition = 0;

                    for (int i = 0; i < rowCount(serverIndex()); i++)
                    {
                        QModelIndex index = ChannelNickListFilterModel::index(i, 0, serverIndex());

                        if(index.data(NickListModel::NickRole).toString().startsWith(pattern, Preferences::self()->nickCompletionCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive) &&
                          (index.data(TimestampRole).toUInt() > timestamp))
                        {
                            timestamp = index.data(TimestampRole).toUInt();
                            m_completionPosition = listPosition;
                        }
                        ++listPosition;
                    }
                }

                // remember old nick completion position
                int oldCompletionPosition = m_completionPosition;
                complete = true;
                QString prefixCharacter = Preferences::self()->prefixCharacter();

                do
                {
                    QString lookNick = ChannelNickListFilterModel::index(m_completionPosition, 0, serverIndex()).data(NickListModel::NickRole).toString();

                    if(!prefixCharacter.isEmpty() && lookNick.contains(prefixCharacter))
                    {
                        lookNick = lookNick.section( prefixCharacter,1 );
                    }

                    if(lookNick.startsWith(pattern, Preferences::self()->nickCompletionCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive))
                    {
                        foundNick = lookNick;
                    }

                    // increment search position
                    m_completionPosition++;

                    // wrap around
                    if(m_completionPosition == rowCount(serverIndex()))
                    {
                        m_completionPosition = 0;
                    }

                    // the search ends when we either find a suitable nick or we end up at the
                    // first search position
                } while((m_completionPosition != oldCompletionPosition) && foundNick.isEmpty());
            }

            // did we find a suitable nick?
            if(!foundNick.isEmpty())
            {
                // set channel nicks completion mode
                inputBar->setCompletionMode('c');

                // remove pattern from line
                newLine.remove(pos, pattern.length());

                // did we find the nick in the middle of the line?
                if(pos && complete)
                {
                    inputBar->setLastCompletion(foundNick);
                    QString addMiddle = Preferences::self()->nickCompleteSuffixMiddle();
                    newLine.insert(pos, foundNick + addMiddle);
                    pos = pos + foundNick.length() + addMiddle.length();
                }
                // no, it was at the beginning
                else if(complete)
                {
                    inputBar->setLastCompletion(foundNick);
                    QString addStart = Preferences::self()->nickCompleteSuffixStart();
                    newLine.insert(pos, foundNick + addStart);
                    pos = pos + foundNick.length() + addStart.length();
                }
                // the nick wasn't complete
                else
                {
                    newLine.insert(pos, foundNick);
                    pos = pos + foundNick.length();
                }
            }
            // no pattern found, so restore old cursor position
            else pos = oldPos;
        }
    }

    // Set new text and cursor position
    inputBar->setText(newLine);
    cursor.setPosition(pos);
    inputBar->setTextCursor(cursor);
}

QString ChannelNickListFilterModel::completeNick(const QString& pattern, bool& complete, QStringList& found,
                                                bool skipNonAlfaNum, bool caseSensitive)
{
    found.clear();
    QString prefix('^');
    QString prefixCharacter = Preferences::self()->prefixCharacter();
    QList<QModelIndex> foundNicks;

    if((pattern.contains(QRegExp("^(\\d|\\w)"))) && skipNonAlfaNum)
    {
        prefix = "^([^\\d\\w]|[\\_]){0,}";
    }

    QRegExp regexp(prefix + QRegExp::escape(pattern));
    regexp.setCaseSensitivity(caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);

    for (int i = 0; i < rowCount(serverIndex()); ++i)
    {
        QModelIndex index = ChannelNickListFilterModel::index(i, 0, serverIndex());
        QString nickName = index.data(NickListModel::NickRole).toString();

        if (!prefix.isEmpty() && nickName.contains(prefixCharacter))
        {
            nickName = nickName.section(prefixCharacter, 1);
        }

        if (nickName.contains(regexp))
        {
            foundNicks.append(index);
        }
    }

    qSort(foundNicks.begin(), foundNicks.end(), ::timestampLessThanSort(this));

    QList<QModelIndex>::const_iterator i;

    for (i = foundNicks.constBegin(); i != foundNicks.constEnd(); ++i)
        found.append((*i).data(NickListModel::NickRole).toString());

    if(found.count() > 1)
    {
        bool ok = true;
        int patternLength = pattern.length();
        QString firstNick = found[0];
        int firstNickLength = firstNick.length();
        int foundCount = found.count();

        while(ok && ((patternLength) < firstNickLength))
        {
            ++patternLength;
            QStringList tmp = found.filter(firstNick.left(patternLength), caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);

            if(tmp.count() != foundCount)
            {
                ok = false;
                --patternLength;
            }
        }

        complete = false;
        return firstNick.left(patternLength);
    }
    else if(found.count() == 1)
    {
        complete = true;
        return found[0];
    }

    return QString();
}

void ChannelNickListFilterModel::endNickCompletion()
{
    if (m_completionPosition)
        m_completionPosition--;
    else
        m_completionPosition = rowCount(serverIndex()) - 1;
}

bool ChannelNickListFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (sourceNickModel())
    {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

        if (index.isValid())
        {
            if (index.internalId() < 0 && sourceNickModel()->getConnectionIdFromRow(index.row()) == m_connectionId && sourceModel()->hasChildren(sourceParent))
                return true;
            if (index.internalId() == m_connectionId)
                return index.data(NickListModel::ChannelsRole).toStringList().contains(m_channelName);
        }
    }

    return false;
}

bool ChannelNickListFilterModel::lessThan(const QModelIndex& sourceLeft, const QModelIndex& sourceRight) const
{
    if (!sourceLeft.isValid() || !sourceRight.isValid())
        return false;

    if(Preferences::self()->sortByActivity())
    {
        return nickActivityLessThan(sourceLeft, sourceRight);
    }
    else if(Preferences::self()->sortByStatus())
    {
        return nickStatusLessThan(sourceLeft, sourceRight);
    }
    else
    {
        return nickLessThan(sourceLeft, sourceRight);
    }
}

bool ChannelNickListFilterModel::nickTimestampLessThan(const QModelIndex& left, const QModelIndex& right) const
{
    int difference = getProperty(left, "timestamp").toUInt() - getProperty(right, "timestamp").toUInt();

    if (difference != 0)
        return (difference < 0) ? false : true;

    if (Preferences::self()->sortByStatus())
        return nickStatusLessThan(left, right);

    return nickLessThan(left, right);
}

bool ChannelNickListFilterModel::nickLessThan(const QModelIndex& left, const QModelIndex& right) const
{
    if (Preferences::self()->sortCaseInsensitive())
    {
        if (left.data(NickListModel::LoweredNickRole).toString() != right.data(NickListModel::LoweredNickRole).toString())
            return left.data(NickListModel::LoweredNickRole).toString() < right.data(NickListModel::LoweredNickRole).toString();

        return nickHostmaskLessThan(left, right);
    }
    else
    {
        if (left.data(NickListModel::NickRole).toString() != right.data(NickListModel::NickRole).toString())
            return left.data(NickListModel::NickRole).toString() < right.data(NickListModel::NickRole).toString();

        return nickHostmaskLessThan(left, right);
    }
}

bool ChannelNickListFilterModel::nickHostmaskLessThan(const QModelIndex& left, const QModelIndex& right) const
{
    if (Preferences::self()->sortCaseInsensitive())
    {
        return left.data(NickListModel::LoweredHostmaskRole).toString() < right.data(NickListModel::LoweredHostmaskRole).toString();
    }

    return left.data(NickListModel::HostmaskRole).toString() < right.data(NickListModel::HostmaskRole).toString();
}

bool ChannelNickListFilterModel::nickActivityLessThan(const QModelIndex& left, const QModelIndex& right) const
{
    int difference = getProperty(left, "activity").toUInt() - getProperty(right, "activity").toUInt();

    if (difference != 0)
        return (difference < 0) ? false : true;

    return nickTimestampLessThan(left, right);
}

bool ChannelNickListFilterModel::nickStatusLessThan(const QModelIndex& left, const QModelIndex& right) const
{
    int difference = getProperty(left, "statusValue").toInt() - getProperty(right, "statusValue").toInt();

    if (difference != 0)
        return (difference > 0) ? false : true;

    return nickLessThan(left, right);
}

#include "nicklistmodel.moc"
