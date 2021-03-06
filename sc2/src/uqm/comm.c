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

#define COMM_INTERNAL
#include "comm.h"

#include "build.h"
#include "commanim.h"
#include "commglue.h"
#include "controls.h"
#include "colors.h"
#include "encount.h"
#include "endian_uqm.h"
#include "gamestr.h"
#include "options.h"
#include "load.h"
#include "oscill.h"
#include "settings.h"
#include "setup.h"
#include "sounds.h"
#include "nameref.h"
#include "uqmdebug.h"
#include "libs/graphics/gfx_common.h"
#include "libs/inplib.h"
#include "libs/sound/sound.h"
#include "libs/sound/trackplayer.h"
#include "libs/log.h"

#include <ctype.h>

#define MAX_RESPONSES 8
#define BACKGROUND_VOL \
		(speechVolumeScale == 0.0f ? NORMAL_VOLUME : (NORMAL_VOLUME >> 1))
#define FOREGROUND_VOL NORMAL_VOLUME


LOCDATA CommData;
int cur_comm;
UNICODE shared_phrase_buf[2048];

typedef struct response_entry
{
	RESPONSE_REF response_ref;
	TEXT response_text;
	RESPONSE_FUNC response_func;
} RESPONSE_ENTRY;

typedef struct encounter_state
{
	BOOLEAN (*InputFunc) (struct encounter_state *pES);
	COUNT MenuRepeatDelay;

	COUNT Initialized;
	BYTE num_responses, cur_response, top_response;
	RESPONSE_ENTRY response_list[MAX_RESPONSES];

	Task AnimTask;

	COUNT phrase_buf_index;
	UNICODE phrase_buf[512];
} ENCOUNTER_STATE;

static ENCOUNTER_STATE *pCurInputState;

// Mutex guards accesses to SubtitleText, last_subtitle and clear_subtitles.
static Mutex subtitle_mutex;
// These vars are indirectly accessed by the ambient_anim_task
static volatile BOOLEAN clear_subtitles;
static TEXT SubtitleText;
static const UNICODE * volatile last_subtitle;

static CONTEXT TextCacheContext;
static FRAME TextCacheFrame;

volatile BOOLEAN ClearSummary;

RECT CommWndRect = {
	// default values; actually inited by HailAlien()
	{SIS_ORG_X, SIS_ORG_Y},
	{0, 0}
};

static void ClearSubtitles (void);
static void CheckSubtitles (void);


/* _count_lines - sees how many lines a given input string would take to
 * display given the line wrapping information
 */
static int
_count_lines (TEXT *pText)
{
	SIZE text_width;
	const unsigned char *pStr;
	int numLines = 0;
	BOOLEAN eol;

	text_width = CommData.AlienTextWidth;
	SetContextFont (CommData.AlienFont);

	pStr = pText->pStr;
	do
	{
		++numLines;
		pText->pStr = pStr;
		eol = getLineWithinWidth (pText, &pStr, text_width, (COUNT)~0);
	} while (!eol);
	pText->pStr = pStr;

	return numLines;
}

// status == -1: draw highlighted player dialog option
// status == -2: draw non-highlighted player dialog option
// status == -4: use current context, and baseline from pTextIn
// status ==  1:  draw alien speech; subtitle cache is used
static COORD
add_text (int status, TEXT *pTextIn)
{
	COUNT maxchars, numchars;
	TEXT locText;
	TEXT *pText;
	SIZE leading;
	const unsigned char *pStr;
	SIZE text_width;
	int num_lines = 0;
	static COORD last_baseline;
	BOOLEAN eol;
	CONTEXT OldContext;
	
	BatchGraphics ();

	maxchars = (COUNT)~0;
	if (status == 1)
	{
		if (last_subtitle == pTextIn->pStr)
		{
			// draws cached subtitle
			STAMP s;

			s.origin.x = 0;
			s.origin.y = 0;
			s.frame = TextCacheFrame;
			DrawStamp (&s);
			UnbatchGraphics ();
			return last_baseline;
		}
		else
		{
			// draw to subtitle cache; prepare first
			OldContext = SetContext (TextCacheContext);
			ClearDrawable ();

			last_subtitle = pTextIn->pStr;
		}

		text_width = CommData.AlienTextWidth;
		SetContextFont (CommData.AlienFont);
		GetContextFontLeading (&leading);

		pText = pTextIn;
	}
	else if (GetContextFontLeading (&leading), status <= -4)
	{
		text_width = (SIZE) (SIS_SCREEN_WIDTH - 8 - (TEXT_X_OFFS << 2));

		pText = pTextIn;
	}
	else
	{
		text_width = (SIZE) (SIS_SCREEN_WIDTH - 8 - (TEXT_X_OFFS << 2));

		switch (status)
		{
			case -3:
				// Unknown. Never reached; color matches the background color.
				SetContextForeGroundColor (
						BUILD_COLOR (MAKE_RGB15 (0x00, 0x00, 0x14), 0x01));
				break;
			case -2:
				// Not highlighted dialog options.
				SetContextForeGroundColor (COMM_PLAYER_TEXT_NORMAL_COLOR);
				break;
			case -1:
				// Currently highlighted dialog option.
				SetContextForeGroundColor (COMM_PLAYER_TEXT_HIGHLIGHT_COLOR);
				break;
		}

		maxchars = pTextIn->CharCount;
		locText = *pTextIn;
		locText.baseline.x -= 8;
		locText.CharCount = (COUNT)~0;
		locText.pStr = STR_BULLET;
		font_DrawText (&locText);

		locText = *pTextIn;
		pText = &locText;
		pText->baseline.y -= leading;
	}

	numchars = 0;
	pStr = pText->pStr;

	if (status > 0 && (CommData.AlienTextValign &
			(VALIGN_MIDDLE | VALIGN_BOTTOM)))
	{
		num_lines = _count_lines(pText);
		if (CommData.AlienTextValign == VALIGN_BOTTOM)
			pText->baseline.y -= (leading * num_lines);
		else if (CommData.AlienTextValign == VALIGN_MIDDLE)
			pText->baseline.y -= ((leading * num_lines) / 2);
		if (pText->baseline.y < 0)
			pText->baseline.y = 0;
	}

	do
	{
		pText->pStr = pStr;
		pText->baseline.y += leading;

		eol = getLineWithinWidth (pText, &pStr, text_width, maxchars);

		maxchars -= pText->CharCount;
		if (maxchars != 0)
			--maxchars;
		numchars += pText->CharCount;
		
		if (status <= 0)
		{
			// Player dialog option or (status == -4) other non-alien
			// text.
			if (pText->baseline.y < SIS_SCREEN_HEIGHT)
				font_DrawText (pText);

			if (status < -4 && pText->baseline.y >= -status - 10)
			{
				// Never actually reached. Status is never <-4.
				++pStr;
				break;
			}
		}
		else
		{
			// Alien speech
			font_DrawTracedText (pText,
					CommData.AlienTextFColor, CommData.AlienTextBColor);
		}
	} while (!eol && maxchars);
	pText->pStr = pStr;

	if (status == 1)
	{
		STAMP s;
		
		// We were drawing to cache -- flush to screen
		SetContext (OldContext);
		s.origin.x = s.origin.y = 0;
		s.frame = TextCacheFrame;
		DrawStamp (&s);
		
		last_baseline = pText->baseline.y;
	}

	UnbatchGraphics ();
	return (pText->baseline.y);
}

