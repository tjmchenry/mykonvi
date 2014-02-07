/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  Copyright (C) 2004, 2007 Peter Simonsson <psn@linux.se>
  Copyright (C) 2006-2008 Eike Hein <hein@kde.org>
*/

#include "serverlistdialog.h"
#include "preferences.h"
#include "application.h"
#include "servergroupdialog.h"
#include "connectionsettings.h"

#include <QCheckBox>
#include <QHeaderView>

#include <KGuiItem>
#include <KMessageBox>
#include <KMenu>

namespace Konversation
{

    ServerListDialog::ServerListDialog(const QString& title, QWidget *parent)
    : KDialog(parent), Ui::ServerListDialogUI()
    {
        setCaption(title);
        setButtons(Ok|Close);

        setupUi(mainWidget());
        mainWidget()->layout()->setMargin(0);

        setButtonGuiItem(Ok, KGuiItem(i18n("C&onnect"), "network-connect", i18n("Connect to the server"), i18n("Click here to connect to the selected IRC network and channel.")));

        m_showAtStartup->setChecked(Preferences::self()->showServerList());
        connect(m_showAtStartup, SIGNAL(toggled(bool)), this, SLOT(setShowAtStartup(bool)));

        m_serverModel = new ServerGroupFilterModel(this);
        m_serverModel->setSourceModel(Preferences::serverGroupModel());

        m_serverList->setModel(m_serverModel);

        m_serverList->setFocus();

        connect(m_serverList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextMenu(QPoint)));
        connect(m_serverList, SIGNAL(activated(const QModelIndex&)), this, SLOT(slotOk()));
        connect(m_serverList->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this, SLOT(updateButtons()));
        connect(m_serverList, SIGNAL(expanded(const QModelIndex&)), this, SLOT(slotSetGroupExpanded(const QModelIndex&)));
        connect(m_serverList, SIGNAL(collapsed(const QModelIndex&)), this, SLOT(slotSetGroupCollapsed(const QModelIndex&)));
        connect(m_addButton, SIGNAL(clicked()), this, SLOT(slotAdd()));
        connect(m_editButton, SIGNAL(clicked()), this, SLOT(slotEdit()));
        connect(m_delButton, SIGNAL(clicked()), this, SLOT(slotDelete()));
        connect(this, SIGNAL(okClicked()), this, SLOT(slotOk()));
        connect(this, SIGNAL(cancelClicked()), this, SLOT(slotClose()));

        updateButtons();

        KConfigGroup config(KGlobal::config(), "ServerListDialog");
        QSize newSize = size();
        newSize = config.readEntry("Size", newSize);
        resize(newSize);
        m_serverList->header()->setMovable(false); // don't let the user reorder the header
        m_serverList->sortByColumn(0, Qt::AscendingOrder);
        m_serverList->header()->restoreState(config.readEntry<QByteArray>("ServerListHeaderState", QByteArray()));
        //because it sorts the first column in ascending order by default
        //causing problems and such.
        m_serverList->selectionModel()->select(m_serverModel->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

        restoreExpandedStates();

        restoreSpannedState();
    }

    ServerListDialog::~ServerListDialog()
    {
        KConfigGroup config(KGlobal::config(), "ServerListDialog");
        config.writeEntry("Size", size());
        config.writeEntry("ServerListHeaderState", m_serverList->header()->saveState());
    }

    void ServerListDialog::restoreExpandedStates()
    {
        m_serverList->setUpdatesEnabled(false);
        QModelIndex start = m_serverModel->index(0, 0);
        QModelIndexList parentItems = m_serverModel->match(start, ServerGroupModel::IsServerRole, 0, -1, Qt::MatchExactly | Qt::MatchWrap);

        foreach (QModelIndex parent, parentItems)
        {
            bool expanded = m_serverModel->data(parent, ServerGroupModel::ExpandedRole).toBool();
            m_serverList->setExpanded(parent, expanded);
        }

        m_serverList->setUpdatesEnabled(true);
    }

    void ServerListDialog::restoreSpannedState()
    {
        m_serverList->setUpdatesEnabled(false);

        QModelIndex start = m_serverModel->index(0, 0);
        QModelIndexList parentItems = m_serverModel->match(start, ServerGroupModel::IsServerRole, 0, -1, Qt::MatchExactly | Qt::MatchWrap);

        foreach (QModelIndex parent, parentItems)
        {
            start = parent.child(0, 0);
            QModelIndexList childItems = m_serverModel->match(start, ServerGroupModel::IsServerRole, 1, -1, Qt::MatchExactly | Qt::MatchWrap);

            foreach (QModelIndex child, childItems)
            {
                m_serverList->setFirstColumnSpanned(child.row(), child.parent(), true);
            }

        }

        m_serverList->setUpdatesEnabled(true);
    }

