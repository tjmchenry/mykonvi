/***************************************************************************
                          ledlistviewitem.cpp  -  A list view with led indicator
                             -------------------
    begin                : Thu Jul 25 2002
    copyright            : (C) 2002 by Matthias Gierlings
    email                : gismore@users.sourceforge.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "ledlistviewitem.h"
#include "konversationapplication.h"

LedListViewItem::LedListViewItem(KListView* parent,const QString &passed_label,bool passed_opState,bool passed_voiceState) :
                   KListViewItem(parent, passed_label)
{
  opState=passed_opState;
  voiceState=passed_voiceState;

  currentLeds=leds.getLed(KonversationApplication::preferences.getOpLedColor(),true);
  opLedOn    =currentLeds.pixmap(QIconSet::Automatic, QIconSet::Active, QIconSet::On);

  currentLeds=leds.getLed(KonversationApplication::preferences.getNoRightsLedColor(),false);
  voiceLedOff=currentLeds.pixmap(QIconSet::Automatic, QIconSet::Active, QIconSet::Off);

  currentLeds=leds.getLed(KonversationApplication::preferences.getVoiceLedColor(),true);
  voiceLedOn =currentLeds.pixmap(QIconSet::Automatic, QIconSet::Active, QIconSet::On);

  // separate LED from Text a little more
  // TODO: Find out if we can align pixmaps vertically centered
  listView()->setColumnWidth(0,opLedOn.width()+2);
  listView()->setColumnAlignment(0,Qt::AlignHCenter);
  listView()->setColumnAlignment(1,Qt::AlignLeft);

  setText(0,QString::null);
  setState(opState,voiceState);
}

LedListViewItem::~LedListViewItem()
{
}

void LedListViewItem::setState(bool passed_opState,bool passed_voiceState)
{
  opState=passed_opState;
  voiceState=passed_voiceState;

  if(opState)
    setPixmap(0,opLedOn);
  else if(voiceState)
    setPixmap(0,voiceLedOn);
  else
    setPixmap(0,voiceLedOff);

  repaint();
}

void LedListViewItem::toggleOpState()
{
  setState(!opState,voiceState);
  repaint();
}

void LedListViewItem::toggleVoiceState()
{
  setState(opState,!voiceState);
  repaint();
}

bool LedListViewItem::getOpState()    { return opState; }
bool LedListViewItem::getVoiceState() { return voiceState; }