// This function calculates how much of a string can be fitted within
// a specific width, up to a newline or terminating \0.
// pText is the text to be fitted. pText->CharCount will be set to the
// number of characters that fitted.
// startNext will be filled with the start of the first word that
// doesn't fit in one line, or if an entire line fits, to the character
// past the newline, or if the entire string fits, to the end of the
// string.
// maxWidth is the maximum number of pixels that a line may be wide
//   ASSUMPTION: there are no words in the text wider than maxWidth
// maxChars is the maximum number of characters (not bytes) that are to
// be fitted.
// TRUE is returned if a complete line fitted
// FALSE otherwise
BOOLEAN
getLineWithinWidth(TEXT *pText, const unsigned char **startNext,
		SIZE maxWidth, COUNT maxChars)
{
	BOOLEAN eol;
			// The end of the line of text has been reached.
	BOOLEAN done;
			// We cannot add any more words.
	RECT rect;
	COUNT oldCount;
	const unsigned char *ptr;
	const unsigned char *wordStart;
	wchar_t ch;
	COUNT charCount;

	//GetContextClipRect (&rect);

	eol = FALSE;	
	done = FALSE;
	oldCount = 1;
	charCount = 0;
	ch = '\0';
	ptr = pText->pStr;
	for (;;)
	{
		wordStart = ptr;

		// Scan one word.
		for (;;)
		{
			if (*ptr == '\0')
			{
				eol = TRUE;
				done = TRUE;
				break;
			}
			ch = getCharFromString (&ptr);
			eol = ch == '\0' || ch == '\n' || ch == '\r';
			done = eol || charCount >= maxChars;
			if (done || ch == ' ')
				break;
			charCount++;
		}

		oldCount = pText->CharCount;
		pText->CharCount = charCount;
		TextRect (pText, &rect, NULL);
		
		if (rect.extent.width >= maxWidth)
		{
			pText->CharCount = oldCount;
			*startNext = wordStart;
			return FALSE;
		}

		if (done)
		{
			*startNext = ptr;
			return eol;
		}
		charCount++;
				// For the space in between words.
	}
}

static void
DrawSISComWindow (void)
{
	CONTEXT OldContext;

	if (LOBYTE (GLOBAL (CurrentActivity)) != WON_LAST_BATTLE)
	{
		RECT r;

		OldContext = SetContext (SpaceContext);

		r.corner.x = 0;
		r.corner.y = SLIDER_Y + SLIDER_HEIGHT;
		r.extent.width = SIS_SCREEN_WIDTH;
		r.extent.height = SIS_SCREEN_HEIGHT - r.corner.y;
		SetContextForeGroundColor (COMM_PLAYER_BACKGROUND_COLOR);
		DrawFilledRectangle (&r);

		SetContext (OldContext);
	}
}

void
DrawAlienFrame (FRAME aframe, SEQUENCE *pSeq)
{
	COUNT i;
	STAMP s;
	ANIMATION_DESC *ADPtr;

	s.origin.x = -SAFE_X;
	s.origin.y = 0;
	s.frame = CommData.AlienFrame;
	if (s.frame == 0)
		s.frame = aframe;
	
	BatchGraphics ();
	DrawStamp (&s);
	i = CommData.NumAnimations;
	ADPtr = &CommData.AlienAmbientArray[i];
	while (i--)
	{
		--ADPtr;

		if (!(ADPtr->AnimFlags & ANIM_MASK))
		{
			s.frame = SetAbsFrameIndex (s.frame, ADPtr->StartIndex);
			DrawStamp (&s);
			ADPtr->AnimFlags |= ANIM_DISABLED;
		}
		else if (pSeq)
		{
			if (pSeq->AnimType == PICTURE_ANIM)
			{
				s.frame = pSeq->AnimObj.CurFrame;
				DrawStamp (&s);
			}
			--pSeq;
		}
	}
	if (aframe && CommData.AlienFrame && aframe != CommData.AlienFrame)
	{
		s.frame = aframe;
		DrawStamp (&s);
	}
	UnbatchGraphics ();
}

void
init_communication (void)
{
	subtitle_mutex = CreateMutex ("Subtitle Lock",
			SYNC_CLASS_TOPLEVEL | SYNC_CLASS_VIDEO);
}

void
uninit_communication (void)
{
	DestroyMutex (subtitle_mutex);
}

static void
RefreshResponses (ENCOUNTER_STATE *pES)
{
	COORD y;
	BYTE response;
	SIZE leading;
	STAMP s;

	SetContext (SpaceContext);
	GetContextFontLeading (&leading);
	BatchGraphics ();

	DrawSISComWindow ();
	y = SLIDER_Y + SLIDER_HEIGHT + 1;
	for (response = pES->top_response; response < pES->num_responses;
			++response)
	{
		pES->response_list[response].response_text.baseline.x = TEXT_X_OFFS + 8;
		pES->response_list[response].response_text.baseline.y = y + leading;
		pES->response_list[response].response_text.align = ALIGN_LEFT;
		if (response == pES->cur_response)
			y = add_text (-1, &pES->response_list[response].response_text);
		else
			y = add_text (-2, &pES->response_list[response].response_text);
	}

	if (pES->top_response)
	{
		s.origin.y = SLIDER_Y + SLIDER_HEIGHT + 1;
		s.frame = SetAbsFrameIndex (ActivityFrame, 6);
	}
	else if (y > SIS_SCREEN_HEIGHT)
	{
		s.origin.y = SIS_SCREEN_HEIGHT - 2;
		s.frame = SetAbsFrameIndex (ActivityFrame, 7);
	}
	else
		s.frame = 0;
	if (s.frame)
	{
		RECT r;

		GetFrameRect (s.frame, &r);
		s.origin.x = SIS_SCREEN_WIDTH - r.extent.width - 1;
		DrawStamp (&s);
	}

	UnbatchGraphics ();
}

static void
FeedbackPlayerPhrase (UNICODE *pStr)
{
	SetContext (SpaceContext);
	
	BatchGraphics ();
	DrawSISComWindow ();
	if (pStr[0])
	{
		TEXT ct;

		ct.baseline.x = SIS_SCREEN_WIDTH >> 1;
		ct.baseline.y = SLIDER_Y + SLIDER_HEIGHT + 13;
		ct.align = ALIGN_CENTER;
		ct.CharCount = (COUNT)~0;

		ct.pStr = GAME_STRING (FEEDBACK_STRING_BASE);
				// "(In response to your statement)"
		SetContextForeGroundColor (COMM_RESPONSE_INTRO_TEXT_COLOR);
		font_DrawText (&ct);

		ct.baseline.y += 16;
		SetContextForeGroundColor (COMM_FEEDBACK_TEXT_COLOR);
		ct.pStr = pStr;
		add_text (-4, &ct);
	}
	UnbatchGraphics ();
}

