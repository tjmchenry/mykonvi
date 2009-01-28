#ifndef SSLLABEL_H
#define SSLLABEL_H

/*
  Copyright (c) 2004 by İsmail Dönmez <ismail.donmez@boun.edu.tr>

  *************************************************************************
  *                                                                       *
  * This program is free software; you can redistribute it and/or modify  *
  * it under the terms of the GNU General Public License as published by  *
  * the Free Software Foundation; either version 2 of the License, or     *
  * (at your option) any later version.                                   *
  *                                                                       *
  *************************************************************************

 */

#include <qlabel.h>
//Added by qt3to4:
#include <QMouseEvent>

class SSLLabel : public QLabel
{
    Q_OBJECT

        public:
        SSLLabel(QWidget* parent, const char* name);

    protected:
        void mouseReleaseEvent(QMouseEvent *e);

        signals:
        void clicked();
};
#endif
