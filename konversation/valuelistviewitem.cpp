/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  valuelistviewitem.cpp  -  List view item that carries an arbitrary value
  begin:     Fre Apr 25 2003
  copyright: (C) 2003 by Dario Abatianni
  email:     eisfuchs@tigress.com

  $Id$
*/

#include "valuelistviewitem.h"

ValueListViewItem::ValueListViewItem(int newValue,KListView* parent,QString label)
                 : KListViewItem(parent,label)
{
  value=newValue;
}


ValueListViewItem::~ValueListViewItem()
{
}

int ValueListViewItem::getValue()
{
  return value;
}