void
UpdateSpeechGraphics (BOOLEAN Initialize)
{
	CONTEXT OldContext;

	if (Initialize)
	{
		RECT r, sr;
		FRAME f;

		InitOscilloscope (0, 0, RADAR_WIDTH, RADAR_HEIGHT,
				SetAbsFrameIndex (ActivityFrame, 9));
		f = SetAbsFrameIndex (ActivityFrame, 2);
		GetFrameRect (f, &r);
		SetSliderImage (f);
		f = SetAbsFrameIndex (ActivityFrame, 5);
		GetFrameRect (f, &sr);
		InitSlider (0, SLIDER_Y, SIS_SCREEN_WIDTH, sr.extent.height,
				r.extent.width, r.extent.height, f);
	}

	OldContext = SetContext (RadarContext);
	Oscilloscope (!Initialize);
	SetContext (SpaceContext);
	Slider ();
	SetContext (OldContext);
}

static BOOLEAN
SpewPhrases (COUNT wait_track)
{
	BOOLEAN ContinuityBreak;
	DWORD TimeIn;
	COUNT which_track;
	FRAME F;
	BOOLEAN rewind = FALSE;

	TimeIn = GetTimeCounter ();

	ContinuityBreak = FALSE;
	F = CommData.AlienFrame;
	if (wait_track == 0)
	{	// Restarting with a rewind
		wait_track = (COUNT)~0;
		which_track = (COUNT)~0;
		rewind = TRUE;
	}

	which_track = PlayingTrack ();
	if (which_track == 0 && !rewind)
	{	// initial start of player
		UnlockMutex (GraphicsLock);
		PlayTrack ();
		// wait for the trackplayer to start playing
		do
		{
			TaskSwitch ();
			which_track = PlayingTrack ();
		} while (!which_track);
		LockMutex (GraphicsLock);
	}
	else if (which_track <= wait_track)
	{	// XXX: I don't know why this is here, but it is not harmful.
		//   We never actually pause in comm.
		ResumeTrack ();
	}

	do
	{
		BOOLEAN left = FALSE;
		BOOLEAN right = FALSE;

		if (GLOBAL (CurrentActivity) & CHECK_ABORT)
		{
			which_track = 0; // abort
			break;
		}
		
		UnlockMutex (GraphicsLock);
		// XXX: Executing this loop 64 times a second is a bit extreme
		SleepThreadUntil (TimeIn + (ONE_SECOND / 64));
		TimeIn = GetTimeCounter ();
#if DEMO_MODE || CREATE_JOURNAL
		InputState = 0;
#else /* !(DEMO_MODE || CREATE_JOURNAL) */
		UpdateInputState ();
#endif

		LockMutex (GraphicsLock);
		if (PulsedInputState.menu[KEY_MENU_CANCEL])
		{
			JumpTrack ();
			which_track = 0; // player stopped
			break;
		}

		CheckSubtitles ();

		if (optSmoothScroll == OPT_PC)
		{
			left = PulsedInputState.menu[KEY_MENU_LEFT];
			right = PulsedInputState.menu[KEY_MENU_RIGHT];
		}
		else if (optSmoothScroll == OPT_3DO)
		{
			left = ImmediateInputState.menu[KEY_MENU_LEFT];
			right = ImmediateInputState.menu[KEY_MENU_RIGHT];
		}
		
		if (right)
		{
			SetSliderImage (SetAbsFrameIndex (ActivityFrame, 3));
			if (optSmoothScroll == OPT_PC)
				FastForward_Page ();
			else if (optSmoothScroll == OPT_3DO)
				FastForward_Smooth ();
			ContinuityBreak = TRUE;
			// XXX: Ugly hack: This causes all animations (talking and ambient)
			// in ambient_anim_task to stop progressing. I see no reason why
			// the animations cannot continue while seeking. This hack has
			// spawned a multitude of workarounds in the comm code, and IMHO
			// should be removed.
			CommData.AlienFrame = 0;
		}
		else if (left || rewind)
		{
			rewind = FALSE;
			SetSliderImage (SetAbsFrameIndex (ActivityFrame, 4));
			if (optSmoothScroll == OPT_PC)
				FastReverse_Page ();
			else if (optSmoothScroll == OPT_3DO)
				FastReverse_Smooth ();
			ContinuityBreak = TRUE;
			// XXX: See ugly hack discussion above
			CommData.AlienFrame = 0;
		}
		else if (ContinuityBreak)
		{
			// This is only done once the seeking is over (in the smooth
			// scroll case, once the user releases the seek button)
			ContinuityBreak = FALSE;
			SetSliderImage (SetAbsFrameIndex (ActivityFrame, 2));
		}
		else
		{	// XXX: See ugly hack discussion above
			// Additionally, this used to have a buggy guard condition, which
			// would cause the animations to remain paused in a couple cases
			// after seeking back to the beginning.
			// Broken cases were: Syreen "several hours later" and Starbase
			// VUX Beast analysis by the scientist.
			CommData.AlienFrame = F;
		}
		
		which_track = PlayingTrack ();

	} while (ContinuityBreak || (which_track && which_track <= wait_track));

	CommData.AlienFrame = F;
	ClearSubtitles ();

	if (!which_track || wait_track == (COUNT)~0)
	{	// reached the end
		SetSliderImage (SetAbsFrameIndex (ActivityFrame, 8));
		return (FALSE);
	}
	
	// We can only get here when we got to the requested track
	// without ending or aborting
	return TRUE;
}

static BOOLEAN
DoTalkSegue (COUNT wait_track)
{
	BOOLEAN done;

	// Transition animation to talking state, if necessary
	if (CommData.AlienTalkDesc.NumFrames)
	{
		if (!(CommData.AlienTransitionDesc.AnimFlags & TALK_INTRO))
		{
			CommData.AlienTransitionDesc.AnimFlags |= TALK_INTRO;
			if (CommData.AlienTransitionDesc.NumFrames)
				CommData.AlienTalkDesc.AnimFlags |= TALK_INTRO;
		}
					
		CommData.AlienTransitionDesc.AnimFlags &= ~PAUSE_TALKING;
		if (CommData.AlienTalkDesc.NumFrames)
			CommData.AlienTalkDesc.AnimFlags |= WAIT_TALKING;
		while (CommData.AlienTalkDesc.AnimFlags & TALK_INTRO)
		{	// wait until the transition finishes
			UnlockMutex (GraphicsLock);
			TaskSwitch ();
			LockMutex (GraphicsLock);
		}
	}

	done = !SpewPhrases (wait_track);

	// transition back to silent, if necessary
	if (CommData.AlienTalkDesc.NumFrames)
	{
		// must set the TALK_DONE flag so that the animation task
		// releases WAIT_TALKING from AlienTalkDesc
		CommData.AlienTransitionDesc.AnimFlags |= TALK_DONE;
		if ((CommData.AlienTalkDesc.AnimFlags & WAIT_TALKING))
			CommData.AlienTalkDesc.AnimFlags |= PAUSE_TALKING;
	}

	return done;
}

