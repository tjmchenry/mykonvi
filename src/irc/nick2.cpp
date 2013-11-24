/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
    begin:     Fri Jan 25 2002
    Copyright (C) 2002 by Dario Abatianni <eisfuchs@tigress.com>
    Copyright (C) 2013 Travis McHenry <me@travisjmchenry.com>
*/

#include "nick2.h"
#include "application.h"
#include "images.h"
#include "preferences.h"

Nick2::Nick2(int connectionId, const QString& nick) : QObject()
{
    m_nick = nick;
    m_channelHash = ChannelHash();

    m_loweredNickname = nick.toLower();
    m_connectionId = connectionId;
    m_away = false;
    m_identified = false;
    m_printedOnline = false;

    //TODO tell kpeople the nick is here

    // reset nick color
    m_nickColor = 0;

    m_queryTooltip = QString();

    connect(this, SIGNAL(channelPropertiesChanged(const QString&)), this, SLOT(updateStatusValue(const QString&)));
    connect(this, SIGNAL(channelPropertiesChanged(const QString&)), this, SLOT(updateTooltips(const QString&)));
}

Nick2::Nick2(const Nick2&)
{
}

Nick2::~Nick2()
{
    //TODO tell kpeople the nick is gone
}

void Nick2::addChannel(const QString& channel)
{
    ChannelProperties* properties = new ChannelProperties();

    Modes* modes = new Modes(); //qaohv
    modes->insert('q', false);
    modes->insert('a', false);
    modes->insert('o', false);
    modes->insert('h', false);
    modes->insert('v', false);

    properties->insert("modes", QVariant::fromValue(modes));
    properties->insert("activity", 0);
    properties->insert("timestamp", 0);
    properties->insert("statusValue", 0);
    properties->insert("icon", QPixmap());
    properties->insert("tooltip", QString());

    m_channelHash.insert(channel, properties);
    emit channelPropertiesChanged(channel);
}

void Nick2::removeChannel(const QString& channel)
{
    m_channelHash.remove(channel);
}

QStringList Nick2::getChannels() const
{
    return QStringList(m_channelHash.keys());
}

bool Nick2::isInChannel(const QString& channel) const
{
    return m_channelHash.contains(channel);
}

bool Nick2::isInAnyChannel() const
{
    return !m_channelHash.isEmpty();
}

bool Nick2::isOwner(const QString& channel) const
{
    if (isInChannel(channel))
    {
        return m_channelHash[channel]->value("modes").value<Modes*>()->value('q');
    }
    return false;
}

bool Nick2::isAdmin(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("modes").value<Modes*>()->value('a');

    return false;
}

bool Nick2::isOp(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("modes").value<Modes*>()->value('o');

    return false;
}

bool Nick2::isHalfOp(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("modes").value<Modes*>()->value('h');

    return false;
}

bool Nick2::hasVoice(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("modes").value<Modes*>()->value('v');

    return false;
}

bool Nick2::isAnyTypeOfOp(const QString& channel) const
{
  return isHalfOp(channel) || isOp(channel) || isAdmin(channel) || isOwner(channel);
}

/** @param mode 'v' to set voice, 'a' to set admin, 'h' to set halfop, 'o' to set op.
 *  @param state what to set the mode to.
 */
bool Nick2::setMode(const QString& channel, char mode, bool state)
{
    switch (mode)
    {
        case 'q':
            return setOwner(channel, state);
        case 'a':
            return setAdmin(channel, state);
        case 'o':
            return setOp(channel, state);
        case 'h':
            return setHalfOp(channel, state);
        case 'v':
            return setVoice(channel, state);
        default:
            kDebug() << "Mode '" << mode << "' not recognised in setModeForNick";
            return false;
    }
}

/** Used still for passing modes from inputfilter to Server.  Should be removed.
 */
bool Nick2::setMode(const QString& channel, int mode)
{
    bool voice = mode%2;
    mode >>= 1;
    bool halfop = mode %2;
    mode >>= 1;
    bool op = mode %2;
    mode >>= 1;
    bool owner = mode %2;
    mode >>= 1;
    bool admin = mode %2;

    return setMode(channel, admin, owner, op, halfop, voice);
}

bool Nick2::setMode(const QString& channel, bool admin, bool owner, bool op, bool halfop, bool voice)
{
    if(setAdmin(channel, admin) || setOwner(channel, owner) || setOp(channel, op)
    || setHalfOp(channel, halfop) || setVoice(channel, voice))
        return true;

    return false;
}

/** set the voice for the nick, and update
 * @returns Whether it needed to be changed.  False for no change.
 */
bool Nick2::setOwner(const QString& channel, bool state)
{
    if (!isInChannel(channel) || isOwner(channel) == state)
        return false;

    m_channelHash[channel]->value("modes").value<Modes*>()->insert('q', state);

    emit channelPropertiesChanged(channel);
    emit nickChanged(getNickname());

    return true;
}

bool Nick2::setAdmin(const QString& channel, bool state)
{
    if (!isInChannel(channel) || isAdmin(channel) == state)
        return false;

    m_channelHash[channel]->value("modes").value<Modes*>()->insert('a', state);

    emit channelPropertiesChanged(channel);
    emit nickChanged(getNickname());

    return true;
}

