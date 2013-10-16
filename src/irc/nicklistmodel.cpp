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

#include <QTextCursor>

NickListModel::NickListModel(Server* server) : QAbstractListModel(server)
{
    m_nickHash = QHash<QString, Nick2*>();
    m_server = server;
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
    m_nickHash.clear();
}

void NickListModel::insertNick(Nick2* item)
{
    m_nickHash.insert(item->getNickname(), item);
    reset();
}

/* insert without resetting the model */
void NickListModel::fastInsertNick(Nick2* item)
{
    m_nickHash.insert(item->getNickname(), item);

    uint position = getHashPosition(item->getNickname());

    beginInsertRows(QModelIndex(), position, position);
    endInsertRows();
}

void NickListModel::addNickToChannel(const QString& nick, const QString& channel)
{
    if (!isNickOnline(nick))
    {
        Nick2* newNick = new Nick2(nick, m_server);
        insertNick(newNick);
    }

    m_nickHash[nick]->addChannel(channel);

    uint position = getHashPosition(nick);
    QModelIndex index = NickListModel::index(position, 0);
    emit dataChanged(index, index); //changes tooltips in existing channels, and all roles in new channel
}

void NickListModel::removeNick(const QString& nick)
{
    if (isNickOnline(nick))
    {
        uint position = getHashPosition(nick);

        beginRemoveRows(QModelIndex(), position, position);

        m_nickHash.remove(nick);

        endRemoveRows();
    }
}

void NickListModel::removeNickFromChannel(const QString& nick, const QString& channel)
{
    kDebug() << "Removing nick from channel";
    if (isNickOnline(nick))
    {
        m_nickHash[nick]->removeChannel(channel);

        if(!m_nickHash[nick]->isInAnyChannel())
        {
            kDebug() << "Removed nick completely";
            removeNick(nick);
        }
        else
        {
            uint position = getHashPosition(nick);
            QModelIndex index = NickListModel::index(position, 0);
            emit dataChanged(index, index); //changes tooltips in existing channels, and all roles in removed channel
        }
    }
}

uint NickListModel::getHashPosition(const QString& nick) const
{
    QHash<QString, Nick2*>::const_iterator i = m_nickHash.find(nick);

    uint position = 0;

    while (i != m_nickHash.constBegin()) 
    {
        ++position;
        --i;
    }

    return position;
}

QModelIndex NickListModel::index(int row, int column, const QModelIndex& /* parent */) const
{
    if (row < 0 || column < 0)
        return QModelIndex();

    if (rowCount() <= row || columnCount() <= column)
        return QModelIndex();

    // This will be okay as long as the data in the hash is not changed
    // If it is, all of the data is invalid and all of these need to be
    // recalculated. Since the model would -also- need to be recalculated
    // there should not be any problems.

    QHash<QString, Nick2*>::const_iterator i = m_nickHash.constBegin() + row;

    return createIndex(row, column, m_nickHash.value(i.key()));
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

int NickListModel::rowCount(const QModelIndex& /*parent*/) const
{
    return m_nickHash.count();
}

QVariant NickListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_nickHash.count ())
        return QVariant();

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

    return QVariant();
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
    kDebug() << "Minimum Row Height: " << m_minimumRowHeight;
}

bool NickListModel::isNickOnline(const QString& nick) const
{
    return m_nickHash.contains(nick);
}

bool NickListModel::isNickIdentified(const QString& nick) const
{
    if (isNickOnline(nick))
        return m_nickHash[nick]->isIdentified();

    return false;
}

QList<QString> NickListModel::getNickChannels(const QString& nick) const
{
    if (isNickOnline(nick))
        return m_nickHash[nick]->getChannels();

    return QList<QString>();
}

bool NickListModel::isNickInChannel(const QString& nick, const QString& channel) const
{
    if (isNickOnline(nick))
        return m_nickHash[nick]->isInChannel(channel);

    return false;
}

//hostmask
QString NickListModel::getNickHostmask(const QString& nick) const
{
    if (isNickOnline(nick))
        return m_nickHash[nick]->getHostmask();

    return QString();
}

