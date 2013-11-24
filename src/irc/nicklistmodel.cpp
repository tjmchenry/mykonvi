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
    //m_nickHashes = QHash<int, NickHash>();
    //m_nickLists = QHash<int, QList<Nick2*> >();
   // m_servers = QMap<int, QPersistentModelIndex>();
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
    QMap<int, QPersistentModelIndex>::const_iterator i = m_servers.find(connectionId);
    int position = 0;
    while (i != m_servers.constBegin())
    {
        position++;
        i--;
    }
    QModelIndex index = NickListModel::createIndex(position, 0, (quint32)connectionId);
    QPersistentModelIndex serverIndex = QPersistentModelIndex(index);
    m_servers[index.internalId()] = serverIndex;
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
void NickListModel::insertNick(int connectionId, Nick2* item)
{
    if (m_servers.contains(connectionId))
    {
        uint position = m_nickLists[connectionId].count();
        beginInsertRows(m_servers[connectionId], position, position);
        m_nickLists[connectionId].append(item);
        m_nickHashes[connectionId].insert(item->getNickname(), item);
        endInsertRows();
    }
}

void NickListModel::addNickToChannel(int connectionId, const QString& channel, const QString& nick)
{
    if (m_servers.contains(connectionId))
    {
        if (!isNickOnline(connectionId, nick))
        {
            Nick2* newNick = new Nick2(connectionId, nick);
            insertNick(connectionId, newNick);
        }

        m_nickHashes[connectionId][nick]->addChannel(channel);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);
        emit dataChanged(index, index); //changes tooltips in existing channels, and all roles in new channel
    }
}

void NickListModel::removeNick(int connectionId, const QString& nick)
{
    if (isNickOnline(connectionId, nick))
    {
        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);

        beginRemoveRows(m_servers[connectionId], position, position);
        m_nickHashes[connectionId].remove(nick);
        m_nickLists[connectionId].removeAt(position);
        endRemoveRows();
    }
}

void NickListModel::removeNickFromChannel(int connectionId, const QString& channel, const QString& nick)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->removeChannel(channel);

        if(!m_nickHashes[connectionId][nick]->isInAnyChannel())
        {
            removeNick(connectionId, nick);
        }
        else
        {
            uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
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
                beginRemoveRows(m_servers[connectionId], position, position);
                i = m_nickHashes[connectionId].erase(i);
                m_nickLists[connectionId].removeAt(position);
                endRemoveRows();
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

QPersistentModelIndex NickListModel::serverIndex(int connectionId)
{
    if (m_servers.contains(connectionId))
        return m_servers[connectionId];

    return QPersistentModelIndex();
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
        //return createIndex(row, column, (quint32)i.key());
    }

    return createIndex(row, column, m_nickLists[parent.internalId()].at(row));
}