static void
FlushTalkSegue (void)
{
	FlushInput ();
	while (AnyButtonPress (TRUE))
		TaskSwitch ();

	do
		TaskSwitch ();
	while (CommData.AlienTalkDesc.AnimFlags & PAUSE_TALKING);
}

void
AlienTalkSegue (COUNT wait_track)
{
	BOOLEAN done;

	// this skips any talk segues that follow an aborted one
	if ((GLOBAL (CurrentActivity) & CHECK_ABORT)
			|| (CommData.AlienTransitionDesc.AnimFlags & TALK_DONE))
		return;

	LockMutex (GraphicsLock);

	if (!pCurInputState->Initialized)
	{
		SetColorMap (GetColorMapAddress (CommData.AlienColorMap));
		DrawAlienFrame (CommData.AlienFrame, NULL);
		UpdateSpeechGraphics (TRUE);

		if (LOBYTE (GLOBAL (CurrentActivity)) == WON_LAST_BATTLE
				|| (!GET_GAME_STATE (PLAYER_HYPNOTIZED)
				&& !GET_GAME_STATE (CHMMR_EMERGING)
				&& GET_GAME_STATE (CHMMR_BOMB_STATE) != 2
				&& (pMenuState == 0 || !GET_GAME_STATE (MOONBASE_ON_SHIP)
				|| GET_GAME_STATE (PROBE_ILWRATH_ENCOUNTER))))
		{
			RECT r;
	
			if (pMenuState == 0 &&
					LOBYTE (GLOBAL (CurrentActivity)) != WON_LAST_BATTLE)
			{
				r.corner.x = SIS_ORG_X;
				r.corner.y = SIS_ORG_Y;
				r.extent.width = SIS_SCREEN_WIDTH;
				r.extent.height = SIS_SCREEN_HEIGHT;
				ScreenTransition (3, &r);
			}
			else
			{
				ScreenTransition (3, &CommWndRect);
			}
			UnbatchGraphics ();
		}
		else
		{
			BYTE clut_buf[] = {FadeAllToColor};
			
			UnbatchGraphics ();
			if (GET_GAME_STATE (MOONBASE_ON_SHIP)
					|| GET_GAME_STATE (CHMMR_BOMB_STATE) == 2)
				XFormColorMap ((COLORMAPPTR)clut_buf, ONE_SECOND * 2);
			else if (GET_GAME_STATE (CHMMR_EMERGING))
				XFormColorMap ((COLORMAPPTR)clut_buf, ONE_SECOND * 2);
			else
				XFormColorMap ((COLORMAPPTR)clut_buf, ONE_SECOND * 5);
		}
		pCurInputState->Initialized = TRUE;

		PlayMusic (CommData.AlienSong, TRUE, 1);
		SetMusicVolume (BACKGROUND_VOL);

		{
			DWORD TimeOut;

			TimeOut = GetTimeCounter () + (ONE_SECOND >> 1);
/* if (CommData.NumAnimations) */
				pCurInputState->AnimTask = StartCommAnimTask ();

			UnlockMutex (GraphicsLock);
			SleepThreadUntil (TimeOut);
			LockMutex (GraphicsLock);
		}

		LastActivity &= ~CHECK_LOAD;
	}
	
	done = DoTalkSegue (wait_track);
	if (done || wait_track == (COUNT)~0)
		FadeMusic (FOREGROUND_VOL, ONE_SECOND);

	UnlockMutex (GraphicsLock);
	FlushTalkSegue ();

	if (done || wait_track == (COUNT)~0)
	{	// all done talking here
		CommData.AlienTransitionDesc.AnimFlags |= TALK_DONE;
	}
	else
	{	// there is more to come
		CommData.AlienTransitionDesc.AnimFlags &= ~TALK_DONE;
		// allow a transition to talking state again later
		CommData.AlienTransitionDesc.AnimFlags &= ~TALK_INTRO;
	}
}


typedef struct summary_state
{
	// standard state required by DoInput
	BOOLEAN (*InputFunc) (struct summary_state *pSS);
	COUNT MenuRepeatDelay;

	// extended state
	BOOLEAN Initialized;
	BOOLEAN PrintNext;
	SUBTITLE_REF NextSub;
	const UNICODE *LeftOver;

} SUMMARY_STATE;

static BOOLEAN
DoConvSummary (SUMMARY_STATE *pSS)
{
#define DELTA_Y_SUMMARY 8
#define MAX_SUMM_ROWS ((SIS_SCREEN_HEIGHT - SLIDER_Y - SLIDER_HEIGHT) \
			/ DELTA_Y_SUMMARY) - 1

	if (!pSS->Initialized)
	{
		pSS->PrintNext = TRUE;
		pSS->NextSub = GetFirstTrackSubtitle ();
		pSS->LeftOver = NULL;
		pSS->MenuRepeatDelay = 0;
		pSS->InputFunc = DoConvSummary;
		pSS->Initialized = TRUE;
		DoInput (pSS, FALSE);
	}
	else if (GLOBAL (CurrentActivity) & CHECK_ABORT)
	{
		return FALSE; // bail out
	}
	else if (PulsedInputState.menu[KEY_MENU_SELECT]
			|| PulsedInputState.menu[KEY_MENU_CANCEL]
			|| PulsedInputState.menu[KEY_MENU_RIGHT])
	{
		if (pSS->NextSub)
		{	// we want the next page
			pSS->PrintNext = TRUE;
		}
		else
		{	// no more, we are done
			return FALSE;
		}
	}
	else if (pSS->PrintNext)
	{	// print the next page
		RECT r;
		TEXT t;
		int row;
		FONT oldFont;

		r.corner.x = 0;
		r.corner.y = 0;
		r.extent.width = SIS_SCREEN_WIDTH;
		r.extent.height = SIS_SCREEN_HEIGHT - SLIDER_Y - SLIDER_HEIGHT + 2;

		LockMutex (GraphicsLock);
		SetContextForeGroundColor (COMM_HISTORY_BACKGROUND_COLOR);
		DrawFilledRectangle (&r);

		SetContextForeGroundColor (COMM_HISTORY_TEXT_COLOR);

		r.extent.width -= 2 + 2;
		t.baseline.x = 2;
		t.align = ALIGN_LEFT;
		t.baseline.y = DELTA_Y_SUMMARY;
		oldFont = SetContextFont (TinyFont);

		for (row = 0; row < MAX_SUMM_ROWS && pSS->NextSub;
				++row, pSS->NextSub = GetNextTrackSubtitle (pSS->NextSub))
		{
			const unsigned char *next;

			if (pSS->LeftOver)
			{	// some text left from last subtitle
				t.pStr = pSS->LeftOver;
				pSS->LeftOver = NULL;
			}
			else
			{
				t.pStr = GetTrackSubtitleText (pSS->NextSub);
				if (!t.pStr)
					continue;
			}

			t.CharCount = (COUNT)~0;
			for ( ; row < MAX_SUMM_ROWS &&
					!getLineWithinWidth (&t, &next, r.extent.width, (COUNT)~0);
					++row)
			{
				font_DrawText (&t);
				t.baseline.y += DELTA_Y_SUMMARY;
				t.pStr = next;
				t.CharCount = (COUNT)~0;
			}

			if (row >= MAX_SUMM_ROWS)
			{	// no more space on screen, but some text left over
				// from the current subtitle
				pSS->LeftOver = next;
				break;
			}
		
			// this subtitle fit completely
			font_DrawText (&t);
			t.baseline.y += DELTA_Y_SUMMARY;
		}

		if (row >= MAX_SUMM_ROWS && (pSS->NextSub || pSS->LeftOver))
		{	// draw *MORE*
			TEXT mt;
			UNICODE buffer[80];

			mt.baseline.x = SIS_SCREEN_WIDTH >> 1;
			mt.baseline.y = t.baseline.y;
			mt.align = ALIGN_CENTER;
			snprintf (buffer, sizeof (buffer), "%s%s%s", // "MORE"
					STR_MIDDLE_DOT, GAME_STRING (FEEDBACK_STRING_BASE + 1),
					STR_MIDDLE_DOT);
			mt.pStr = buffer;
			SetContextForeGroundColor (COMM_MORE_TEXT_COLOR);
			font_DrawText (&mt);
		}

		SetContextFont (oldFont);
		UnlockMutex (GraphicsLock);

		pSS->PrintNext = FALSE;
	}
	else
	{
		SleepThread (ONE_SECOND / 20);
	}

	return TRUE; // keep going
}

