//Copyright Paul Reiche, Fred Ford. 1992-2002

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _SNDINTRN_H
#define _SNDINTRN_H

#include <stdio.h>
#include "libs/sndlib.h"
#include "libs/reslib.h"

extern void *_GetMusicData (uio_Stream *fp, DWORD length);
extern BOOLEAN _ReleaseMusicData (void *handle);

extern void *_GetSoundBankData (uio_Stream *fp, DWORD length);
extern BOOLEAN _ReleaseSoundBankData (void *handle);

#define AllocMusicData HMalloc
#define FreeMusicData  HFree

extern char* CheckMusicResName (char* filename);

#endif /* _SNDINTRN_H */