QModelIndex NickListModel::parent(const QModelIndex& index) const
{
    if (index.isValid())
    {
        if (m_servers.contains(index.internalId()))
            return QModelIndex();
        else
        {
            Nick2* item = static_cast<Nick2*>(index.internalPointer());
            if (item && m_servers.contains(item->getConnectionId()))
                return m_servers[item->getConnectionId()];
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
        if (m_servers.contains(parent.internalId()))
            return m_nickLists[parent.internalId()].count();
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

    if (!index.parent().isValid() && m_servers.contains(index.internalId())) //top level item
    {
        if (role == Qt::DisplayRole)
        {
            Application* konvApp = static_cast<Application*>(kapp);
            Server* server = konvApp->getConnectionManager()->getServerByConnectionId(index.internalId());
            if (server)
                return server->getServerName();
            else
                return QVariant();
        }
        else
            return QVariant();
    }

    Nick2* item = static_cast<Nick2*>(index.internalPointer());

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
            case 0:
                return item->getNickname(); //TODO special cases for what to display here
            case 1:
                if (m_hostmask)
                    return item->getHostmask();
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
        return item->getQueryTooltip();
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
        return item->getNickname();
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
    if (index.isValid() && !index.parent().isValid() && m_servers.contains(index.internalId()))
        return (m_nickLists[index.internalId()].count() > 0);

    else if(!index.isValid())
        return (m_servers.count() > 0);

    return false;
}

QHash<int, QByteArray> NickListModel::roleNames() const
{
    //TODO do rolenames
    QHash<int, QByteArray> roles;

    roles[NickRole] = "nick";
    roles[HostmaskRole] = "hostmask";

    return roles;
}

//TODO when would we need to call this?
void NickListModel::updateMinimumRowHeight()
{
    Images* images = Application::instance()->images();
    m_minimumRowHeight = images->getNickIcon(Images::Normal, false).height() + 2;
}

bool NickListModel::isNickOnline(int connectionId, const QString& nick) const
{
    if (m_nickHashes.contains(connectionId))
        return m_nickHashes[connectionId].contains(nick);

    return false;
}

bool NickListModel::isNickIdentified(int connectionId, const QString& nick) const
{
    if (isNickOnline(connectionId, nick))
        return m_nickHashes[connectionId][nick]->isIdentified();

    return false;
}

QStringList NickListModel::getNickChannels(int connectionId, const QString& nick) const
{
    if (isNickOnline(connectionId, nick))
        return m_nickHashes[connectionId][nick]->getChannels();

    return QStringList();
}

bool NickListModel::isNickInChannel(int connectionId, const QString& channel, const QString& nick) const
{
    if (isNickOnline(connectionId, nick))
        return m_nickHashes[connectionId][nick]->isInChannel(channel);

    return false;
}

bool NickListModel::isNickAnyTypeOfOp(int connectionId, const QString& channel, const QString& nick) const
{
    if (isNickOnline(connectionId, nick) && isNickInChannel(connectionId, channel, nick))
        return m_nickHashes[connectionId][nick]->isAnyTypeOfOp(channel);

    return false;
}


//hostmask
QString NickListModel::getNickHostmask(int connectionId, const QString& nick) const
{
    if (isNickOnline(connectionId, nick))
        return m_nickHashes[connectionId][nick]->getHostmask();

    return QString();
}

void NickListModel::setNickHostmask(int connectionId, const QString& nick, const QString& hostmask)
{
    if (!hostmask.isEmpty() && isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setHostmask(hostmask);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

void NickListModel::setNickRealName(int connectionId, const QString& nick, const QString& realName)
{
    if (!realName.isEmpty() && isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setRealName(realName);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

void NickListModel::setNewNickname(int connectionId, const QString& nick, const QString& newNick)
{
    if (!newNick.isEmpty() && isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setNickname(newNick);
        m_nickHashes[connectionId][newNick] = m_nickHashes[connectionId][nick];

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        m_nickHashes[connectionId].remove(nick);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

void NickListModel::setNickOnlineSince(int connectionId, const QString& nick, const QDateTime& onlineSince)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setOnlineSince(onlineSince);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

void NickListModel::setNickNetServer(int connectionId, const QString& nick, const QString& netServer)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setNetServer(netServer);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

void NickListModel::setNickNetServerInfo(int connectionId, const QString& nick, const QString& netServerInfo)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setNetServerInfo(netServerInfo);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

//activity
uint NickListModel::getNickActivity(int connectionId, const QString& channel, const QString& nick) const
{
    if (isNickOnline(connectionId, nick))
        return m_nickHashes[connectionId][nick]->getRecentActivity(channel);

    return 0;
}

void NickListModel::setNickMoreActive(int connectionId, const QString& channel, const QString& nick)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->moreActive(channel);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

void NickListModel::setAllChannelNicksLessActive(int connectionId, const QString& channel)
{
    if (!m_nickHashes[connectionId].isEmpty())
    {
        NickHash::const_iterator i = m_nickHashes[connectionId].constBegin();
        uint position = 0;

        for (i = m_nickHashes[connectionId].constBegin(); i != m_nickHashes[connectionId].constEnd(); ++i)
        {
            if (isNickInChannel(connectionId, i.key(), channel))
            {
                i.value()->lessActive(channel);
                QModelIndex index = NickListModel::index(m_nickLists[connectionId].indexOf(i.value()), 0, m_servers[connectionId]);

                emit dataChanged(index, index);
            }

            position++;
        }
    }
}

//timestamp
uint NickListModel::getNickTimestamp(int connectionId, const QString& channel, const QString& nick) const
{
    if (isNickOnline(connectionId, nick))
        return m_nickHashes[connectionId][nick]->getTimestamp(channel);

    return 0;
}

//status
uint NickListModel::getNickStatusValue(int connectionId, const QString& channel, const QString& nick) const
{
    if (isNickOnline(connectionId, nick))
        return m_nickHashes[connectionId][nick]->getStatusValue(channel);

    return 0;
}

void NickListModel::setNickMode(int connectionId, const QString& channel, const QString& nick, unsigned int mode)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setMode(channel, mode);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DecorationRole);
    }
}

void NickListModel::setNickMode(int connectionId, const QString& channel, const QString& nick, char mode, bool state)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setMode(channel, mode, state);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DecorationRole);
    }
}

void NickListModel::setNickAway(int connectionId, const QString& nick, bool away, const QString& awayMessage)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setAway(away, awayMessage);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DecorationRole << Qt::DisplayRole);
    }
}