    void ServerListDialog::contextMenu(const QPoint& pos)
    {
        QModelIndex index = m_serverList->indexAt(pos);
        if (!index.isValid()) return;

        KMenu* menu = new KMenu(this);

        menu->addAction(i18n("&Edit..."), this, SLOT(slotEdit()));
        menu->addAction(i18n("&Delete..."), this, SLOT(slotDelete()));

        menu->exec(QCursor::pos());

        delete menu;
    }

    void ServerListDialog::slotClose()
    {
//         slotApply();
        accept();
    }

    void ServerListDialog::slotOk()
    {
        QModelIndexList selected = m_serverList->selectionModel()->selectedRows(0);
        foreach (QModelIndex index, selected)
        {
            QModelIndex srcIndex = m_serverModel->mapToSource(index);
            if (srcIndex.internalId() >= 0) // is server
            {
                ConnectionSettings settings;
                ServerGroupSettingsPtr serverGroup = Preferences::serverGroupById(srcIndex.internalId());
                settings.setServerGroup(serverGroup);

                settings.setServer(serverGroup->serverByIndex(srcIndex.row()));

                emit connectTo(Konversation::PromptToReuseConnection, settings);
            }
            else //is a servergroup
                emit connectTo(Konversation::PromptToReuseConnection, Preferences::serverGroupByIndex(srcIndex.row())->id());
        }
    }

    void ServerListDialog::slotAdd()
    {
        QPointer<ServerGroupDialog> dlg = new ServerGroupDialog(i18n("New Network"), this);

        if(dlg->exec() == KDialog::Accepted)
        {
            addServerGroup(dlg->serverGroupSettings());

            emit serverGroupsChanged(dlg->serverGroupSettings());
        }
        delete dlg;

        restoreSpannedState();
    }

    void ServerListDialog::slotEdit()
    {
        QModelIndex index = m_serverList->selectionModel()->selectedRows(0).first();

        if (index.isValid())
        {
            QModelIndex srcIndex = m_serverModel->mapToSource(index);
            Konversation::ServerGroupSettingsPtr serverGroup;

            if (srcIndex.parent().isValid())
                serverGroup = Preferences::serverGroupById(srcIndex.internalId());
            else
                serverGroup = Preferences::serverGroupByIndex(srcIndex.row());

            if (serverGroup)
            {
                QPointer<ServerGroupDialog> dlg = new ServerGroupDialog(i18n("Edit Network"), this);

                dlg->setServerGroupSettings(serverGroup);

                if (srcIndex.parent().isValid())
                {
                    if(dlg->execAndEditServer(serverGroup->serverByIndex(srcIndex.row())) == KDialog::Accepted)
                    {
                        addServerGroup(dlg->serverGroupSettings());
                    }
                }
                else
                {
                    if(dlg->exec() == KDialog::Accepted)
                    {
                        addServerGroup(dlg->serverGroupSettings());
                    }
                }

                delete dlg;
            }

            restoreSpannedState();
        }
    }

