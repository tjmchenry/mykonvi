/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2002-2004 Dario Abatianni <eisfuchs@tigress.com>
  Copyright (C) 2004-2006 Shintaro Matsuoka <shin@shoegazed.org>
  Copyright (C) 2004,2005 John Tapsell <john@geola.co.uk>
*/

#ifndef DCCTRANSFER_H
#define DCCTRANSFER_H

#include <qdatetime.h>
#include <qobject.h>
#include <qtimer.h>

#include <kurl.h>
#include <kio/global.h>

#define TIME_REMAINING_NOT_AVAILABLE -1
#define TIME_REMAINING_INFINITE      -2

#define CPS_CALCULATING -1
#define CPS_NOT_IN_TRANSFER -2

class DccTransfer : public QObject
{
    Q_OBJECT

    public:
        enum DccType
        {
            Send,
            Receive,
            DccTypeCount
        };

        enum DccStatus
        {
            Queued = 0,                           // Newly added DCC, RECV: Waiting for local user's response
            Preparing,                            // Opening KIO to write received data
            WaitingRemote,                        // Waiting for remote host's response
            Connecting,                           // RECV: trying to connect to the server
            Sending,                              // Sending
            Receiving,                            // Receiving
            Done,                                 // Transfer done
            Failed,                               // Transfer failed
            Aborted,                              // Transfer aborted by user
            DccStatusCount
        };

        DccTransfer( DccType dccType, const QString& partnerNick );
        virtual ~DccTransfer();

        DccType            getType()                  const;
        DccStatus          getStatus()                const;
        const QString&     getStatusDetail()          const;
        QDateTime          getTimeOffer()             const;
        QString            getOwnIp()                 const;
        QString            getOwnPort()               const;
        QString            getPartnerNick()           const;
        QString            getPartnerIp()             const;
        QString            getPartnerPort()           const;
        QString            getFileName()              const;
        KIO::filesize_t    getFileSize()              const;
        KIO::fileoffset_t  getTransferringPosition()  const;
        KURL               getFileURL()               const;
        bool               isResumed()                const;
        long               getCPS()                   ;
        int                getTimeRemaining()         ;
        int                getProgress()              const;

    signals:
        void transferStarted( DccTransfer* item );
        void done( DccTransfer* item );
        void statusChanged( DccTransfer* item, int newStatus, int oldStatus );

    public slots:
        virtual void start() = 0;
        virtual void abort() = 0;

    protected:
        void setStatus( DccStatus status, const QString& statusDetail = QString::null );
        void startTransferLogger();
        void finishTransferLogger();

        static QString getNumericalIpText( const QString& ipString );
        static unsigned long intel( unsigned long value );

    protected slots:
        void logTransfer();

    protected:
        // transfer information
        DccType m_dccType;
        DccStatus m_dccStatus;
        QString m_dccStatusDetail;
        bool m_resumed;
        KIO::fileoffset_t m_transferringPosition;
        KIO::fileoffset_t m_transferStartPosition;

        /*
        QValueList<QDateTime> m_transferTimeLog;  // write per packet to calc CPS
        QValueList<KIO::fileoffset_t> m_transferPositionLog;  // write per packet to calc CPS
        */

        QString m_partnerNick;
        QString m_partnerIp;                      // null when unknown
        QString m_partnerPort;
        QString m_ownIp;
        QString m_ownPort;

        unsigned long m_bufferSize;
        char* m_buffer;

        /**
         * The filename.
         * For receiving, it holds the filename as the sender said.
         * So be careful, it can contain "../" and so on.
         */
        QString m_fileName;

        /** The file size of the complete file sending/recieving. */
        KIO::filesize_t  m_fileSize;

        /**
         * If we are sending a file, this is the url of the file we are sending.
         * If we are recieving a file, this is the url of the file we are saving
         * to in the end (Temporararily it will be filename+".part" ).
         */
        KURL m_fileURL;

    private:
        void updateTransferMeters();

    private:
        QDateTime m_timeOffer;
        QDateTime m_timeTransferStarted;
        //QDateTime m_timeLastActive;
        QDateTime m_timeTransferFinished;

        QTimer m_loggerTimer;
        QTime m_loggerBaseTime;           // it's used for calculating CPS
        QValueList<int> m_transferLogTime;
        QValueList<KIO::fileoffset_t> m_transferLogPosition;

        // transfer meters;
        double m_cps;                             // bytes(characters) per second
        int m_timeRemaining;
};

#endif  // DCCTRANSFER_H