// Called when the player presses the select button on a response.
static void
SelectResponse (ENCOUNTER_STATE *pES)
{
	const unsigned char *end;
	TEXT *response_text =
			&pES->response_list[pES->cur_response].response_text;
	end = skipUTF8Chars(response_text->pStr, response_text->CharCount);
	pES->phrase_buf_index = end - response_text->pStr;
	memcpy(pES->phrase_buf, response_text->pStr, pES->phrase_buf_index);
	pES->phrase_buf[pES->phrase_buf_index++] = '\0';

	LockMutex (GraphicsLock);
	FeedbackPlayerPhrase (pES->phrase_buf);
	StopTrack ();
	ClearSubtitles ();
	SetSliderImage (SetAbsFrameIndex (ActivityFrame, 2));
	UnlockMutex (GraphicsLock);

	FadeMusic (BACKGROUND_VOL, ONE_SECOND);

	CommData.AlienTransitionDesc.AnimFlags &= ~(TALK_INTRO | TALK_DONE);
	pES->num_responses = 0;
	(*pES->response_list[pES->cur_response].response_func)
			(pES->response_list[pES->cur_response].response_ref);
}

// Called when the player presses the cancel button in comm.
static void
SelectConversationSummary (ENCOUNTER_STATE *pES)
{
	SUMMARY_STATE SummaryState;
	
	LockMutex (GraphicsLock);
	FeedbackPlayerPhrase (pES->phrase_buf);
	PauseAnimTask = TRUE;
	UnlockMutex (GraphicsLock);
	// wait for ambient anim task to pause
	SleepThread (ONE_SECOND / 30);

	SummaryState.Initialized = FALSE;
	DoConvSummary (&SummaryState);

	LockMutex (GraphicsLock);
	RefreshResponses (pES);
	ClearSummary = TRUE;
	PauseAnimTask = FALSE;
	UnlockMutex (GraphicsLock);
}

static void
PlayerResponseInput (ENCOUNTER_STATE *pES)
{
	BYTE response;
	DWORD TimeIn = GetTimeCounter ();

	if (pES->top_response == (BYTE)~0)
	{
		pES->top_response = 0;
		LockMutex (GraphicsLock);
		RefreshResponses (pES);
		UnlockMutex (GraphicsLock);
	}

	if (PulsedInputState.menu[KEY_MENU_SELECT])
	{
		SelectResponse (pES);
	}
	else if (PulsedInputState.menu[KEY_MENU_CANCEL] &&
			LOBYTE (GLOBAL (CurrentActivity)) != WON_LAST_BATTLE)
	{
		SelectConversationSummary (pES);
	}
	else
	{
		response = pES->cur_response;
		if (PulsedInputState.menu[KEY_MENU_LEFT])
		{
			FadeMusic (BACKGROUND_VOL, ONE_SECOND);
			LockMutex (GraphicsLock);
			FeedbackPlayerPhrase (pES->phrase_buf);
			// reset transition state
			CommData.AlienTransitionDesc.AnimFlags &=
					~(TALK_INTRO | TALK_DONE);
			DoTalkSegue (0);

			if (!(GLOBAL (CurrentActivity) & CHECK_ABORT))
			{
				RefreshResponses (pES);
				FadeMusic (FOREGROUND_VOL, ONE_SECOND);
			}
			
			UnlockMutex (GraphicsLock);
			FlushTalkSegue ();
			// done one way or the other
			CommData.AlienTransitionDesc.AnimFlags |= TALK_DONE;

		}
		else if (PulsedInputState.menu[KEY_MENU_UP])
			response = (BYTE)((response + (BYTE)(pES->num_responses - 1))
					% pES->num_responses);
		else if (PulsedInputState.menu[KEY_MENU_DOWN])
			response = (BYTE)((BYTE)(response + 1) % pES->num_responses);

		if (response != pES->cur_response)
		{
			COORD y;

			LockMutex (GraphicsLock);
			BatchGraphics ();
			add_text (-2,
					&pES->response_list[pES->cur_response].response_text);

			pES->cur_response = response;

			y = add_text (-1,
					&pES->response_list[pES->cur_response].response_text);
			if (response < pES->top_response)
			{
				pES->top_response = 0;
				RefreshResponses (pES);
			}
			else if (y > SIS_SCREEN_HEIGHT)
			{
				pES->top_response = response;
				RefreshResponses (pES);
			}
			UnbatchGraphics ();
			UnlockMutex (GraphicsLock);
		}

		SleepThreadUntil (TimeIn + ONE_SECOND / 20);
	}
}

