/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  dcctransfer.cpp  -  description
  begin:     Mit Aug 7 2002
  copyright: (C) 2002 by Dario Abatianni
  email:     eisfuchs@tigress.com
*/

#include <qhostaddress.h>
#include <qstyle.h>
#include <qtimer.h>

#include <kdebug.h>
#include <kglobal.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kprogress.h>

#include "dccpanel.h"
#include "dcctransfer.h"
#include "konversationapplication.h"

DccTransfer::DccTransfer(KListView* _parent, DccType _dccType, const QString& _partnerNick)
  : KListViewItem(_parent)
{
  dccType = _dccType;
  partnerNick = _partnerNick;
  
  dccStatus = Queued;
  bResumed = false;
  transferringPosition = 0;
  transferStartPosition = 0;
  partnerIp = QString::null;
  partnerPort = QString::null;
  ownIp = QString::null;
  ownPort = QString::null;
  timeOffer = QDateTime::currentDateTime();
  
  bufferSize = KonversationApplication::preferences.getDccBufferSize();
  buffer = new char[bufferSize];
  
  autoUpdateViewTimer = 0;
  
  progressBar = new KProgress(100);
  progressBar->setCenterIndicator(true);
  progressBar->setPercentageVisible(true);
  
  // FIXME: we shouldn't do these init in the instance constructer
  TypeText[Send]    = i18n("Send");
  TypeText[Receive] = i18n("Receive");
  
  StatusText[Queued]        = i18n("Queued");
  StatusText[WaitingRemote] = i18n("Offering");
  StatusText[LookingUp]     = i18n("Lookup");
  StatusText[Connecting]    = i18n("Connecting");
  StatusText[Sending]       = i18n("Sending");
  StatusText[Receiving]     = i18n("Receiving");
  StatusText[Failed]        = i18n("Failed");
  StatusText[Aborted]       = i18n("Aborted");
  StatusText[Done]          = i18n("Done");
}

DccTransfer::~DccTransfer()
{
  delete[] buffer;
  delete progressBar;
  stopAutoUpdateView();
}

void DccTransfer::updateView()  // slot
{
  setPixmap(DccPanel::Column::TypeIcon, getTypeIcon());
  setPixmap(DccPanel::Column::Status,   getStatusIcon());
  
  setText(DccPanel::Column::OfferDate,     timeOffer.toString("hh:mm:ss"));
  setText(DccPanel::Column::Status,        getStatusText());
  setText(DccPanel::Column::FileName,      fileName);
  setText(DccPanel::Column::PartnerNick,   partnerNick);
  //setText(DccPanel::Column::Progress,      getProgressPrettyText());
  setText(DccPanel::Column::Position,      getPositionPrettyText());
  setText(DccPanel::Column::TimeRemaining, getTimeRemainingPrettyText());
  setText(DccPanel::Column::CPS,           getCPSPrettyText());
}

void DccTransfer::paintCell(QPainter* painter, const QColorGroup& colorgroup, int column, int width, int alignment)
{
  KListViewItem::paintCell(painter, colorgroup, column, width, alignment);
  
  if(column==DccPanel::Column::Progress)  // Render progress bar
  {
    progressBar->setProgress((int)(100*transferringPosition/fileSize));
    
    progressBar->setFixedWidth(width);
    progressBar->setFixedHeight(height());
    QRect rectangle = progressBar->contentsRect();
    
    int styleflags = QStyle::Style_Default;
    if ( isSelected() )
      styleflags |= QStyle::Style_Selected;
  
    progressBar->style().drawControl( QStyle::CE_ProgressBarGroove,
                                      painter,
                                      progressBar,
                                      rectangle,
                                      progressBar->colorGroup(),
                                      styleflags );
    progressBar->style().drawControl( QStyle::CE_ProgressBarContents,
                                      painter,
                                      progressBar,
                                      rectangle,
                                      progressBar->colorGroup(),
                                      styleflags );
    progressBar->style().drawControl( QStyle::CE_ProgressBarLabel,
                                      painter,
                                      progressBar,
                                      rectangle,
                                      progressBar->colorGroup(),
                                      styleflags );
  }
}

void DccTransfer::startAutoUpdateView()
{
  stopAutoUpdateView();
  autoUpdateViewTimer = new QTimer(this);
  connect(autoUpdateViewTimer, SIGNAL(timeout()), this, SLOT(updateView()));
  autoUpdateViewTimer->start(1000);
}

void DccTransfer::stopAutoUpdateView()
{
  if(autoUpdateViewTimer)
  {
    autoUpdateViewTimer->stop();
    delete autoUpdateViewTimer;
    autoUpdateViewTimer = 0;
  }
}

void DccTransfer::setStatus(DccStatus status)
{
  DccStatus oldStatus=dccStatus;
  dccStatus=status;
  if (oldStatus!=status) emit statusChanged(this);
}

