/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  copyright: (C) 2004 by Peter Simonsson
  email:     psn@linux.se
*/
#include "insertchardialog.h"

#include <kcharselect.h>
#include <klocale.h>
#include <kguiitem.h>


namespace Konversation
{

    InsertCharDialog::InsertCharDialog(const QString& font, QWidget *parent, const char *name)
        : KDialog(parent)
    {
        setButtons( KDialog::Ok | KDialog::Close );
        setDefaultButton( KDialog::Ok );
        setModal( false );
        setCaption(  i18n("Insert Character") );
        setButtonGuiItem(KDialog::Ok, KGuiItem(i18n("&Insert"), "ok", i18n("Insert a character")));

        m_charTable = new KCharSelect(this, KCharSelect::CharacterTable);
        m_charTable->setCurrentFont( QFont( font ) );
        setMainWidget(m_charTable);
        connect( this, SIGNAL( okClicked() ), this, SLOT( slotOk ) );
        connect(m_charTable, SIGNAL(doubleClicked()), this, SLOT(slotOk()));
    }

    InsertCharDialog::~InsertCharDialog()
    {
    }

    void InsertCharDialog::setFont(const QFont &font)
    {
        m_charTable->setFont(font.family());
    }

    QChar InsertCharDialog::chr()
    {
        return m_charTable->currentChar();
    }

    void InsertCharDialog::slotOk()
    {
        emit insertChar(m_charTable->currentChar());
    }

}

#include "insertchardialog.moc"