static BOOLEAN
DoCommunication (ENCOUNTER_STATE *pES)
{
	SetMenuSounds (MENU_SOUND_UP | MENU_SOUND_DOWN, MENU_SOUND_SELECT);

	if (!(CommData.AlienTransitionDesc.AnimFlags & TALK_DONE))
		AlienTalkSegue ((COUNT)~0);

	if (GLOBAL (CurrentActivity) & CHECK_ABORT)
		;
	else if (pES->num_responses == 0)
	{
		// The player doesn't get a chance to say anything.
		DWORD TimeIn, TimeOut;

		TimeOut = FadeMusic (0, ONE_SECOND * 3) + ONE_SECOND / 60;
		TimeIn = GetTimeCounter ();
		do
		{
			SleepThreadUntil (TimeIn + ONE_SECOND / 120);
			TimeIn = GetTimeCounter ();
			// Warning!  This used to re-gather input data to check for rewind.
			UpdateInputState ();
			if (PulsedInputState.menu[KEY_MENU_LEFT])
			{
				FadeMusic (BACKGROUND_VOL, ONE_SECOND);
				LockMutex (GraphicsLock);
				// reset transition state
				CommData.AlienTransitionDesc.AnimFlags &=
						~(TALK_INTRO | TALK_DONE);
				DoTalkSegue (0);
				UnlockMutex (GraphicsLock);
				FlushTalkSegue ();
				// done one way or the other
				CommData.AlienTransitionDesc.AnimFlags |= TALK_DONE;

				if (GLOBAL (CurrentActivity) & CHECK_ABORT)
					break;
				TimeOut = FadeMusic (0, ONE_SECOND * 2) + ONE_SECOND / 60;
				TimeIn = GetTimeCounter ();
			}
		} while (TimeIn <= TimeOut);
	}
	else
	{
		PlayerResponseInput (pES);
		return (TRUE);
	}

	LockMutex (GraphicsLock);

	if (pES->AnimTask)
	{
		UnlockMutex (GraphicsLock);
		ConcludeTask (pES->AnimTask);
		LockMutex (GraphicsLock);
		pES->AnimTask = 0;
	}
	CommData.AlienTransitionDesc.AnimFlags &= ~(TALK_INTRO | TALK_DONE);

	SetContext (SpaceContext);
	DestroyContext (TaskContext);
	TaskContext = 0;

	UnlockMutex (GraphicsLock);

	FlushColorXForms ();
	ClearSubtitles ();

	StopMusic ();
	StopSound ();
	StopTrack ();
	SleepThreadUntil (FadeMusic (NORMAL_VOLUME, 0) + ONE_SECOND / 60);

	return (FALSE);
}

void
DoResponsePhrase (RESPONSE_REF R, RESPONSE_FUNC response_func,
		UNICODE *ConstructStr)
{
	ENCOUNTER_STATE *pES = pCurInputState;
	RESPONSE_ENTRY *pEntry;

	if (GLOBAL (CurrentActivity) & CHECK_ABORT)
		return;
			
	if (pES->num_responses == 0)
	{
		pES->cur_response = 0;
		pES->top_response = (BYTE)~0;
	}

	pEntry = &pES->response_list[pES->num_responses];
	pEntry->response_ref = R;
	pEntry->response_text.pStr = ConstructStr;
	if (pEntry->response_text.pStr)
		pEntry->response_text.CharCount = (COUNT)~0;
	else
	{
		STRING locString;
		
		locString = SetAbsStringTableIndex (CommData.ConversationPhrases,
				(COUNT) (R - 1));
		pEntry->response_text.pStr =
				(UNICODE *) GetStringAddress (locString);
		pEntry->response_text.CharCount = GetStringLength (locString);
//#define BVT_PROBLEM
#ifdef BVT_PROBLEM
		if (pEntry->response_text.pStr[pEntry->response_text.CharCount - 1]
				== '\0')
			--pEntry->response_text.CharCount;
#endif /* BVT_PROBLEM */
	}
	pEntry->response_func = response_func;
	++pES->num_responses;
}

static void
HailAlien (void)
{
	ENCOUNTER_STATE ES;
	FONT PlayerFont, OldFont;
	MUSIC_REF SongRef = 0;
	COLOR TextBack;

	pCurInputState = &ES;
	memset (pCurInputState, 0, sizeof (*pCurInputState));

	ES.InputFunc = DoCommunication;
	PlayerFont = LoadFont (PLAYER_FONT);

	CommData.AlienFrame = CaptureDrawable (
			LoadGraphic (CommData.AlienFrameRes));
	CommData.AlienFont = LoadFont (CommData.AlienFontRes);
	CommData.AlienColorMap = CaptureColorMap (
			LoadColorMap (CommData.AlienColorMapRes));
	if ((CommData.AlienSongFlags & LDASF_USE_ALTERNATE)
			&& CommData.AlienAltSongRes)
		SongRef = LoadMusic (CommData.AlienAltSongRes);
	if (SongRef)
		CommData.AlienSong = SongRef;
	else
		CommData.AlienSong = LoadMusic (CommData.AlienSongRes);

	CommData.ConversationPhrases = CaptureStringTable (
			LoadStringTable (CommData.ConversationPhrasesRes));

	SubtitleText.baseline = CommData.AlienTextBaseline;
	SubtitleText.align = CommData.AlienTextAlign;

	// init subtitle cache context
	TextCacheContext = CreateContext ();
	TextCacheFrame = CaptureDrawable (
			CreateDrawable (WANT_PIXMAP, SIS_SCREEN_WIDTH,
			SIS_SCREEN_HEIGHT - SLIDER_Y - SLIDER_HEIGHT + 2, 1));
	SetContext (TextCacheContext);
	SetContextFGFrame (TextCacheFrame);
	TextBack = BUILD_COLOR (MAKE_RGB15 (0x00, 0x00, 0x10), 0x00);
			// Color key for the background.
	SetContextBackGroundColor (TextBack);
	ClearDrawable ();
	SetFrameTransparentColor (TextCacheFrame, TextBack);

	ES.phrase_buf_index = 1;
	ES.phrase_buf[0] = '\0';

	SetContext (SpaceContext);
	OldFont = SetContextFont (PlayerFont);

	{
		RECT r;

		TaskContext = CreateContext ();
		SetContext (TaskContext);
		SetContextFGFrame (Screen);
		GetFrameRect (CommData.AlienFrame, &r);
		r.extent.width = SIS_SCREEN_WIDTH;
		CommWndRect.extent = r.extent;
		
		SetTransitionSource (NULL);
		BatchGraphics ();
		if (LOBYTE (GLOBAL (CurrentActivity)) == WON_LAST_BATTLE)
		{
			r.corner = CommWndRect.corner;
			SetContextClipRect (&r);
		}
		else
		{
			r.corner.x = SIS_ORG_X;
			r.corner.y = SIS_ORG_Y;
			SetContextClipRect (&r);
			CommWndRect.corner = r.corner;

			if (pMenuState == 0)
			{
				RepairSISBorder ();
				UnlockMutex (GraphicsLock);
				DrawMenuStateStrings ((BYTE)~0, 1);
				LockMutex (GraphicsLock);
			}
			else /* in starbase */
			{
				DrawSISFrame ();
				if (GET_GAME_STATE (STARBASE_AVAILABLE))
				{
					DrawSISMessage (GAME_STRING (STARBASE_STRING_BASE + 1));
							// "Starbase Commander"
					DrawSISTitle (GAME_STRING (STARBASE_STRING_BASE + 0));
							// "Starbase"
				}
				else
				{
					DrawSISMessage (NULL);
					DrawSISTitle (GLOBAL_SIS (PlanetName));
				}
			}
		}

		DrawSISComWindow ();
	}

	UnlockMutex (GraphicsLock);

	LastActivity |= CHECK_LOAD; /* prevent spurious input */
	(*CommData.init_encounter_func) ();
	DoInput (&ES, FALSE);
	if (!(GLOBAL (CurrentActivity) & (CHECK_ABORT | CHECK_LOAD)))
		(*CommData.post_encounter_func) ();
	(*CommData.uninit_encounter_func) ();

	LockMutex (GraphicsLock);

	DestroyStringTable (ReleaseStringTable (CommData.ConversationPhrases));
	DestroyMusic (CommData.AlienSong);
	DestroyColorMap (ReleaseColorMap (CommData.AlienColorMap));
	DestroyFont (CommData.AlienFont);
	DestroyDrawable (ReleaseDrawable (CommData.AlienFrame));

	DestroyContext (TextCacheContext);
	DestroyDrawable (ReleaseDrawable (TextCacheFrame));

	SetContext (SpaceContext);
	SetContextFont (OldFont);
	DestroyFont (PlayerFont);

	// Some support code tests either of these to see if the
	// game is currently in comm or encounter
	CommData.ConversationPhrasesRes = 0;
	CommData.ConversationPhrases = 0;
	pCurInputState = 0;
}

