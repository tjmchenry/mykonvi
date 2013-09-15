/*
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License or (at your option) version 3 or any later version
  accepted by the membership of KDE e.V. (or its successor appro-
  ved by the membership of KDE e.V.), which shall act as a proxy
  defined in Section 14 of version 3 of the license.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see http://www.gnu.org/licenses/.
*/

/*
  Copyright (C) 2012 Eike Hein <hein@kde.org>
*/

#ifndef TOPICHISTORYMODEL_H
#define TOPICHISTORYMODEL_H

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QDateTime>
#include "channel.h"


#ifdef HAVE_QCA2
namespace Konversation
{
    class Cipher;
}

class CipherFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

    public:
        explicit CipherFilterProxyModel(QObject* parent = 0);
        ~CipherFilterProxyModel();

        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

        void setCipher(Konversation::Cipher* cipher) { m_cipher = cipher; }
        void clearCipher() { m_cipher = 0; }
        bool hasCipher() { return (m_cipher ? true : false); }

    private:
        Konversation::Cipher* m_cipher;
};
#endif


struct Topic
{
    QString author;
    QString text;
    QDateTime timestamp;

    bool operator==(const Topic& other) const
    {
        return (author == other.author
            && text == other.text
            && timestamp == other.timestamp);
    }
};

class TopicHistoryModel : public QAbstractListModel
{
    Q_OBJECT

    public:
        explicit TopicHistoryModel(QObject* parent = 0);
        ~TopicHistoryModel();

        QString currentTopic();

        void appendTopic(const QString& text, const QString& author = QString(), QDateTime timestamp = QDateTime::currentDateTime());
        void setCurrentTopicMetadata(const QString& author, QDateTime timestamp = QDateTime::currentDateTime());

        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
        QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

        int columnCount(const QModelIndex& parent = QModelIndex()) const;
        int rowCount(const QModelIndex& parent = QModelIndex()) const;

        static QString authorPlaceholder();


    signals:
        void currentTopicChanged();


    private:
        QList<Topic> m_topicList;

};

#endif
