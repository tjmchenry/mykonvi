/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  nicklistview.h  -  Channel Nick List, including context menu
  begin:     Fre Jun 7 2002
  copyright: (C) 2002 by Dario Abatianni
  email:     eisfuchs@tigress.com

  $Id$
*/


#ifndef NICKLISTVIEW_H
#define NICKLISTVIEW_H

#include <qevent.h>
#include <qpopupmenu.h>

#include <klistview.h>

/*
  @author Dario Abatianni
*/

class NickListView : public KListView
{
  Q_OBJECT

  public:
    NickListView(QWidget* parent);
    ~NickListView();

    enum PopupIDs
    {
      Modes,GiveOp,TakeOp,GiveVoice,TakeVoice,
      Kick,KickBan,BanNick,BanHostmask,
      Whois,Version,Ping,Query,
      CustomID
    };

  signals:
    /* Will be connected to Channel::popupCommand(int) */
    void popupCommand(int id);

  protected:
    void contextMenuEvent(QContextMenuEvent *ce);

    QPopupMenu* popup;
    QPopupMenu* modes;
};

#endif
