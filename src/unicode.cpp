/*
 *************************************************************************
 *                                                                       *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 *************************************************************************
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 */

/* See http://lxr.mozilla.org/mozilla/source/modules/rdf/src/utils.c#540 */
/* Modified by İsmail Dönmez <ismail@kde.org> (C) 2005 for Konversation */

#define kLeft1BitMask  0x80
#define kLeft2BitsMask 0xC0
#define kLeft3BitsMask 0xE0
#define kLeft4BitsMask 0xF0
#define kLeft5BitsMask 0xF8
#define kLeft6BitsMask 0xFC
#define kLeft7BitsMask 0xFE

#define k2BytesLeadByte kLeft2BitsMask
#define k3BytesLeadByte kLeft3BitsMask
#define k4BytesLeadByte kLeft4BitsMask
#define k5BytesLeadByte kLeft5BitsMask
#define k6BytesLeadByte kLeft6BitsMask
#define kTrialByte      kLeft1BitMask

#define UTF8_1Byte(c) ( 0 == ((c) & kLeft1BitMask))
#define UTF8_2Bytes(c) ( k2BytesLeadByte == ((c) & kLeft3BitsMask))
#define UTF8_3Bytes(c) ( k3BytesLeadByte == ((c) & kLeft4BitsMask))
#define UTF8_4Bytes(c) ( k4BytesLeadByte == ((c) & kLeft5BitsMask))
#define UTF8_5Bytes(c) ( k5BytesLeadByte == ((c) & kLeft6BitsMask))
#define UTF8_6Bytes(c) ( k6BytesLeadByte == ((c) & kLeft7BitsMask))
#define UTF8_ValidTrialByte(c) ( kTrialByte == ((c) & kLeft2BitsMask))

bool isUtf8(const QCString& text)
{
    int i;
    int j;
    int clen = 0;
    int len = text.length();

    JapaneseCode* jc = new JapaneseCode();

    JapaneseCode::Type result = jc->guess_jp(text, len);

    switch(result)
    {
        case JapaneseCode::SJIS:
        case JapaneseCode::JIS:
            delete jc;
            return false;
        default:
            delete jc;
            break;
    }

    for(i=0; i < len; i += clen)
    {
        if(UTF8_1Byte(text[i]))
        {
            clen = 1;
        }
        else if(UTF8_2Bytes(text[i]))
        {
            clen = 2;

            /* No enough trail bytes */
            if( (i + clen) > len)
                return false;

            /* 0000 0000 - 0000 007F : should encode in less bytes */
            if(0 ==  (text[i] & 0x1E ))
                return false;
        }
        else if(UTF8_3Bytes(text[i]))
        {
            clen = 3;

            /* No enough trail bytes */
            if( (i + clen) > len)
                return false;

            /* a single Surrogate should not show in 3 bytes UTF8, instead, the pair should be intepreted
               as one single UCS4 char and encoded UTF8 in 4 bytes */
            if((QChar(0xED) == text[i] ) && (0xA0 == (text[i+1] & 0xA0 ) ))
                return false;

            /* 0000 0000 - 0000 07FF : should encode in less bytes */
            if((0 ==  (text[i] & 0x0F )) && (0 ==  (text[i+1] & 0x20 ) ))
                return false;
        }
        else if(UTF8_4Bytes(text[i]))
        {
            clen = 4;

            /* No enough trail bytes */
            if( (i + clen) > len)
                return false;

            /* 0000 0000 - 0000 FFFF : should encode in less bytes */
            if((0 ==  (text[i] & 0x07 )) && (0 ==  (text[i+1] & 0x30 )) )
                return false;
        }
        else if(UTF8_5Bytes(text[i]))
        {
            clen = 5;

            /* No enough trail bytes */
            if( (i + clen) > len)
                return false;

            /* 0000 0000 - 001F FFFF : should encode in less bytes */
            if((0 ==  (text[i] & 0x03 )) && (0 ==  (text[i+1] & 0x38 )) )
                return false;
        }
        else if(UTF8_6Bytes(text[i]))
        {
            clen = 6;

            /* No enough trail bytes */
            if( (i + clen) > len)
                return false;

            /* 0000 0000 - 03FF FFFF : should encode in less bytes */
            if((0 ==  (text[i] & 0x01 )) && (0 ==  (text[i+1] & 0x3E )) )
                return false;
        }
        else
        {
            return false;
        }

        for(j = 1; j<clen ;++j)
        {
            if(! UTF8_ValidTrialByte(text[i+j]))  /* Trail bytes invalid */
                return false;
        }
    }
    return true;
}
