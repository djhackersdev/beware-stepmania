#include "global.h"
#include "GradeDisplay.h"
#include "RageUtil.h"
#include "GameConstantsAndTypes.h"
#include "PrefsManager.h"
#include "ThemeManager.h"
#include "RageTexture.h"
#include "RandomNumber.h"

const float SCROLL_TIME = 3.0f;
const float QUICK_SCROLL_TIME = .25f;
const int NUM_GRADE_FRAMES = 8;
const float GRADE_FRAME_HEIGHT = 1/(float)NUM_GRADE_FRAMES;
const float GRADES_TO_SCROLL = NUM_GRADE_FRAMES*4;
const float SCROLL_UNIT = 0.03125f; //amount of scrolling for one grade difference

int NUM_GRADE_FRAMES_VAR;
float GRADE_FRAME_HEIGHT_VAR;
int scrollkind = -1;
const int numofscrollkinds = 4;

GradeDisplay::GradeDisplay()
{
	m_fTimeLeftInScroll = 0;
	m_bDoScrolling = 0;

	SetGrade( PLAYER_1, GRADE_NO_DATA );
}

bool GradeDisplay::Load( RageTextureID ID )
{
	ID.bStretch = true;
	Sprite::Load( ID );
	Sprite::StopAnimating();
	
	if (Sprite::GetNumStates() == 7)
		NUM_GRADE_FRAMES_VAR = 7;
	else
		NUM_GRADE_FRAMES_VAR = 8;

	GRADE_FRAME_HEIGHT_VAR = 1/(float)NUM_GRADE_FRAMES_VAR;
//	if( Sprite::GetNumStates() != 8 && Sprite::GetNumStates() != 16 )
//		RageException::Throw( "The grade graphic '%s' must have either 8 or 16 frames.", ID.filename.c_str() );
	return true;
}

void GradeDisplay::Update( float fDeltaTime )
{
	Sprite::Update( fDeltaTime );

	if( m_bDoScrolling )
	{
		m_fTimeLeftInScroll -= fDeltaTime;
		m_fTimeLeftInScroll = max( 0, m_fTimeLeftInScroll );

		float fPercentIntoScrolling;
		if( m_bDoScrolling == 1)
		{
			if (scrollkind == 0) {
				//decelerate, settle
				fPercentIntoScrolling = ((m_fTimeLeftInScroll * 1.6f) * (m_fTimeLeftInScroll * 1.6f));
			} else if (scrollkind == 1) {
				//decelerate, stop once in between
				if (m_fTimeLeftInScroll >= 0.52f) {
					fPercentIntoScrolling = 1.2f + (((m_fTimeLeftInScroll-0.52f) * 1.8f) * ((m_fTimeLeftInScroll-0.52f) * 1.8f));
				} else if (m_fTimeLeftInScroll >= 0.16f) {
					fPercentIntoScrolling = 1.2f;
				} else {
					fPercentIntoScrolling = m_fTimeLeftInScroll/0.16f * 1.2f;
				}
			} else if (scrollkind == 2) {
				//decelerate, stop 2 times in between
				if (m_fTimeLeftInScroll >= 0.68f) {
					fPercentIntoScrolling = 2.25f + (((m_fTimeLeftInScroll-0.68f) * 1.6f) * ((m_fTimeLeftInScroll-0.68f) * 1.6f));
				} else if (m_fTimeLeftInScroll >= 0.44f) {
					fPercentIntoScrolling = 2.25f;
				} else if (m_fTimeLeftInScroll >= 0.28f) {
					fPercentIntoScrolling = 1.15f + (m_fTimeLeftInScroll-0.28f)/(0.44f-0.28f) * (2.25f-1.15f);
				} else if (m_fTimeLeftInScroll >= 0.16f) {
					fPercentIntoScrolling = 1.15f;
				} else {
					fPercentIntoScrolling = m_fTimeLeftInScroll/0.16f * 1.15f;
				}
			} else if (scrollkind == 3) {
				//decelerate, stop 3 times in between
				if (m_fTimeLeftInScroll >= 1.00f) {
					fPercentIntoScrolling = 3.48f + (((m_fTimeLeftInScroll-1.00f) * 1.8f) * ((m_fTimeLeftInScroll-1.00f) * 1.8f));
				} else if (m_fTimeLeftInScroll >= 0.68f) {
					fPercentIntoScrolling = 3.48f; 
				} else if (m_fTimeLeftInScroll >= 0.56f) {
					fPercentIntoScrolling = 2.3f + (m_fTimeLeftInScroll-0.56f)/(0.68f-0.56f) * (3.48f-2.3f);
				} else if (m_fTimeLeftInScroll >= 0.44f) {
					fPercentIntoScrolling = 2.3f;
				} else if (m_fTimeLeftInScroll >= 0.28f) {
					fPercentIntoScrolling = 1.2f + (m_fTimeLeftInScroll-0.28f)/(0.44f-0.28f) * (2.3f-1.2f);
				} else if (m_fTimeLeftInScroll >= 0.16f) {
					fPercentIntoScrolling = 1.2f;
				} else {
					fPercentIntoScrolling = m_fTimeLeftInScroll/0.16f * 1.2f;
				}
			} else fPercentIntoScrolling = 0;
			fPercentIntoScrolling = 1 + (fPercentIntoScrolling * SCROLL_UNIT);
		} else {
			fPercentIntoScrolling = 1 - (m_fTimeLeftInScroll/QUICK_SCROLL_TIME);
		}
		
		m_frectCurTexCoords.left   = m_frectStartTexCoords.left*(1-fPercentIntoScrolling)   + m_frectDestTexCoords.left*fPercentIntoScrolling;
		m_frectCurTexCoords.top    = m_frectStartTexCoords.top*(1-fPercentIntoScrolling)    + m_frectDestTexCoords.top*fPercentIntoScrolling;
		m_frectCurTexCoords.right  = m_frectStartTexCoords.right*(1-fPercentIntoScrolling)  + m_frectDestTexCoords.right*fPercentIntoScrolling;
		m_frectCurTexCoords.bottom = m_frectStartTexCoords.bottom*(1-fPercentIntoScrolling) + m_frectDestTexCoords.bottom*fPercentIntoScrolling;

		this->SetCustomTextureRect( m_frectCurTexCoords );
		if (m_fTimeLeftInScroll == 0.0f) 
		{
			m_bDoScrolling = 0;
			scrollkind = -1;
		}
	}
}

