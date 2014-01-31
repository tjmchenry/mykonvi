/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  The line input widget with chat enhanced functions
  begin:     Tue Mar 5 2002
  copyright: (C) 2002 by Dario Abatianni
  email:     eisfuchs@tigress.com
*/

#ifndef IRCINPUT_H
#define IRCINPUT_H

#include <KTextEdit>

namespace Sonnet
{
    class Speller;
}

class KCompletionBox;

class IRCInput : public KTextEdit
{
    Q_OBJECT

    public:
        explicit IRCInput(QWidget* parent);
        ~IRCInput();

        void setCompletionMode(char mode);
        char getCompletionMode();
        void setOldCursorPosition(int pos);
        int getOldCursorPosition();
        QString lastCompletion() const { return m_lastCompletion; }
        void doInlineAutoreplace();

        virtual QSize sizeHint() const;
        virtual QSize minimumSizeHint() const;

        virtual bool event(QEvent* e);

        virtual void createHighlighter();

    signals:
        void nickCompletion(IRCInput* input);
        void endCompletion();                     // tell channel that completion phase is over
        void textPasted(const QString& text);
        void submit();
        void envelopeCommand();

    public slots:
        void paste(bool useSelection);
        void showCompletionList(const QStringList& nicks);
        void setText(const QString& text, bool preserveContents = false);
        void setLastCompletion(const QString& completion);
        virtual void setOverwriteMode(bool) { }
        virtual void updateAppearance();

    protected slots:
        void getHistory(bool up);
        void insertCompletion(const QString& nick);
        void disableSpellChecking();
        void setSpellChecking(bool set);
        void insertLanguageMenu(QMenu* contextMenu);
        void languageSelected();

        void maybeResize();

    protected:
        bool eventFilter(QObject *object,QEvent *event);
        void addHistory(const QString& text);
        bool checkPaste(QString& text);

        virtual void insertFromMimeData(const QMimeData *source);
        virtual void keyPressEvent(QKeyEvent* e);
        virtual void wheelEvent(QWheelEvent* e);
        //virtual Q3PopupMenu *createPopupMenu( const QPoint& pos );
        virtual void showEvent(QShowEvent* e);
        virtual void hideEvent(QHideEvent* e);
        virtual void resizeEvent(QResizeEvent* e);

        QStringList historyList;
        int lineNum;
        int oldPos;
        char completionMode;
        KCompletionBox* completionBox;
        QString m_lastCompletion;
        bool m_multiRow;
        int m_qtBoxPadding; //see comment in constructor

        QTimer* m_disableSpellCheckTimer;

        static Sonnet::Speller* m_speller;
};
#endif