void NickListModel::setNickIdentified(int connectionId, const QString& nick, bool identified)
{
    if (isNickOnline(connectionId, nick))
    {
        m_nickHashes[connectionId][nick]->setIdentified(identified);

        uint position = m_nickLists[connectionId].indexOf(m_nickHashes[connectionId][nick]);
        QModelIndex index = NickListModel::index(position, 0, m_servers[connectionId]);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

struct timestampLessThanSort
{
    ChannelNickListFilterModel* This;
    timestampLessThanSort(ChannelNickListFilterModel* newThis)
    {
        This = newThis;
    }
    bool operator()(const Nick2* left, const Nick2* right)
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

    if (m_channel && index.parent().isValid() && (role == Qt::DecorationRole || Qt::ForegroundRole || role == Qt::ToolTipRole))
    {
        Nick2* nick = static_cast<Nick2*>(mapToSource(index).internalPointer());

        if (nick && role == Qt::ToolTipRole)
            return nick->getChannelTooltip(m_channelName);
        else if (nick && role == Qt::DecorationRole)
            return nick->getIcon(m_channelName);
        else if (nick && role == Qt::ForegroundRole && nick->isAway())
            return qApp->palette("QListView").brush(QPalette::Disabled, QPalette::Text);
    }

    return sourceModel()->data(mapToSource(index), role);
}

bool ChannelNickListFilterModel::isNickInChannel(const QString& nick) const
{
    if (sourceNickModel())
        return sourceNickModel()->isNickInChannel(m_connectionId, m_channelName, nick);

    return false;
}

bool ChannelNickListFilterModel::isNickAnyTypeOfOp(const QString& nick) const
{
    if (sourceNickModel())
        return sourceNickModel()->isNickAnyTypeOfOp(m_connectionId, m_channelName, nick);

    return false;
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
    if (pos == 0 && !inputBar->lastCompletion().isEmpty() && isNickInChannel(inputBar->lastCompletion()))
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
                        Nick2* nick = static_cast<Nick2*>(mapToSource(index(i, 0)).internalPointer());
                        if(nick->getNickname().startsWith(pattern, Preferences::self()->nickCompletionCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive) &&
                          (nick->getTimestamp(m_channelName) > timestamp))
                        {
                            timestamp = nick->getTimestamp(m_channelName);
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
                    QString lookNick = static_cast<Nick2*>(mapToSource(index(m_completionPosition,0)).internalPointer())->getNickname();

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
    QList<Nick2*> foundNicks;

    if((pattern.contains(QRegExp("^(\\d|\\w)"))) && skipNonAlfaNum)
    {
        prefix = "^([^\\d\\w]|[\\_]){0,}";
    }

    QRegExp regexp(prefix + QRegExp::escape(pattern));
    regexp.setCaseSensitivity(caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    for (int i = 0; i < rowCount(serverIndex()); ++i)
    {
        QString nickName = QString();
        Nick2* nick = static_cast<Nick2*>(mapToSource(index(i, 0)).internalPointer());
        if (nick) nickName = nick->getNickname();

        if (!prefix.isEmpty() && nickName.contains(prefixCharacter))
        {
            nickName = nickName.section(prefixCharacter, 1);
        }

        if (nickName.contains(regexp))
        {
            foundNicks.append(nick);
        }
    }

    qSort(foundNicks.begin(), foundNicks.end(), ::timestampLessThanSort(this));

    QList<Nick2*>::const_iterator i;
    for (i = foundNicks.constBegin(); i != foundNicks.constEnd(); ++i)
        found.append((*i)->getNickname());

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
            if (sourceNickModel()->hasChildren(sourceParent) && m_connectionId == index.internalId())
                return true;

            Nick2* nick = static_cast<Nick2*>(index.internalPointer());

            if (nick)
                return nick->isInChannel(m_channelName);
        }
    }

    return false;
}

//TODO if there's any speed optimizations to be had, it's here.
bool ChannelNickListFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    Nick2* leftNick = static_cast<Nick2*>(left.internalPointer());
    Nick2* rightNick = static_cast<Nick2*>(right.internalPointer());

    if (leftNick && rightNick)
    {
        if(Preferences::self()->sortByActivity())
        {
            return nickActivityLessThan(leftNick, rightNick);
        }
        else if(Preferences::self()->sortByStatus())
        {
            return nickStatusLessThan(leftNick, rightNick);
        }
        else
        {
            return nickLessThan(leftNick, rightNick);
        }
    }

    return false;
}

bool ChannelNickListFilterModel::nickTimestampLessThan(const Nick2* left, const Nick2* right) const
{
    int difference = left->getTimestamp(m_channelName) - right->getTimestamp(m_channelName);

    if (difference != 0)
        return (difference < 0) ? false : true;

    if (Preferences::self()->sortByStatus())
        return nickStatusLessThan(left, right);

    return nickLessThan(left, right);
}

bool ChannelNickListFilterModel::nickLessThan(const Nick2* left, const Nick2* right) const
{
    if (Preferences::self()->sortCaseInsensitive())
    {
        if (left->getLoweredNickname() != right->getLoweredNickname())
            return left->getLoweredNickname() < right->getLoweredNickname();

        return nickHostmaskLessThan(left, right);
    }
    else
    {
        if (left->getNickname() != right->getNickname())
            return left->getNickname() < right->getNickname();

        return nickHostmaskLessThan(left, right);
    }
}

bool ChannelNickListFilterModel::nickHostmaskLessThan(const Nick2* left, const Nick2* right) const
{
    if (Preferences::self()->sortCaseInsensitive())
    {
        return left->getLoweredHostmask() < right->getLoweredHostmask();
    }

    return left->getHostmask() < right->getHostmask();
}

bool ChannelNickListFilterModel::nickActivityLessThan(const Nick2* left, const Nick2* right) const
{
    int difference = left->getRecentActivity(m_channelName) - right->getRecentActivity(m_channelName);

    if (difference != 0)
        return (difference < 0) ? false : true;

    return nickTimestampLessThan(left, right);
}

bool ChannelNickListFilterModel::nickStatusLessThan(const Nick2* left, const Nick2* right) const
{
    int difference = left->getStatusValue(m_channelName) - right->getStatusValue(m_channelName);

    if (difference != 0)
        return (difference < 0) ? false : true;

    return nickLessThan(left, right);
}

#include "nicklistmodel.moc"
