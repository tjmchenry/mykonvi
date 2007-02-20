/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002-2004 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2004-2007 Shintaro Matsuoka <shin@shoegazed.org>
  Copyright (C) 2004,2005 John Tapsell <john@geola.co.uk>
*/

#include <qhostaddress.h>

#include "preferences.h"

#include "dcctransfer.h"

DccTransfer::DccTransfer( DccType dccType, const QString& partnerNick )
{
    kdDebug() << "DccTransfer::DccTransfer()" << endl;
    m_type = dccType;
    m_partnerNick = partnerNick;

    m_status = Configuring;
    m_resumed = false;
    m_transferringPosition = 0;
    m_transferStartPosition = 0;
    m_timeOffer = QDateTime::currentDateTime();

    m_bufferSize = Preferences::dccBufferSize();
    m_buffer = new char[ m_bufferSize ];

    connect( &m_loggerTimer, SIGNAL( timeout() ), this, SLOT( logTransfer() ) );
}

DccTransfer::~DccTransfer()
{
    kdDebug() << "DccTransfer::~DccTransfer()" << endl;
    delete[] m_buffer;
    m_loggerTimer.stop();
}

DccTransfer::DccTransfer( const DccTransfer& obj )
    : QObject()
{
    m_buffer = 0;
    m_bufferSize = 0;
    m_currentSpeed = obj.getCurrentSpeed();
    m_status = obj.getStatus();
    m_statusDetail = obj.getStatusDetail();
    m_type = obj.getType();
    m_fileName = obj.getFileName();
    m_fileSize = obj.getFileSize();
    m_fileURL = obj.getFileURL();
    // m_loggerBaseTime
    // m_loggerTimer
    m_ownIp = obj.getOwnIp();
    m_ownPort = obj.getOwnPort();
    m_partnerIp = obj.getPartnerIp();
    m_partnerNick = obj.getPartnerNick();
    m_partnerPort = obj.getPartnerPort();
    m_resumed = obj.isResumed();
    m_timeLeft = obj.getTimeLeft();
    m_timeOffer = obj.getTimeOffer();
    m_timeTransferFinished = obj.getTimeTransferFinished();
    m_timeTransferStarted = obj.getTimeTransferStarted();
    // m_transferLogPosition
    // m_transferLogTime
    m_transferringPosition = obj.getTransferringPosition();
    m_transferStartPosition = obj.getTransferStartPosition();
}

void DccTransfer::setFileURL( const KURL& url )
{
    // FIXME

    if ( getStatus() == Queued )
    {
        m_fileURL = url;
    }
}

void DccTransfer::queue()
{
    if ( getStatus() == Configuring )
        setStatus( Queued );
}

void DccTransfer::startTransferLogger()
{
    m_timeTransferStarted = QDateTime::currentDateTime();
    m_loggerBaseTime.start();
    m_loggerTimer.start( 100 );
}

void DccTransfer::finishTransferLogger()
{
    if ( m_timeTransferFinished.isNull() )
        m_timeTransferFinished = QDateTime::currentDateTime();
    m_loggerTimer.stop();
    updateTransferMeters();
}

// called by m_loggerTimer
void DccTransfer::logTransfer()
{
    m_transferLogTime.append( m_loggerBaseTime.elapsed() );
    m_transferLogPosition.append( m_transferringPosition );
    updateTransferMeters();
}

void DccTransfer::setStatus( DccStatus status, const QString& statusDetail )
{
    bool changed = ( status != m_status );
    DccStatus oldStatus = m_status;
    m_status = status;
    m_statusDetail = statusDetail;
    if ( changed )
        emit statusChanged( this, m_status, oldStatus );
}