bool Nick2::setOp(const QString& channel, bool state)
{
    if (!isInChannel(channel) || isOp(channel) == state)
        return false;

    m_channelHash[channel]->value("modes").value<Modes*>()->insert('o', state);

    emit channelPropertiesChanged(channel);
    emit nickChanged(getNickname());

    return true;
}

bool Nick2::setHalfOp(const QString& channel, bool state)
{
    if (!isInChannel(channel) || isHalfOp(channel) == state)
        return false;

    m_channelHash[channel]->value("modes").value<Modes*>()->insert('h', state);

    emit channelPropertiesChanged(channel);
    emit nickChanged(getNickname());

    return true;
}

bool Nick2::setVoice(const QString& channel, bool state)
{
    if (!isInChannel(channel) || hasVoice(channel) == state)
        return false;

    m_channelHash[channel]->value("modes").value<Modes*>()->insert('v', state);

    emit channelPropertiesChanged(channel);
    emit nickChanged(getNickname());

    return true;
}

void Nick2::updateStatusValue(const QString& channel)
{
    if(channel.isEmpty()) //TODO fix so only run when away status changes
    {
        ChannelHash::const_iterator i;

        for (i = m_channelHash.constBegin(); i != m_channelHash.constEnd(); ++i)
        {
            updateStatusValue(i.key());
        }
    }
    else if (isInChannel(channel))
    {
        int value = 0;
        bool away = false;

        Images* images = Application::instance()->images();
        QPixmap icon;

        if (isAway()) //FIXME this doesn't really make sense why would away people be worth 1 more?
        {
            value = 1;
            away = true;
        }

        if (isOwner(channel))
        {
            value += 64;
            icon = images->getNickIcon(Images::Owner, away);
        }
        else if (isAdmin(channel))
        {
            value += 128;
            icon = images->getNickIcon(Images::Admin, away);
        }
        else if (isOp(channel))
        {
            value += 32;
            icon = images->getNickIcon(Images::Op, away);
        }
        else if (isHalfOp(channel))
        {
            value += 16;
            icon = images->getNickIcon(Images::HalfOp, away);
        }
        else if (hasVoice(channel))
        {
            value += 8;
            icon = images->getNickIcon(Images::Voice, away);
        }
        else
        {
            value += 4;
            icon = images->getNickIcon(Images::Normal, away);
        }

        m_channelHash[channel]->insert("statusValue", value);
        m_channelHash[channel]->insert("icon", icon);
    }
}

int Nick2::getStatusValue(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("statusValue").toInt();

    return 0;
}

QPixmap Nick2::getIcon(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("icon").value<QPixmap>();

    return QPixmap();
}

QString Nick2::getNickname() const
{
    return m_nick;
}

QString Nick2::getHostmask() const
{
    return m_hostmask;
}

QString Nick2::getLoweredHostmask() const
{
    return m_loweredHostmask;
}

