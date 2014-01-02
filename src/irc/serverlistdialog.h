/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2004 Peter Simonsson <psn@linux.se>
  Copyright (C) 2006-2008 Eike Hein <hein@kde.org>
*/

#ifndef KONVERSATIONSERVERLISTDIALOG_H
#define KONVERSATIONSERVERLISTDIALOG_H

#include "common.h"
#include "servergroupsettings.h"
#include "servergroupmodel.h"
#include "ui_serverlistdialogui.h"
#include <kdialog.h>

#include <QSortFilterProxyModel>

class ConnectionSettings;
class QCheckBox;

namespace Konversation
{
    class ServerListDialog : public KDialog, private Ui::ServerListDialogUI
    {
        Q_OBJECT

        public:
            explicit ServerListDialog(const QString& title, QWidget *parent = 0);
            ~ServerListDialog();

        signals:
            void connectTo(Konversation::ConnectionFlag flag, int serverGroupId);
            void connectTo(Konversation::ConnectionFlag flag, ConnectionSettings connectionSettings);
            void serverGroupsChanged(const Konversation::ServerGroupSettingsPtr serverGroup = Konversation::ServerGroupSettingsPtr());

        protected slots:
            virtual void slotOk();
            void slotClose();
            void slotAdd();
            void slotEdit();
            void slotDelete();

            void slotSetGroupExpanded(const QModelIndex& index);
            void slotSetGroupCollapsed(const QModelIndex& index);

            void updateButtons();

            void setShowAtStartup(bool show);

        protected:
            void addServerGroup(ServerGroupSettingsPtr serverGroup);

        private:
            int selectedChildrenCount(const QModelIndex& parent);

            ServerGroupFilterModel* m_serverModel;

    };
}
#endif
