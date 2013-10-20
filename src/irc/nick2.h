/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  begin:     Fri Jan 25 2002
  Copyright (C) 2002 by Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2007 Peter Simonsson <peter.simonsson@gmail.com>
  Copyright (C) 2013 Travis McHenry <me@travisjmchenry.com>
*/

#ifndef NICK2_H
#define NICK2_H

#include <QObject>
#include "server.h"

typedef QHash<char, bool> Modes;
typedef QHash<QString, QVariant> ChannelProperties;
typedef QHash<QString, ChannelProperties*> ChannelHash;

Q_DECLARE_METATYPE(Modes*);
Q_DECLARE_METATYPE(ChannelProperties);
Q_DECLARE_METATYPE(ChannelHash);

class Nick2 : public QObject
{
    Q_OBJECT

    public:
        Nick2(const QString& nick, Server* server);
        Nick2(const Nick2&);
        ~Nick2();

        void addChannel(const QString& channel);
        void removeChannel(const QString& channel);
        QStringList getChannels() const;
        bool isInChannel(const QString& channel) const;
        bool isInAnyChannel() const;

        bool isAnyTypeOfOp(const QString& channel) const;
        bool hasVoice(const QString& channel) const;
        bool isHalfOp(const QString& channel) const;
        bool isOp(const QString& channel) const;
        bool isAdmin(const QString& channel) const;
        bool isOwner(const QString& channel) const;

        bool setOwner(const QString& channel, bool state);
        bool setAdmin(const QString& channel, bool state);
        bool setHalfOp(const QString& channel, bool state);
        bool setVoice(const QString& channel, bool state);
        bool setOp(const QString& channel, bool state);

        bool setMode(const QString& channel, char mode, bool state);
        bool setMode(const QString& channel, bool admin, bool owner, bool op, bool halfop, bool voice);
        bool setMode(const QString& channel, int mode); //TODO FIXME get rid of this

        int getStatusValue(const QString& channel) const;
        QPixmap getIcon(const QString& channel) const;

        QString getChannelTooltip(const QString& channel) const;
        QString getQueryTooltip() const;

        QString getLoweredNickname() const;
        QString getRealName() const;
        void setRealName(const QString& realName);
        QString getNickname() const;
        void setNickname(const QString& nick);
        QString getHostmask() const;
        QString getLoweredHostmask() const;
        void setHostmask(const QString& hostmask);
        QString getBestPersonName() const;

        bool isAway() const;
        void setAway(bool away, const QString& awayMessage = QString());
        QString getAwayMessage() const;
        void setAwayMessage(const QString&);
        QDateTime getOnlineSince() const;
        void setOnlineSince(const QDateTime& time);
        QString getPrettyOnlineSince() const;

        bool getPrintedOnline() const;
        void setPrintedOnline(bool printed);

        uint getNickColor();
        bool isIdentified() const;
        void setIdentified(bool identified);
        uint getTimestamp(const QString& channel) const;
        void setTimestamp(const QString& channel, uint timestamp);

        void lessActive(const QString& channel);
        void moreActive(const QString& channel);
        uint getRecentActivity(const QString& channel) const;

        Server* getServer() const;
        QString getNetServer() const;
        void setNetServer(const QString& netServer);
        QString getNetServerInfo() const;
        void setNetServerInfo(const QString& netServerInfo);

    signals:
        void channelPropertiesChanged(const QString& channel);
        void nickChanged(const QString& nick);

    public slots:
        void updateTooltips(const QString& channel);
        void updateStatusValue(const QString& channel);

    private:
        void startNickInfoChangedTimer();
        void tooltipTableData(QTextStream &tooltip) const;

        QString m_nick;
        QString m_loweredNickname;
        ChannelHash m_channelHash;
        QString m_hostmask;
        QString m_loweredHostmask;
        QString m_queryTooltip;
        QString m_realName;
        QString m_netServer;
        QString m_netServerInfo;
        QDateTime m_onlineSince;
        Server* m_owningServer;
        bool m_identified;
        bool m_printedOnline;
        int m_nickColor;
        bool m_away;
        QString m_awayMessage;
};
#endif