void NickListModel::setNickHostmask(const QString& nick, const QString& hostmask)
{
    if (!hostmask.isEmpty() && isNickOnline(nick))
    {
        m_nickHash[nick]->setHostmask(hostmask);

        uint position = getHashPosition(nick);
        QModelIndex index = NickListModel::index(position, 0);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

void NickListModel::setNewNickname(const QString& nick, const QString& newNick)
{
    if (isNickOnline(nick) && !newNick.isEmpty())
    {
        m_nickHash[nick]->setNickname(newNick);
        m_nickHash[newNick] = m_nickHash[nick];
        m_nickHash.remove(nick);

        uint position = getHashPosition(nick);
        QModelIndex index = NickListModel::index(position, 0);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

//activity
uint NickListModel::getNickActivity(const QString& nick, const QString& channel) const
{
    if (isNickOnline(nick))
        return m_nickHash[nick]->getRecentActivity(channel);

    return 0;
}

void NickListModel::setNickMoreActive(const QString& nick, const QString& channel)
{
    if (isNickOnline(nick))
    {
        m_nickHash[nick]->moreActive(channel);

        uint position = getHashPosition(nick);
        QModelIndex index = NickListModel::index(position, 0);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DisplayRole);
    }
}

//timestamp
uint NickListModel::getNickTimestamp(const QString& nick, const QString& channel) const
{
    if (isNickOnline(nick))
        return m_nickHash[nick]->getTimestamp(channel);

    return 0;
}

//status
uint NickListModel::getNickStatusValue(const QString& nick, const QString& channel) const
{
    if (isNickOnline(nick))
        return m_nickHash[nick]->getStatusValue(channel);

    return 0;
}

void NickListModel::setNickMode(const QString& nick, const QString& channel, unsigned int mode)
{
    kDebug() << "Setting Nick Mode for: " << nick;
    if (isNickOnline(nick))
    {
        m_nickHash[nick]->setMode(channel, mode);

        uint position = getHashPosition(nick);
        QModelIndex index = NickListModel::index(position, 0);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DecorationRole);
    }
}

void NickListModel::setNickMode(const QString& nick, const QString& channel, char mode, bool state)
{
    kDebug() << "Setting Nick Mode for: " << nick;
    if (isNickOnline(nick))
    {
        m_nickHash[nick]->setMode(channel, mode, state);

        uint position = getHashPosition(nick);
        QModelIndex index = NickListModel::index(position, 0);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DecorationRole);
    }
}

void NickListModel::setNickAway(const QString& nick, bool away)
{
    kDebug() << "Setting Nick Away for: " << nick;
    if (isNickOnline(nick))
    {
        m_nickHash[nick]->setAway(away);

        uint position = getHashPosition(nick);
        QModelIndex index = NickListModel::index(position, 0);

        //TODO when we can dep Qt 5 we can specify what roles have changed.
        emit dataChanged(index, index); //, QVector<int>() << Qt::DecorationRole << Qt::DisplayRole);
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

ChannelNickListFilterModel::ChannelNickListFilterModel(Channel* channel) : QSortFilterProxyModel(channel)
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

    QSortFilterProxyModel::setDynamicSortFilter(true);
    QSortFilterProxyModel::sort(0, Qt::AscendingOrder);
}

ChannelNickListFilterModel::~ChannelNickListFilterModel()
{
}

void ChannelNickListFilterModel::insertNick(Nick2* item)
{
    if (sourceNickModel()->isNickOnline(item->getNickname()))
    {
        sourceNickModel()->fastInsertNick(item);

        sourceNickModel()->addNickToChannel(item->getNickname(), m_channelName);
    }
}

void ChannelNickListFilterModel::removeNick(const Nick2* item)
{
    if (sourceNickModel()->isNickOnline(item->getNickname()))
    {
        sourceNickModel()->removeNickFromChannel(item->getNickname(), m_channelName);
    }

}

NickListModel* ChannelNickListFilterModel::sourceNickModel() const
{
    return static_cast<NickListModel*>(sourceModel());
}

QVariant ChannelNickListFilterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return QVariant();

    if (m_channel && (role == Qt::ToolTipRole || role == Qt::DecorationRole || Qt::ForegroundRole))
    {
        Nick2* nick = static_cast<Nick2*>(mapToSource(index).internalPointer());

        if (role == Qt::ToolTipRole)
            return nick->getChannelTooltip(m_channelName);
        else if (role == Qt::DecorationRole)
            return nick->getIcon(m_channelName);
        else if (role == Qt::ForegroundRole && nick->isAway())
            return qApp->palette("QListView").brush(QPalette::Disabled, QPalette::Text);
    }

    return sourceModel()->data(mapToSource(index), role);
}

bool ChannelNickListFilterModel::isNickInChannel(const QString& nick) const
{
    return sourceNickModel()->isNickInChannel(nick, m_channelName);
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
    if (m_completionPosition >= rowCount()) m_completionPosition = 0;

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
            else if(Preferences::self()->nickCompletionMode() == 0 && rowCount() > 0)
            {
                if(mode == '\0') {
                    uint timestamp = 0;
                    int listPosition = 0;

                    for (int i = 0; i < rowCount(); i++)
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
                    if(m_completionPosition == rowCount())
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
    for (int i = 0; i < rowCount(); ++i)
    {
        Nick2* nick = static_cast<Nick2*>(mapToSource(index(i, 0)).internalPointer());
        QString nickName = nick->getNickname();

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

bool ChannelNickListFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    Nick2* nick = static_cast<Nick2*>(index.internalPointer());

    return nick->isInChannel(m_channelName);
}

//TODO if there's any speed optimizations to be had, it's here.
bool ChannelNickListFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    Nick2* leftNick = static_cast<Nick2*>(left.internalPointer());
    Nick2* rightNick = static_cast<Nick2*>(right.internalPointer());
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

bool ChannelNickListFilterModel::nickTimestampLessThan(const Nick2* left, const Nick2* right) const
{
    int difference = left->getTimestamp(m_channelName) - right->getTimestamp(m_channelName);

    if (difference != 0)
        return (difference < 0);

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
        return (difference < 0);

    return nickTimestampLessThan(left, right);
}

bool ChannelNickListFilterModel::nickStatusLessThan(const Nick2* left, const Nick2* right) const
{
    int difference = left->getStatusValue(m_channelName) - right->getStatusValue(m_channelName);

    if (difference != 0)
        return (difference < 0);

    return nickLessThan(left, right);
}

#include "nicklistmodel.moc"
