/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

/*
  prefspageappearance.h  -  The preferences panel that holds the appearance settings
  begin:     Son Dez 22 2002
  copyright: (C) 2002 by Dario Abatianni
             (C) 2004 by Peter Simonsson
  email:     eisfuchs@tigress.com
*/

#ifndef PREFSPAGEAPPEARANCE_H
#define PREFSPAGEAPPEARANCE_H

#include <qfont.h>
#include <qstringlist.h>
#include <qptrlist.h>

#include <kcolorbutton.h>

#include "prefspage.h"

/**
  @author Dario Abatianni
*/

class QLabel;
class QCheckBox;
class QSpinBox;
class QColor;

class KListView;
class KURLRequester;

class PrefsPageAppearance : public PrefsPage
{
  Q_OBJECT

  public:
    PrefsPageAppearance(QFrame* newParent,Preferences* newPreferences);
    ~PrefsPageAppearance();

  public slots:
    void applyPreferences();

  protected slots:
    void textFontClicked();
    void listFontClicked();

    void timestampingChanged(int state);
            
  protected:
    void updateFonts();

    QLabel* textPreviewLabel;
    QLabel* listPreviewLabel;
    QFont textFont;
    QFont listFont;
    QCheckBox* fixedMOTDCheck;

    QCheckBox* doTimestamping;
    QCheckBox* showDate;
    QLabel* formatLabel;
    QComboBox* timestampFormat;
    
    QCheckBox* autoUserhostCheck;
    QCheckBox* showQuickButtons;
    QCheckBox* showModeButtons;
    QCheckBox* showTopic;
        
    QStringList colorList;
    KLineEdit* backgroundName;
    KURLRequester* backgroundURL;
    QPtrList<KColorButton> colorComboList;
    QPtrList<KColorButton> ircColorBtnList;
    QCheckBox* colorInputFieldsCheck;
    QCheckBox* parseIrcColorsCheck;
};

#endif