COUNT
InitCommunication (CONVERSATION which_comm)
{
	COUNT status;
	LOCDATA *LocDataPtr;

#ifdef DEBUG
	if (disableInteractivity)
		return 0;
#endif
	
	LockMutex (GraphicsLock);

	if (LastActivity & CHECK_LOAD)
	{
		LastActivity &= ~CHECK_LOAD;
		if (which_comm != COMMANDER_CONVERSATION)
		{
			if (LOBYTE (LastActivity) == 0)
			{
				DrawSISFrame ();
			}
			else
			{
				ClearSISRect (DRAW_SIS_DISPLAY);
				RepairSISBorder ();
			}
			DrawSISMessage (NULL);
			if (LOBYTE (GLOBAL (CurrentActivity)) == IN_HYPERSPACE)
				DrawHyperCoords (GLOBAL (ShipStamp.origin));
			else if (GLOBAL (ip_planet) == 0)
				DrawHyperCoords (CurStarDescPtr->star_pt);
			else
				DrawSISTitle (GLOBAL_SIS (PlanetName));
		}
	}

	if (which_comm == URQUAN_DRONE_CONVERSATION)
	{
		status = URQUAN_DRONE_SHIP;
		which_comm = URQUAN_CONVERSATION;
	}
	else
	{
		if (which_comm == YEHAT_REBEL_CONVERSATION)
		{
			status = YEHAT_REBEL_SHIP;
			which_comm = YEHAT_CONVERSATION;
		}
		else
		{
			COUNT commToShip[] = {
				RACE_SHIP_FOR_COMM
			};
			status = commToShip[which_comm];
			if (status >= YEHAT_REBEL_SHIP) {
				/* conversation exception, set to self */
				status = HUMAN_SHIP;
			}
		}
		ActivateStarShip (status, SPHERE_TRACKING);

		if (which_comm == ORZ_CONVERSATION
				|| (which_comm == TALKING_PET_CONVERSATION
				&& (!GET_GAME_STATE (TALKING_PET_ON_SHIP)
				|| LOBYTE (GLOBAL (CurrentActivity)) == IN_LAST_BATTLE))
				|| (which_comm != CHMMR_CONVERSATION
				&& which_comm != SYREEN_CONVERSATION
				))//&& ActivateStarShip (status, CHECK_ALLIANCE) == BAD_GUY))
			BuildBattle (NPC_PLAYER_NUM);
	}

	LocDataPtr = init_race (
			status != YEHAT_REBEL_SHIP ? which_comm :
			YEHAT_REBEL_CONVERSATION);
	if (LocDataPtr)
		CommData = *LocDataPtr;

	UnlockMutex (GraphicsLock);

	if (GET_GAME_STATE (BATTLE_SEGUE) == 0)
	{
		// Not offered the chance to attack.
		status = HAIL;
	}
	else if ((status = InitEncounter ()) == HAIL && LocDataPtr)
	{
		// The player chose to talk.
		SET_GAME_STATE (BATTLE_SEGUE, 0);
	}
	else
	{
		// The player chose to attack.
		status = ATTACK;
		SET_GAME_STATE (BATTLE_SEGUE, 1);
	}

	LockMutex (GraphicsLock);

	if (status == HAIL)
	{
		cur_comm = which_comm;
		HailAlien ();
		cur_comm = 0;
	}
	else if (LocDataPtr)
	{	// only when comm initied successfully
		if (!(GLOBAL (CurrentActivity) & (CHECK_ABORT | CHECK_LOAD)))
			(*CommData.post_encounter_func) (); // process states

		(*CommData.uninit_encounter_func) (); // cleanup
	}

	UnlockMutex (GraphicsLock);

	status = 0;
	if (!(GLOBAL (CurrentActivity) & (CHECK_ABORT | CHECK_LOAD)))
	{
		if (LOBYTE (GLOBAL (CurrentActivity)) == IN_LAST_BATTLE
				&& (GLOBAL (glob_flags) & CYBORG_ENABLED))
			ReinitQueue (&GLOBAL (npc_built_ship_q));

		SET_GAME_STATE (GLOBAL_FLAGS_AND_DATA, 0);
		status = (GET_GAME_STATE (BATTLE_SEGUE)
				&& GetHeadLink (&GLOBAL (npc_built_ship_q)));
		if (status)
		{
			// Start combat
			BuildBattle (RPG_PLAYER_NUM);
			EncounterBattle ();
		}
		else
		{
			SET_GAME_STATE (BATTLE_SEGUE, 0);
		}
	}

	UninitEncounter ();

	return (status);
}

