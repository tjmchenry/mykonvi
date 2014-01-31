/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2005 Ismail Donmez <ismail@kde.org>
  Copyright (C) 2005 Peter Simonsson <psn@linux.se>
  Copyright (C) 2005 John Tapsell <johnflux@gmail.com>
  Copyright (C) 2005-2008 Eike Hein <hein@kde.org>
*/

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include "servergroupsettings.h"
#include <servergroupmodel.h>
#include "identity.h"
#include "preferences_base.h"


/*
Options still to be GUIfied:

Operator LEDs (int)
OperatorColor (int)
VoiceColor (int)
NoRightsColor (int)
*/

class Ignore;
class Highlight;
struct PreferencesSingleton;

class QTreeView;

class Preferences : public PreferencesBase
{
    Q_OBJECT

    friend struct PreferencesSingleton;

    protected:
        Preferences();

    public:

        static Preferences *self();
        ~Preferences();
        enum Pages
        {
            NotifyPage,
            ChatWinAppearancePage
        };
        static const Konversation::ServerGroupHash serverGroupHash();
        static const Konversation::ServerGroupList serverGroupList();
        static ServerGroupModel* serverGroupModel();
        static void setServerGroupList(const Konversation::ServerGroupList& list);
        static void addServerGroup(Konversation::ServerGroupSettingsPtr serverGroup);
        static const Konversation::ServerGroupSettingsPtr serverGroupById(int id);
        static const Konversation::ServerGroupSettingsPtr serverGroupByIndex(int index);
        static const QList<Konversation::ServerGroupSettingsPtr> serverGroupsByServer(const QString& server);
        static QList<int> serverGroupIdsByName(const QString& serverGroup);
        static bool isServerGroup(const QString& server);
        static void removeServerGroup(int id);

        /** Returns a list of alias set up by default.  This is a set of aliases for the scripts found. */
        static QStringList defaultAliasList();

        static const QList<Highlight*> highlightList();
        static void setHighlightList(QList<Highlight*> newList);
        static void addHighlight(const QString& highlight, bool regExp, const QColor& color,
            const QString& soundURL, const QString& autoText,const QString& chatWindows, bool notify);

        /* All of the below work on the first (default) identity in your identity list*/
        static void addIgnore(const QString &newIgnore);
        static bool removeIgnore(const QString &oldIgnore);
        static bool isIgnored(const QString &nickname);
        static void clearIgnoreList();
        static const QList<Ignore*> ignoreList();
        static void setIgnoreList(QList<Ignore*> newList);

        static const QStringList quickButtonList();
        static const QStringList defaultQuickButtonList();
        static void setQuickButtonList(const QStringList& newList);
        static void clearQuickButtonList();

        static const QList<QStringList> autoreplaceList();
        static const QList<QStringList> defaultAutoreplaceList();
        static void setAutoreplaceList(const QList<QStringList> newList);
        static void clearAutoreplaceList();

        static void addIdentity(IdentityPtr identity);
        static void removeIdentity(IdentityPtr identity);
        static void clearIdentityList();
        static const IdentityList identityList();
        static void setIdentityList(const IdentityList& list);
        static const IdentityPtr identityByName(const QString& name);
        static const IdentityPtr identityById(int id);
        static const QString defaultNicknameSortingOrder();

        static bool dialogFlag(const QString& flagName);
        static void setDialogFlag(const QString& flagName,bool state);

        static const QString channelEncoding(const QString& server,const QString& channel);
        static const QString channelEncoding(int serverGroupId,const QString& channel);
        static void setChannelEncoding(const QString& server,const QString& channel,const QString& encoding);
        static void setChannelEncoding(int serverGroupId,const QString& channel,const QString& encoding);
        static const QList<int> channelEncodingsServerGroupIdList();
        static const QStringList channelEncodingsChannelList(int serverGroupId);

        static const QString spellCheckingLanguage(Konversation::ServerGroupSettingsPtr serverGroup, const QString& key);
        static const QString spellCheckingLanguage(const QString& server, const QString& key);
        static void setSpellCheckingLanguage(Konversation::ServerGroupSettingsPtr serverGroup, const QString& key, const QString& language);
        static void setSpellCheckingLanguage(const QString& server, const QString& key, const QString& language);
        static const QHash<Konversation::ServerGroupSettingsPtr, QHash<QString, QString> > serverGroupSpellCheckingLanguages();
        static const QHash<QString, QHash<QString, QString> > serverSpellCheckingLanguages();

        static void setShowTrayIcon(bool state);
        static void setTrayNotify(bool state);
        static void setAutoUserhost(bool state);

        static QString webBrowserCmd();

        static void saveColumnState(QTreeView *treeView, QString name);
        static void restoreColumnState(QTreeView *treeView, QString name, int defaultColumn = 0,
                                       Qt::SortOrder defaultSortOrder = Qt::AscendingOrder);

    public slots:
        static void slotSetUseOSD(bool use);

    signals:
        void updateTrayIcon();

    protected:
        IdentityPtr mIdentity;
        ServerGroupModel* mServerGroupModel;
        QList<Ignore*> mIgnoreList;
        QList<IdentityPtr> mIdentityList;
        QList<Highlight*> mHighlightList;
        QMap< int,QMap<QString,QString> > mChannelEncodingsMap;  // mChannelEncodingsMap[serverGroupdId][channelName]
        QHash<Konversation::ServerGroupSettingsPtr, QHash<QString, QString> > mServerGroupSpellCheckingLanguages;
        QHash<QString, QHash<QString, QString> > mServerSpellCheckingLanguages;
        QStringList mQuickButtonList;
        QList<QStringList> mAutoreplaceList;
        QString mSortingOrder;
};
#endif