void DccTransfer::updateTransferMeters()
{
    const int timeToCalc = 5;

    if ( getStatus() == Transferring )
    {
        // update CurrentSpeed

        // remove too old data
        QValueList<int>::iterator itTime = m_transferLogTime.begin();
        QValueList<KIO::fileoffset_t>::iterator itPos = m_transferLogPosition.begin();
        while ( itTime != m_transferLogTime.end() && ( m_transferLogTime.last() - (*itTime) > timeToCalc * 1000 ) )
        {
            itTime = m_transferLogTime.remove( itTime );
            itPos = m_transferLogPosition.remove( itPos );
        }

        // shift the base of the time (m_transferLoggerBaseTime)
        // reason: QTime can't handle a time longer than 24 hours
        int shiftOffset = m_loggerBaseTime.restart();
        itTime = m_transferLogTime.begin();
        for ( ; itTime != m_transferLogTime.end() ; ++itTime )
            (*itTime) = (*itTime) - shiftOffset;

        if ( m_transferLogTime.count() >= 2 )
            m_currentSpeed = (double)( m_transferLogPosition.last() - m_transferLogPosition.front() ) / (double)( m_transferLogTime.last() - m_transferLogTime.front() ) * 1000;
        else // avoid zero devision
            m_currentSpeed = DccTransfer::Calculating;

        // update the remaining time
        if ( m_currentSpeed <= 0 )
            m_timeLeft = DccTransfer::InfiniteValue;
        else
            m_timeLeft = (int)( (double)( m_fileSize - m_transferringPosition ) / m_currentSpeed );
    }
    else if ( m_status >= Done )
    {
        //m_averageSpeed = (double)( m_transferringPosition - m_transferStartPosition ) / (double)m_timeTransferStarted.secsTo( m_timeTransferFinished );
        m_currentSpeed = 0;
        m_timeLeft = 0;
    }
    else
    {
        m_currentSpeed = 0;
        m_timeLeft = DccTransfer::NotInTransfer;
    }
}

//FIXME: IPv6 support
QString DccTransfer::getNumericalIpText( const QString& ipString )
{
    QHostAddress ip;
    ip.setAddress( ipString );

    return QString::number( ip.ip4Addr() );
}

unsigned long DccTransfer::intel( unsigned long value )
{
    value = ( (value & 0xff000000) >> 24 ) +
        ( (value & 0xff0000) >> 8 ) +
        ( (value & 0xff00) << 8 ) +
        ( (value & 0xff) << 24 );

    return value;
}

DccTransfer::DccType DccTransfer::getType() const
{
    return m_type;
}

DccTransfer::DccStatus DccTransfer::getStatus() const 
{
    return m_status;
}

const QString& DccTransfer::getStatusDetail() const
{
    return m_statusDetail;
}

QDateTime DccTransfer::getTimeOffer() const 
{
    return m_timeOffer; 
}

QString DccTransfer::getOwnIp() const 
{
    return m_ownIp; 
}

QString DccTransfer::getOwnPort() const 
{
    return m_ownPort; 
}

QString DccTransfer::getPartnerNick() const 
{
    return m_partnerNick; 
}

QString DccTransfer::getPartnerIp() const 
{
    return m_partnerIp; 
}

QString DccTransfer::getPartnerPort() const 
{
    return m_partnerPort; 
}

QString DccTransfer::getFileName() const 
{
    return m_fileName; 
}

KIO::filesize_t DccTransfer::getFileSize() const 
{
    return m_fileSize; 
}

KIO::fileoffset_t DccTransfer::getTransferringPosition() const
{
    return m_transferringPosition; 
}

KIO::fileoffset_t DccTransfer::getTransferStartPosition() const
{
    return m_transferStartPosition;
}

KURL DccTransfer::getFileURL() const 
{
    return m_fileURL;
}

bool DccTransfer::isResumed() const 
{
    return m_resumed; 
}

long DccTransfer::getCurrentSpeed() const
{
    return (unsigned long)m_currentSpeed;
}

int DccTransfer::getTimeLeft() const
{
    return m_timeLeft;
}

int DccTransfer::getProgress() const
{
    return (int)( ( (double)getTransferringPosition() / (double)getFileSize() ) * 100 );
}

QDateTime DccTransfer::getTimeTransferStarted() const
{
    return m_timeTransferStarted;
}

QDateTime DccTransfer::getTimeTransferFinished() const
{
    return m_timeTransferFinished;
}

#include "dcctransfer.moc"
