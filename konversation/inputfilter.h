/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  inputfilter.h  -  description
  begin:     Fri Jan 25 2002
  copyright: (C) 2002 by Dario Abatianni
  email:     eisfuchs@tigress.com

  $Id$
*/

#include <qobject.h>

#ifndef INPUTFILTER_H
#define INPUTFILTER_H

/*
  @author Dario Abatianni
*/

class Server;

class InputFilter : public QObject
{
  Q_OBJECT

  public:
    InputFilter();
    ~InputFilter();

    void setServer(Server* newServer);
    void parseLine(QString line);

  signals:
    void welcome();
    void notifyResponse(QString nicksOnline);

  protected:
    void parseClientCommand(QString& prefix,QString& command,QStringList& parameterList,QString& trailing);
    void parseServerCommand(QString& prefix,QString& command,QStringList& parameterList,QString& trailing);
    void parseModes(QString sourceNick,QStringList parameterList);

    Server* server;
    bool welcomeSent;
};

#endif
