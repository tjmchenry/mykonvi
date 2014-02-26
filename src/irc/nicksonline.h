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

#ifndef NICKSONLINEMODEL_H
#define NICKSONLINEMODEL_H

#include <QSortFilterProxyModel>
#include <QStringList>

#include <KToolBar>

#include "chatwindow.h"
#include "nicklistmodel.h"
#include "connectionmanager.h"
#include "servergroupmodel.h"
#include "servergroupsettings.h"

#include "ui_nicksonlinepanelui.h"

            //nick, nick object
typedef QHash<QString, Nick2*> WatchedNicks;
            //connection id, watched nick list
typedef QHash<int, WatchedNicks> WatchedNickConnections;
            //server group id, watched nick connections
typedef QHash<int, WatchedNickConnections> WatchedNickListHash;

class ConnectionManager;
class NickListModel;

class NicksOnlineFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    public:
        explicit NicksOnlineFilterModel(QObject* parent = 0);
        ~NicksOnlineFilterModel();

        QVariant data(const QModelIndex& index, int role) const;
        QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
        bool isWatchedNickOnline(int cId, const QString& nick) const;
        Nick2* getWatchedNick(int cId, const QString& nick) const;
        Nick2* getWatchedNick(int sgId, int cId, const QString& nick) const;

    signals:
        void requestWhois(int cId, const QString& nick);

    public slots:
        void removeNotifyNick(int sgId, const QString& nick);
        void addNotifyNick(int sgId, const QString& nick);
        void updateNotifyConnection(int sgId, int cId);

        void isonResponse(int sgId, int cId, const QString& newIson);
        void watchedNicksOnline(int sgId, int cId, const QStringList& onlineList);
        void watchedNicksOffline(int sgId, int cId, const QStringList& offlineList);
        void watchedNicksList(int sgId, int cId, int type, const QStringList& watchedList);
        void endOfWatchedNicksList(int sgId, int cId, int type);
        void nickOnline(int sgId, int cId, const QString& nick);
        void nickOffline(int sgId, int cId, Nick2* nick);

        void slotRequestWhois(int cId, const QString& nick);
        void whoisReceived(int cId, const QString& nick);

        void slotRequestWatchedNicksList(int cId, int type);
        void watchedNicksListReceived(int sgId, int cId, int type);

        void endOfISupport(int cId = -1, bool last = true);

    protected:
        bool lessThan(const QModelIndex& left, const QModelIndex& right) const;
        bool filterAcceptsColumn(int column, const QModelIndex& parent) const;
        bool filterAcceptsRow(int row, const QModelIndex& parent) const;
        int columnCount(const QModelIndex& parent = QModelIndex()) const;

        void removeNotifyNick(int sgId, int cId, const QString& nick);
        void removeActivelyWatchedNick(int cId, const QString& nick);
        void addNotifyNick(int sgId, int cId, Nick2* nick);
        void replaceNotifyNick(int sgId, int cId, Nick2* nick);
        bool isNickWatched(int sgId, int cId, const QString& nick) const;
        bool isNickActivelyWatched(int cId, const QString& nick) const;
        bool isNickOnline(int cId, const QString& nick) const;
        bool isWatchedNickOnline(int sgId, int cId, const QString& nick) const;

        Nick2* getNick(int cId, const QString& nick) const;
        QModelIndex getNotifyNickIndex(int sgId, const QString& nick) const;

        void waitForISupport(int cId);
        void addServerNotifyNicks(int cId);

        void requestMonitor(int cId, const QStringList& nicks);
        void requestWatch(int cId, const QStringList& nicks);
        void requestIson(int cId, const QStringList& nicks);

    protected slots:
        void notifyCheck();
        void slotNickChanged(int cId, const QString& nick, QVector<int> columnsChanged, QVector<int> rolesChanged);

    private:
        void updateMinimumRowHeight();

        int m_minimumRowHeight;

        QHash<int, QHash<int, QStringList> > m_activelyWatchedNicks;
        QHash<int, bool> m_notifyChanged;

        QHash<int, QStringList> m_isonList;

        QMultiHash<int, QString> m_whoisRequested;

        QIcon m_onlineIcon;
        QIcon m_offlineIcon;
        QString m_whatsThis;
        bool m_hostmask;
        int m_column;
        NickListModel* m_nickListModel;
        ConnectionManager* m_connectionManager;

        WatchedNickListHash m_watchedNicks;
        QTimer* m_notifyTimer;
        QHash<int, QTimer*> m_iSupportTimerHash;
};

class NicksOnline : public ChatWindow, private Ui::NicksOnlineWidgetUI
{
    Q_OBJECT

    public:
        explicit NicksOnline(QWidget* parent = 0);
        ~NicksOnline();

        virtual bool canBeFrontView() { return true; }

        bool isInsertSupported() { return true; }
        QString getTextInLine();

    signals:
        void doubleClicked(int sgId, const QString& nick);
        void showView(ChatWindow* view);

    public slots:
        virtual void appendInputText(const QString&, bool fromCursor);

    protected:
        /** Called from ChatWindow adjustFocus */
        virtual void childAdjustFocus(){}

    protected slots:
        //Used to disable functions when not connected
        virtual void serverOnline(bool online);

        void addNickname();
        void slotAddNickname(int cId, const QString& nick);
        void removeNickname();
        void createContact();
        void editContact();
        void changeAssociation();
        void openQuery();

        void doubleClicked(const QModelIndex& index);
        void currentChanged(const QModelIndex& current, const QModelIndex& previous);
        void collapsed(const QModelIndex& index);
        void expanded(const QModelIndex& index);
        void contextMenu(const QPoint& pos);

    private:
        NicksOnlineFilterModel* m_nicksOnlineModel;

        KToolBar* m_toolBar;
        QAction* m_addNickname;
        QAction* m_removeNickname;
        QAction* m_newContact;
        QAction* m_editContact;
        QAction* m_changeAssociation;
        QAction* m_openQuery;

};

#endif