void
RaceCommunication (void)
{
	COUNT i, status;
	HSHIPFRAG hStarShip;
	SHIP_FRAGMENT *FragPtr;
	HENCOUNTER hEncounter = 0;
	CONVERSATION RaceComm[] =
	{
		RACE_COMMUNICATION
	};

	if (LOBYTE (GLOBAL (CurrentActivity)) == IN_LAST_BATTLE)
	{
		/* Going into talking pet conversation */
		ReinitQueue (&GLOBAL (npc_built_ship_q));
		CloneShipFragment (SAMATRA_SHIP, &GLOBAL (npc_built_ship_q), 0);
		InitCommunication (TALKING_PET_CONVERSATION);
		if (!(GLOBAL (CurrentActivity) & (CHECK_ABORT | CHECK_LOAD))
				&& GLOBAL_SIS (CrewEnlisted) != (COUNT)~0)
		{
			GLOBAL (CurrentActivity) = WON_LAST_BATTLE;
		}
		return;
	}
	else if (NextActivity & CHECK_LOAD)
	{
		BYTE ec;

		ec = GET_GAME_STATE (ESCAPE_COUNTER);

		if (GET_GAME_STATE (FOUND_PLUTO_SPATHI) == 1)
			InitCommunication (SPATHI_CONVERSATION);
		else if (GET_GAME_STATE (GLOBAL_FLAGS_AND_DATA) == 0)
			InitCommunication (TALKING_PET_CONVERSATION);
		else if (GET_GAME_STATE (GLOBAL_FLAGS_AND_DATA) &
				((1 << 4) | (1 << 5)))
			// Communicate with the Ilwrath using a Hyperwave Broadcaster.
			InitCommunication (ILWRATH_CONVERSATION);
		else
			InitCommunication (CHMMR_CONVERSATION);
		if (GLOBAL_SIS (CrewEnlisted) != (COUNT)~0)
		{
			NextActivity = GLOBAL (CurrentActivity) & ~START_ENCOUNTER;
			if (LOBYTE (NextActivity) == IN_INTERPLANETARY)
				NextActivity |= START_INTERPLANETARY;
			GLOBAL (CurrentActivity) |= CHECK_LOAD; /* fake a load game */
		}

		SET_GAME_STATE (ESCAPE_COUNTER, ec);
		return;
	}
	else if (LOBYTE (GLOBAL (CurrentActivity)) == IN_HYPERSPACE)
	{
		ReinitQueue (&GLOBAL (npc_built_ship_q));
		if (GET_GAME_STATE (ARILOU_SPACE_SIDE) >= 2)
		{
			InitCommunication (ARILOU_CONVERSATION);
			return;
		}
		else
		{
			/* Encounter with a black globe in HS, prepare enemy ship list */
			COUNT NumShips;
			ENCOUNTER *EncounterPtr;

			// The encounter globe that the flagship collided with is moved
			// to the head of the queue in hyper.c:cleanup_hyperspace()
			hEncounter = GetHeadEncounter ();
			LockEncounter (hEncounter, &EncounterPtr);

			NumShips = LONIBBLE (EncounterPtr->SD.Index);
			for (i = 0; i < NumShips; ++i)
			{
				CloneShipFragment (EncounterPtr->SD.Type,
						&GLOBAL (npc_built_ship_q),
						EncounterPtr->ShipList[i].crew_level);
			}

			// XXX: Bug: CurStarDescPtr was abused to point within
			//    an ENCOUNTER struct, which is immediately unlocked
			//CurStarDescPtr = (STAR_DESC*)&EncounterPtr->SD;
			UnlockEncounter (hEncounter);
		}
	}

	// First ship in the npc queue defines which alien race
	// the player will be talking to
	hStarShip = GetHeadLink (&GLOBAL (npc_built_ship_q));
	FragPtr = LockShipFrag (&GLOBAL (npc_built_ship_q), hStarShip);
	i = FragPtr->race_id;
	UnlockShipFrag (&GLOBAL (npc_built_ship_q), hStarShip);

	status = InitCommunication (RaceComm[i]);

	if (GLOBAL (CurrentActivity) & (CHECK_ABORT | CHECK_LOAD))
		return;

	if (i == CHMMR_SHIP)
		ReinitQueue (&GLOBAL (npc_built_ship_q));

	if (LOBYTE (GLOBAL (CurrentActivity)) == IN_INTERPLANETARY)
	{
		/* if used destruct code in interplanetary */
		if (i == SLYLANDRO_SHIP && status == 0)
			ReinitQueue (&GLOBAL (npc_built_ship_q));
	}
	else if (hEncounter)
	{
		/* Update HSpace encounter info, ships lefts, etc. */
		BYTE i, NumShips;
		ENCOUNTER *EncounterPtr;

		LockEncounter (hEncounter, &EncounterPtr);

		NumShips = (BYTE)CountLinks (&GLOBAL (npc_built_ship_q));
		EncounterPtr->SD.Index = MAKE_BYTE (NumShips,
				HINIBBLE (EncounterPtr->SD.Index));
		EncounterPtr->SD.Index |= ENCOUNTER_REFORMING;
		if (status == 0)
			EncounterPtr->SD.Index |= ONE_SHOT_ENCOUNTER;

		for (i = 0; i < NumShips; ++i)
		{
			HSHIPFRAG hStarShip;
			SHIP_FRAGMENT *FragPtr;
			BRIEF_SHIP_INFO *BSIPtr;

			hStarShip = GetStarShipFromIndex (&GLOBAL (npc_built_ship_q), i);
			FragPtr = LockShipFrag (&GLOBAL (npc_built_ship_q), hStarShip);
			BSIPtr = &EncounterPtr->ShipList[i];
			BSIPtr->race_id = FragPtr->race_id;
			BSIPtr->crew_level = FragPtr->crew_level;
			BSIPtr->max_crew = FragPtr->max_crew;
			BSIPtr->max_energy = FragPtr->max_energy;
			UnlockShipFrag (&GLOBAL (npc_built_ship_q), hStarShip);
		}
		
		UnlockEncounter (hEncounter);
		ReinitQueue (&GLOBAL (npc_built_ship_q));
	}
}

void
RedrawSubtitles (void)
{
	TEXT t;

	if (!optSubtitles)
		return;

	LockMutex (subtitle_mutex);
	if (SubtitleText.pStr)
	{
		t = SubtitleText;
		add_text (1, &t);
	}
	UnlockMutex (subtitle_mutex);
}

// Returns clear_subtitles and resets it
BOOLEAN
HaveSubtitlesChanged (void)
{
	BOOLEAN ret;

	LockMutex (subtitle_mutex);
	ret = clear_subtitles;
	clear_subtitles = FALSE;
	UnlockMutex (subtitle_mutex);

	return ret;
}

static void
ClearSubtitles (void)
{
	LockMutex (subtitle_mutex);
	clear_subtitles = TRUE;
	last_subtitle = NULL;
	SubtitleText.pStr = NULL;
	SubtitleText.CharCount = 0;
	UnlockMutex (subtitle_mutex);
}

static void
CheckSubtitles (void)
{
	const UNICODE *pStr;

	pStr = GetTrackSubtitle ();

	LockMutex (subtitle_mutex);
	if (pStr != SubtitleText.pStr)
	{	// Subtitles changed
		clear_subtitles = TRUE;
		// Baseline may be updated by the ZFP
		SubtitleText.baseline = CommData.AlienTextBaseline;
		SubtitleText.align = CommData.AlienTextAlign;
		SubtitleText.pStr = pStr;
		// may have been cleared too
		if (pStr)
			SubtitleText.CharCount = (COUNT)~0;
		else
			SubtitleText.CharCount = 0;
	}
	UnlockMutex (subtitle_mutex);
}