    void ServerListDialog::slotDelete()
    {
        //TODO trigger datachanged / rows removed

        QModelIndexList selected = m_serverList->selectionModel()->selectedRows(0);

        QModelIndex selectedRow;

        // Make sure we're not deleting a network's only servers
        foreach (QModelIndex index, selected)
        {
            QModelIndex srcIndex = m_serverModel->mapToSource(index);
            //if it has a parent that's also selected it'll be deleted anyway so no need to worry
            if (!srcIndex.parent().isValid() || !m_serverList->selectionModel()->isSelected(srcIndex.parent()))
            {
                if (srcIndex.internalId() >= 0)
                {
                    Konversation::ServerGroupSettingsPtr serverGroup = Preferences::serverGroupById(srcIndex.internalId());

                    if (serverGroup && serverGroup->serverList().count() == 1)
                    {
                        KMessageBox::error(this, i18n("You cannot delete %1.\n\nThe network %2 needs to have at least one server.", index.data().toString(), index.parent().data().toString()));

                        return;
                    }
                    else if (serverGroup && serverGroup->serverList().count() == selectedChildrenCount(srcIndex.parent()))
                    {
                        KMessageBox::error(this, i18np("You cannot delete the selected server.\n\nThe network %2 needs to have at least one server.",
                                            "You cannot delete the selected servers.\n\nThe network %2 needs to have at least one server.",
                                            selectedChildrenCount(index.parent()),
                                            index.parent().data().toString()));

                        return;
                    }
                }
            }
        }

        if (selected.isEmpty())
            return;

        // Ask the user if he really wants to delete what he selected
        QString question;

        if (selected.count() > 1)
            question = i18n("Do you really want to delete the selected entries?");
        else
            question = i18n("Do you really want to delete %1?", selected.first().data().toString());

        if (KMessageBox::warningContinueCancel(this, question) == KMessageBox::Cancel)
        {
            return;
        }

        // Have fun deleting
        foreach (QModelIndex index, selected)
        {
            QModelIndex srcIndex = m_serverModel->mapToSource(index);

            if (index.parent().isValid())
            {
                if (!selectedRow.isValid() || selectedRow == index || (selectedRow.isValid() && selectedRow.parent().row() > index.parent().row()) ||
                    (selectedRow.parent() == index.parent() && selectedRow.row() > index.row()))
                {
                    if (index.row() > 0 && m_serverModel->rowCount(index.parent()) > 1)
                        selectedRow = index.sibling(index.row()-1, 0);
                    else if (index.row() == 0 && m_serverModel->rowCount(index.parent()) > 1)
                        selectedRow = index.sibling(0, 0);
                    else if (index.row() == 0 && m_serverModel->rowCount(index.parent()) <= 1)
                    {
                        if (index.parent().row() > 0)
                            selectedRow = index.parent().sibling(index.parent().row()-1, 0);
                        else if (index.parent().row() == 0 && m_serverModel->rowCount() > 1)
                            selectedRow = index.parent().sibling(0, 0);
                        else
                            selectedRow = QModelIndex();
                    }
                    else
                        selectedRow = QModelIndex();
                }

                Konversation::ServerGroupSettingsPtr serverGroup = Preferences::serverGroupById(srcIndex.internalId());
                serverGroup->removeServer(serverGroup->serverByIndex(srcIndex.row()));
            }
            else
            {
                if (index.row() > 0)
                    selectedRow = index.sibling(index.row()-1, 0);
                else if (index.row() == 0 && m_serverModel->rowCount() > 1)
                    selectedRow = index.sibling(0, 0);
                else
                    selectedRow = QModelIndex();

                Preferences::removeServerGroup(Preferences::serverGroupByIndex(srcIndex.row())->id());
            }
        }

        // Select the row above the first deleted item, or the first row, if any.

        m_serverList->selectionModel()->select(selectedRow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
   }

    void ServerListDialog::slotSetGroupExpanded(const QModelIndex& index)
    {
        QModelIndex srcIndex = m_serverModel->mapToSource(index);

        if (!srcIndex.parent().isValid() && Preferences::serverGroupList().count() > srcIndex.row())
        {
            Konversation::ServerGroupSettingsPtr serverGroup = Preferences::serverGroupByIndex(srcIndex.row());
            serverGroup->setExpanded(true);
        }
    }

    void ServerListDialog::slotSetGroupCollapsed(const QModelIndex& index)
    {
        QModelIndex srcIndex = m_serverModel->mapToSource(index);

        if (!srcIndex.parent().isValid() && Preferences::serverGroupList().count() > srcIndex.row())
        {
            Konversation::ServerGroupSettingsPtr serverGroup = Preferences::serverGroupByIndex(srcIndex.row());
            serverGroup->setExpanded(false);

            QModelIndexList selected = m_serverList->selectionModel()->selectedRows(0);

            bool selectParent = false;
            foreach (QModelIndex i, selected)
            {
                if (i.parent() == index)
                {
                    selectParent = true;
                    m_serverList->selectionModel()->select(i, QItemSelectionModel::Deselect);
                }
            }

            if (selectParent)
                m_serverList->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    void ServerListDialog::updateButtons()
    {
        int count = m_serverList->selectionModel()->selectedRows(0).count();
        bool enable = (count > 0);

        enableButtonOk(enable);
        m_delButton->setEnabled(enable);

        enable = (count == 1);
        m_editButton->setEnabled(enable);
    }

    void ServerListDialog::addServerGroup(ServerGroupSettingsPtr serverGroup)
    {
        Preferences::addServerGroup(serverGroup);

        int row = m_serverModel->getServerGroupIndexById(serverGroup->id());
        QModelIndex srcIndex = m_serverModel->sourceModel()->index(row, 0);
        QModelIndex index = m_serverModel->mapFromSource(srcIndex);

        m_serverList->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    void ServerListDialog::setShowAtStartup(bool show)
    {
        Preferences::self()->setShowServerList(show);
    }

    int ServerListDialog::selectedChildrenCount(const QModelIndex& parent)
    {
        if (parent.isValid())
        {
            int count = 0;

            int max = Preferences::serverGroupByIndex(parent.row())->serverList().count();

            for (int i = 0; i < max; i++)
            {
                if (parent.child(i, 0).isValid() && m_serverList->selectionModel()->isSelected(parent.child(i, 0))) count++;
            }

            return count;
        }

        return 0;
    }
}

#include "serverlistdialog.moc"