QString DccTransfer::getTypeText() const
{
  return TypeText[dccType];
}

QPixmap DccTransfer::getTypeIcon() const
{
  QString icon;
  if(dccType == Send)
    icon = "up";
  else if(dccType == Receive)
    icon = "down";
  return KGlobal::iconLoader()->loadIcon(icon, KIcon::Small);
}

QPixmap DccTransfer::getStatusIcon() const
{
  QString icon;
  switch(dccStatus)
  {
    case Queued:
      icon = "player_stop";
      break;
    case WaitingRemote:
      icon = "goto";
      break;
    case Sending:
    case Receiving:
      icon = "player_play";
      break;
    case Aborted:
    case Failed:
      icon = "stop";
      break;
    case Done:
      icon = "ok";
      break;
    default: ; // sugar
  }
  return KGlobal::iconLoader()->loadIcon(icon, KIcon::Small);
}

QString DccTransfer::getStatusText() const
{
  return StatusText[dccStatus];
}

QString DccTransfer::getFileSizePrettyText() const
{
  // TODO: make it pretty!
  return QString::number(fileSize) + "bytes";
}

QString DccTransfer::getPositionPrettyText() const
{
  return QString::number(transferringPosition) + " / " + QString::number(fileSize);
}

QString DccTransfer::getProgressPrettyText() const
{
  return QString::number((int)(100*transferringPosition/fileSize)) + "%";
}

QString DccTransfer::getTimeRemainingPrettyText() const
{
  if(dccStatus != Sending && dccStatus != Receiving )
    return QString::null;
  if(!fileSize)
    return i18n("unknown");
  // not use getCPS() for exact result
  int trnsfdTime = timeTransferStarted.secsTo(QDateTime::currentDateTime());
  unsigned long trnsfdBytes = transferringPosition - transferStartPosition;
  if(!trnsfdBytes)
    return QString::null;  // anyway we cannot anticipate exactly
  unsigned long remBytes = fileSize - transferringPosition;
  int remTime = (int)((double)remBytes / (double)trnsfdBytes * trnsfdTime);
  int remHour = remTime / 3600; remTime -= remHour * 3600;
  int remMin = remTime / 60; remTime -= remMin * 60;
  QString text;
  if(remHour)
    text += QString::number(remHour) + ":";
  if(remMin)
    text += QString::number(remMin) + ":";
  if(text.isEmpty())
    text = QString::number(remTime) + i18n(" sec");
  else
    text += QString::number(remTime);
  return text;
}

QString DccTransfer::getCPSPrettyText() const
{
  // TODO: make it pretty!
  return QString::number(getCPS()) + " B/s";
}

unsigned long DccTransfer::getCPS() const
{
  int elapsed=timeTransferStarted.secsTo(QDateTime::currentDateTime());
  if(elapsed == 0 && transferringPosition-transferStartPosition > 0)
    elapsed = 1;
  // prevent division by zero
  return elapsed ? (transferringPosition-transferStartPosition)/elapsed : 0;
}

QString DccTransfer::getNumericalIpText(const QString& _ip)  // static
{
  QHostAddress ip;
  ip.setAddress(_ip);
  
  return QString::number(ip.ip4Addr());
}

QString DccTransfer::getErrorString(int code)  // static
{
  QString errorString(QString::null);

  switch(code)
  {
    case IO_Ok:
      errorString=i18n("The operation was successful. Should never happen in an error dialog.");
    break;
    case IO_ReadError:
      errorString=i18n("Could not read from file \"%1\".");
    break;
    case IO_WriteError:
      errorString=i18n("Could not write to file \"%1\".");
    break;
    case IO_FatalError:
      errorString=i18n("A fatal unrecoverable error occurred.");
    break;
    case IO_OpenError:
      errorString=i18n("Could not open file \"%1\".");
    break;

        // Same case value? Damn!
//        case IO_ConnectError:
//          errorString="Could not connect to the device.";
//        break;

    case IO_AbortError:
      errorString=i18n("The operation was unexpectedly aborted.");
    break;
    case IO_TimeOutError:
      errorString=i18n("The operation timed out.");
    break;
    case IO_UnspecifiedError:
      errorString=i18n("An unspecified error happened on close.");
    break;
    default:
      errorString=i18n("Unknown error. Code %1").arg(code);
    break;
  }

  return errorString;
}

unsigned long DccTransfer::intel(unsigned long value)  // static
{
  value=((value & 0xff000000) >> 24) +
        ((value & 0xff0000) >> 8) +
        ((value & 0xff00) << 8) +
        ((value & 0xff) << 24);

  return value;
}

QString DccTransfer::TypeText[DccTransfer::DccTypeCount];
QString DccTransfer::StatusText[DccTransfer::DccStatusCount];

#include "dcctransfer.moc"
