/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2004-2006, 2009 Peter Simonsson <peter.simonsson@gmail.com>
  Copyright (C) 2006-2008 Eike Hein <hein@kde.org>
*/

#ifndef CHANNEL_H
#define CHANNEL_H

#include <config-konversation.h>

#include "server.h"
#include "chatwindow.h"
#include "nicklistmodel.h"

#ifdef HAVE_QCA2
#include "cipher.h"
#endif

#include <QTimer>
#include <QString>
#include <QListView>
#include <QTreeView>


class QLabel;
class QTimer;
class QTreeWidgetItem;
class QStringList;
class QSplitter;
class QToolButton;

class KLineEdit;
class KVBox;
class KHBox;
class KComboBox;

class AwayLabel;
class Nick;
class QuickButton;
class ModeButton;
class IRCInput;
class NickChangeDialog;
class TopicHistoryModel;
class CipherFilterProxyModel;
class ChannelNickListFilterModel;

class Nick2;

namespace Konversation
{
    class TopicLabel;
    class ChannelOptionsDialog;
    class ChannelSettings;
}

class Channel : public ChatWindow
{
    Q_OBJECT

    friend class Nick;

    public:
        explicit Channel(QWidget* parent, const QString& name);
        ~Channel();
//META
        virtual bool canBeFrontView();
        virtual bool searchView();

        virtual void append(const QString& nickname,const QString& message);
        virtual void appendAction(const QString& nickname,const QString& message);
        #ifdef HAVE_QCA2
        Konversation::Cipher* getCipher();
        #endif
//General administrative stuff
    public:
        void setName(const QString& newName);
        QString getPassword();

        const Konversation::ChannelSettings channelSettings();

        QString getPassword() const;

        virtual void setServer(Server* newServer);

        void setEncryptedOutput(bool);

        bool joined() { return m_joined; }
        bool rejoinable();
//Unsure of future placement and/or continued existence of these members
        int numberOfNicks() const { return nicks; }
        int numberOfOps() const { return ops; }
        virtual void setChannelEncoding(const QString& encoding);
        virtual QString getChannelEncoding();
        virtual QString getChannelEncodingDefaultDesc();

        virtual bool log();

    protected:
        void setActive(bool active);
        bool shouldShowEvent(const QString& nick);

        QString getCurrentTopic();

    public slots:
        void setNickname(const QString& newNickname);
        void scheduleAutoWho(int msec = -1);
        void setAutoUserhost(bool state);
        void rejoin();

    protected slots:
        void setCurrentTopic();
        void autoUserhost();
        void autoWho();
        void updateAutoWho();
        void fadeActivity();
        virtual void serverOnline(bool online);


//Nicklist
    public:
        void joinNickname(const QString& nickname, const QString& hostmask);
        void removeNick(const QString& nickname, const QString& hostmask, const QString &reason, bool quit);
        void kickNick(const QString& nickname, const QString &kicker, const QString &reason);
        void nickRenamed(const QString &oldNick, const QString& nickname);
        void endOfNames();

        virtual void emitUpdateInfo();

        void resizeNicknameListViewColumns();

    protected slots:
        void purgeNicks();
        void contextMenu(const QPoint& pos);

//Topic
    public:
        QString getTopic();
        TopicHistoryModel* getTopicHistory() { return m_topicHistory; };
#ifdef HAVE_QCA2
        CipherFilterProxyModel* getCipherFilter() { return m_cipherFilterModel; }
#endif

        void setTopic(const QString& text);
        void setTopic(const QString& nickname, const QString& text);
        void setTopicAuthor(const QString& author, QDateTime timestamp);

    signals:
        void joined(Channel* channel);


//Modes
//TODO: the only representation of the channel limit is held in the GUI

    public:
        /// Internal - Empty the modelist
        void clearModeList();
        /// Get the list of modes that this channel has - e.g. {+l,+s,-m}
        //TODO: does this method return a list of all modes, all modes that have been changed, or all modes that are +?
        QStringList getModeList() const { return m_modeList; }

        /** Outputs a message on the channel, and modifies the mode for a ChannelNick.
         *  @param sourceNick The server or the nick of the person that made the mode change.
         *  @param mode The mode that is changing one of v,h,o,a for voice halfop op admin
         *  @param plus True if the mode is being granted, false if it's being taken away.
         *  @param parameter This depends on what the mode change is.  In most cases it is the nickname of the person that is being given voice/op/admin etc.  See the code.
         */
        void updateMode(const QString& sourceNick, char mode, bool plus, const QString &parameter);

    signals:
        void modesChanged();

//Bans
    public:

        void addBan(const QString& ban);
        void removeBan(const QString& ban);

        void clearBanList();
        QStringList getBanList() const { return m_BanList; }

    signals:
        void banAdded(const QString& newban);
        void banRemoved(const QString& newban);
        void banListCleared();

//Generic GUI
    public:
        virtual bool eventFilter(QObject* watched, QEvent* e);

//Specific GUI
    public:
        void updateModeWidgets(char mode, bool plus, const QString &parameter);
        void updateQuickButtons(const QStringList &newButtonList);

        /// Sounds suspiciously like a destructor..
        virtual bool closeYourself(bool askForConfirmation=true);

        bool autoJoin();

        QStringList getSelectedNickList();

        Konversation::ChannelSettings channelSettings() const;

    signals:
        void sendFile();

    public slots:
        void updateAppearance();
        void channelTextEntered();
        void channelPassthroughCommand();
        void sendText(const QString& line);
        void showOptionsDialog();
        void showQuickButtons(bool show);
        void showModeButtons(bool show);

        virtual void indicateAway(bool show);
        void showTopic(bool show);
        void showNicknameBox(bool show);
        void showNicknameList(bool show);

        void setAutoJoin(bool autojoin);

        void connectionStateChanged(Server*, Konversation::ConnectionState);

        bool isNickInChannel(const QString& nick) const;
        bool isNickAnyTypeOfOp(const QString& nick) const;
        uint getNickTimestamp(const QString& nick) const;

    protected slots:
        void quickButtonClicked(const QString& definition);
        void modeButtonClicked(int id,bool on);
        void channelLimitChanged();

        void doubleClickCommand(QTreeWidgetItem *item,int column);  ///< Connected to NickListView::itemDoubleClicked()
        // Dialogs
        void changeNickname(const QString& newNickname);

        void textPasted(const QString& text); ///< connected to IRCInput::textPasted() - used to handle large/multiline pastings

        void sendFileMenu(); ///< connected to IRCInput::sendFile()
        void nicknameComboboxChanged();
        /// Enable/disable the mode buttons depending on whether you are op or not.
        void refreshModeButtons();

        void nicknameListViewTextChanged(int textChangedFlags);
    protected:
        void showEvent(QShowEvent* event);
        void syncSplitters();
        /// Called from ChatWindow adjustFocus
        virtual void childAdjustFocus();

        // to take care of redraw problem if hidden
        bool quickButtonsChanged;
        bool quickButtonsState;
        bool modeButtonsChanged;
        bool modeButtonsState;
        bool awayChanged;
        bool awayState;
        bool splittersInitialized;
        bool topicSplitterHidden;
        bool channelSplitterHidden;

        int completionPosition;

        QSplitter* m_horizSplitter;
        QSplitter* m_vertSplitter;
        QWidget* topicWidget;
        QToolButton* m_topicButton;
        Konversation::TopicLabel* topicLine;

        //TODO: abstract these
        KHBox* modeBox;
        ModeButton* modeT;
        ModeButton* modeN;
        ModeButton* modeS;
        ModeButton* modeI;
        ModeButton* modeP;
        ModeButton* modeM;
        ModeButton* modeK;
        ModeButton* modeL;

        KLineEdit* limit; //TODO: this GUI element is the only storage for the mode

        QListView* m_nicknameListView2;

        ChannelNickListFilterModel* m_channelNickListModel;
        KHBox* commandLineBox;
        KVBox* nickListButtons;
        QWidget* m_buttonsGrid;
        KComboBox* nicknameCombobox;
        QString oldNick; ///< GUI
        AwayLabel* awayLabel;
        QLabel* cipherLabel;

        NickChangeDialog* nickChangeDialog;
        QList<QuickButton*> buttonList;

//Members from here to end are not GUI
        bool m_joined;
        QTimer userhostTimer;
        int m_nicknameListViewTextChanged;

        TopicHistoryModel* m_topicHistory;
        QStringList m_BanList;

        QTimer m_whoTimer; ///< For continuous auto /WHO
        QTime  m_whoTimerStarted;

        QTimer m_fadeActivityTimer; ///< For the smoothing function used in activity sorting

        bool m_initialNamesReceived;

        QStringList m_modeList;

        bool pendingNicks; ///< are there still nicks to be added by /names reply?
        int nicks; ///< How many nicks on the channel
        int ops; ///< How many ops on the channel

        Konversation::ChannelOptionsDialog *m_optionsDialog;
#ifdef HAVE_QCA2
        Konversation::Cipher *m_cipher;
        CipherFilterProxyModel* m_cipherFilterModel;
#endif
};
#endif