void GradeDisplay::DrawPrimitives()
{
	Sprite::DrawPrimitives();
}

int GradeDisplay::GetFrameNo( PlayerNumber pn, Grade g )
{
	// either 8, or 16 states
	int iNumCols;
	switch( Sprite::GetNumStates() )
	{
	default:
		ASSERT(0);
	case 7:		iNumCols=1;	break;
	case 8:		iNumCols=1;	break;
	case 16:	iNumCols=2;	break;
	}

	int iFrame;
	if (NUM_GRADE_FRAMES_VAR == 7)
	{
		switch( g )
		{
		case GRADE_TIER_1:	iFrame = 0;	break;
		case GRADE_TIER_2:	iFrame = 0;	break;
		case GRADE_TIER_3:	iFrame = 3;	break;
		case GRADE_TIER_4:	iFrame = 6;	break;
		case GRADE_TIER_5:	iFrame = 1;	break;
		case GRADE_TIER_6:	iFrame = 5;	break;
		case GRADE_TIER_7:	iFrame = 2;	break;
		case GRADE_FAILED:	iFrame = 4;	break;
		default:		iFrame = 4;	break;
		}

	} else {
		switch( g )
		{
		case GRADE_TIER_1:	iFrame = 0;	break;
		case GRADE_TIER_2:	iFrame = 1;	break;
		case GRADE_TIER_3:	iFrame = 2;	break;
		case GRADE_TIER_4:	iFrame = 3;	break;
		case GRADE_TIER_5:	iFrame = 4;	break;
		case GRADE_TIER_6:	iFrame = 5;	break;
		case GRADE_TIER_7:	iFrame = 6;	break;
		case GRADE_FAILED:	iFrame = 7;	break;
		default:		iFrame = 7;	break;
		}
	}

	iFrame *= iNumCols;
	if( iNumCols==2 )
		iFrame += pn;
	return iFrame;
}

void GradeDisplay::SetGrade( PlayerNumber pn, Grade g )
{
	m_PlayerNumber = pn;
	m_Grade = g;

	m_bDoScrolling = false;
	StopUsingCustomCoords();

	if(g != GRADE_NO_DATA)
	{
		SetState( GetFrameNo(pn,g) );
		SetDiffuse( RageColor(1,1,1,1.0f) );
	} else
		SetDiffuse( RageColor(1,1,1,0) );
}

void GradeDisplay::Spin()
{
	m_bDoScrolling = true;

	int iFrameNo = GetFrameNo( m_PlayerNumber, m_Grade );

	m_frectDestTexCoords = *m_pTexture->GetTextureCoordRect( iFrameNo );
	m_frectStartTexCoords = m_frectDestTexCoords;
	m_frectStartTexCoords.top += GRADES_TO_SCROLL * GRADE_FRAME_HEIGHT_VAR;
	m_frectStartTexCoords.bottom += GRADES_TO_SCROLL * GRADE_FRAME_HEIGHT_VAR;

	m_fTimeLeftInScroll = SCROLL_TIME;

	if (scrollkind == -1) scrollkind = randomnumber(numofscrollkinds);


	/* Set the initial position. */
	Update(0);
}

void GradeDisplay::SettleImmediately()
{
	scrollkind = -1;
	m_fTimeLeftInScroll = 0;
}

void GradeDisplay::SettleQuickly()
{
	scrollkind = -1;
	if(m_bDoScrolling != 1)
		return;

	/* If we're in the last phase of scrolling, don't do this. */
	if( 1 - (m_fTimeLeftInScroll/SCROLL_TIME) >= 0.9 )
		return;

	/* m_frectDestTexCoords.top is between 0 and 1 (inclusive).  m_frectCurTexCoords
	 * is somewhere above that.  Shift m_frectCurTexCoords downwards so it's pointing
	 * at the same physical place (remember, the grade texture is tiled) but no more
	 * than one rotation away from the destination. */
	while(m_frectCurTexCoords.top > m_frectDestTexCoords.top + 1.0f)
	{
		m_frectCurTexCoords.top -= 1.0f;
		m_frectCurTexCoords.bottom -= 1.0f;
	}

	m_frectStartTexCoords = m_frectCurTexCoords;
	m_bDoScrolling = 2;
	m_fTimeLeftInScroll = QUICK_SCROLL_TIME;
}

/*
 * (c) 2001-2002 Chris Danford
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
