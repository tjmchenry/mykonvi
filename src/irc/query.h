/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2005-2008 Eike Hein <hein@kde.org>
*/

#ifndef QUERY_H
#define QUERY_H

#include "chatwindow.h"
#include "nick2.h"

#include <config-konversation.h>

#ifdef HAVE_QCA2
#include "cipher.h"
#endif

#include <QString>

/* TODO: Idle counter to close query after XXX minutes of inactivity */
/* TODO: Use /USERHOST to check if queries are still valid */

class AwayLabel;
class IRCInput;

class QLabel;
class QSplitter;

class KSqueezedTextLabel;


class Query : public ChatWindow
{
    Q_OBJECT

    public:
        explicit Query(QWidget* parent, const QString& _name);
        virtual void setServer(Server* newServer);

        ~Query();

        /** This will always be called soon after this object is created.
         *  @param nickInfo A nickinfo that must exist.
         */
        void setNick(Nick2* nick);
        /** It seems that this does _not_ guaranttee to return non null.
         *  The problem is when you open a query to someone, then the go offline.
         *  This should be fixed maybe?  I don't know.
         */
        Nick2* getNick();
        virtual bool closeYourself(bool askForConfirmation=true);
        virtual bool canBeFrontView();
        virtual bool searchView();

        virtual void setChannelEncoding(const QString& encoding);
        virtual QString getChannelEncoding();
        virtual QString getChannelEncodingDefaultDesc();
        virtual void emitUpdateInfo();

        /** call this when you see a nick quit from the server.
         *  @param reason The quit reason given by that user.
         */
        void quitNick(const QString& reason);

        #ifdef HAVE_QCA2
        Konversation::Cipher* getCipher();
        #endif

    signals:
        void sendFile(const QString& recipient);
        void updateQueryChrome(ChatWindow*, const QString&);

    public slots:
        void sendText(const QString& text);
        virtual void indicateAway(bool show);
        void setEncryptedOutput(bool);
        void connectionStateChanged(Server*, Konversation::ConnectionState);

    protected slots:
        void queryTextEntered();
        void queryPassthroughCommand();
        void sendFileMenu();
        void urlsDropped(const KUrl::List urls);
        // connected to IRCInput::textPasted() - used to handle large/multiline pastes
        void textPasted(const QString& text);
        void nickChanged();
        void updateNick(Server* server, Nick2* nick);

    protected:
        void setName(const QString& newName);
        void showEvent(QShowEvent* event);
        /** Called from ChatWindow adjustFocus */
        virtual void childAdjustFocus();

    private:
        bool awayChanged;
        bool awayState;

        QString queryName;
        QString buffer;

        QSplitter* m_headerSplitter;
        KSqueezedTextLabel* queryHostmask;
        QLabel* addresseeimage;
        QLabel* addresseelogoimage;
        QLabel* blowfishLabel;
        AwayLabel* awayLabel;
        Nick2* m_nick;

        bool m_initialShow;

        #ifdef HAVE_QCA2
        //FIXME: We might want to put this into the attendee object (i.e. NickInfo).
        Konversation::Cipher *m_cipher;
        #endif
};
#endif