void Nick2::setHostmask(const QString& newMask)
{
    if (newMask.isEmpty() || m_hostmask == newMask) return;

    m_hostmask = newMask;
    m_loweredHostmask = newMask.toLower();

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

QString Nick2::getChannelTooltip(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("tooltip").toString();

    return QString();
}

QString Nick2::getQueryTooltip() const
{
    return m_queryTooltip;
}

void Nick2::updateTooltips(const QString& channel)
{
    if (channel.isEmpty())
    {
        ChannelHash::const_iterator i;
        for (i = m_channelHash.constBegin(); i != m_channelHash.constEnd(); ++i)
        {
            updateTooltips(i.key());
        }
    }
    else if (isInChannel(channel))
    {
        QString strTooltip;
        QTextStream tooltip( &strTooltip, QIODevice::WriteOnly );

        tooltip << "<qt>";
        tooltip << "<table cellspacing=\"5\" cellpadding=\"0\">";

        tooltipTableData(tooltip);

        m_queryTooltip = strTooltip + "</table></qt>";

        QStringList modes;
        if(isOp(channel)) modes << i18n("Operator");
        if(isAdmin(channel)) modes << i18n("Admin");
        if(isOwner(channel)) modes << i18n("Owner");
        if(isHalfOp(channel)) modes << i18n("Half-operator");
        if(hasVoice(channel)) modes << i18n("Has voice");

        if(!modes.empty())
        {
            tooltip << "<tr><td><b>" << i18n("Mode") << ":</b></td><td>" << modes.join(", ") << "</td></tr>";
        }

        tooltip << "</table></qt>";

        m_channelHash[channel]->insert("tooltip", strTooltip);
    }
}

void Nick2::tooltipTableData(QTextStream &tooltip) const
{
    tooltip << "<tr><td colspan=\"2\" valign=\"top\">";

    //TODO get avatar from kpeople if it exists

    bool isimage = false; //temporary, used to define if there is an image to change formatting accordingly

    tooltip << "<b>" << (isimage?"":"<center>");

    tooltip << getBestPersonName().replace('<',"&lt;").replace('>',"&gt;");

    if (isIdentified()) tooltip << i18n(" (identified)");
    tooltip << (isimage?"":"</center>") << "</b>";

    //TODO get random properties birthday/email/whatever

    if (!getHostmask().isEmpty())
    {
        tooltip << "<tr><td><b>" << i18n("Hostmask:") << "</b></td><td>" << getHostmask() << "</td></tr>";
    }
    if(isAway())
    {
        tooltip << "<tr><td><b>" << i18n("Away&nbsp;Message:") << "</b></td><td>";
        if(!getAwayMessage().isEmpty())
            tooltip << getAwayMessage();
        else
            tooltip << i18n("(unknown)");
        tooltip << "</td></tr>";
    }
    if(!getOnlineSince().toString().isEmpty())
    {
        tooltip << "<tr><td><b>" << i18n("Online&nbsp;Since:") << "</b></td><td>" << getPrettyOnlineSince() << "</td></tr>";
    }
}

uint Nick2::getRecentActivity(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("activity").toUInt();

    return 0;
}

void Nick2::moreActive(const QString& channel)
{
    if (isInChannel(channel))
        m_channelHash[channel]->insert("activity", m_channelHash[channel]->value("activity").toUInt() + 1);
}

void Nick2::lessActive(const QString& channel)
{
    if (isInChannel(channel))
        m_channelHash[channel]->insert("activity", m_channelHash[channel]->value("activity").toUInt() - 1);
}

uint Nick2::getTimestamp(const QString& channel) const
{
    if (isInChannel(channel))
        return m_channelHash[channel]->value("timestamp").toUInt();

    return 0;
}

void Nick2::setTimestamp(const QString& channel, uint timestamp)
{
    if (isInChannel(channel))
        m_channelHash[channel]->insert("timestamp", timestamp);
}

QString Nick2::getLoweredNickname() const
{
    return m_loweredNickname;
}

bool Nick2::isAway() const
{
    return m_away;
}

QString Nick2::getAwayMessage() const
{
    return m_awayMessage;
}

QString Nick2::getRealName() const
{
    return m_realName;
}

QString Nick2::getNetServer() const
{
    return m_netServer;
}

QString Nick2::getNetServerInfo() const
{
    return m_netServerInfo;
}

QDateTime Nick2::getOnlineSince() const
{
    return m_onlineSince;
}

uint Nick2::getNickColor()
{
    // do we already have a color?
    if (!m_nickColor) m_nickColor = Konversation::colorForNick(m_nick) + 1;

    // return color offset -1 (since we store it +1 for 0 checking)
    return m_nickColor-1;
}

bool Nick2::isIdentified() const
{
    return m_identified;
}

QString Nick2::getPrettyOnlineSince() const
{
    return KGlobal::locale()->formatDateTime(m_onlineSince, KLocale::FancyLongDate, false);
}

int Nick2::getConnectionId() const
{
    return m_connectionId;
}

void Nick2::setNickname(const QString& newNickname)
{
    //FIXME why assert?
    Q_ASSERT(!newNickname.isEmpty());
    if(m_nick == newNickname) return;

    //TODO tell kpeople about the new nickname

    m_nick = newNickname;
    m_loweredNickname = newNickname.toLower();

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

void Nick2::setAway(bool state, const QString& awayMessage)
{
    if (m_away == state && m_awayMessage == awayMessage)
        return;

    if ((!m_awayMessage.isEmpty() && m_away) || (m_awayMessage.isEmpty() && !m_away))
        m_awayMessage = awayMessage;

    m_away = state;


    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());

    //TODO tell kpeople
}

void Nick2::setIdentified(bool identified)
{
    if(m_identified == identified) return;
    m_identified = identified;

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

void Nick2::setAwayMessage(const QString& newMessage)
{
    if(m_awayMessage == newMessage) return;
    m_awayMessage = newMessage;

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

void Nick2::setRealName(const QString& newRealName)
{
    if (newRealName.isEmpty() || m_realName == newRealName) return;
    m_realName = newRealName;

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

void Nick2::setNetServer(const QString& newNetServer)
{
    if (newNetServer.isEmpty() || m_netServer == newNetServer) return;
    m_netServer = newNetServer;

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

void Nick2::setNetServerInfo(const QString& newNetServerInfo)
{
    if (newNetServerInfo.isEmpty() || newNetServerInfo == m_netServerInfo) return;
    m_netServerInfo = newNetServerInfo;

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

void Nick2::setOnlineSince(const QDateTime& datetime)
{
    if (datetime.isNull() || datetime == m_onlineSince) return;
    m_onlineSince = datetime;

    emit channelPropertiesChanged(QString());
    emit nickChanged(getNickname());
}

QString Nick2::getBestPersonName() const
{
//TODO get alternative names from kpeople to use
    if(!getRealName().isEmpty())
    {
        return getRealName();
    }
    else
    {
        return getNickname();
    }
}

//FIXME what do these do and do they need to be available for each channel?
void Nick2::setPrintedOnline(bool printed)
{
    m_printedOnline = printed;
}

bool Nick2::getPrintedOnline() const
{
    return m_printedOnline;
}

#include "nick2.moc"