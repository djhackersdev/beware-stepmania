#include "global.h"
#include "MusicWheelItem.h"
#include "RageUtil.h"
#include "SongManager.h"
#include "GameManager.h"
#include "PrefsManager.h"
#include "ScreenManager.h"	// for sending SM_PlayMusicSample
#include "RageLog.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "ThemeManager.h"
#include "Steps.h"
#include "song.h"
#include "Course.h"
#include "ProfileManager.h"
#include "ActorUtil.h"


// WheelItem stuff
#define ICON_X			THEME->GetMetricF("MusicWheelItem","IconX")
#define SONG_NAME_X		THEME->GetMetricF("MusicWheelItem","SongNameX")
#define SECTION_NAME_X	THEME->GetMetricF("MusicWheelItem","SectionNameX")
#define SECTION_ZOOM	THEME->GetMetricF("MusicWheelItem","SectionZoom")
#define SECTION_ZOOM_X	THEME->GetMetricF("MusicWheelItem","SectionZoomX")
#define SECTION_ZOOM_Y	THEME->GetMetricF("MusicWheelItem","SectionZoomY")
#define ROULETTE_X		THEME->GetMetricF("MusicWheelItem","RouletteX")
#define ROULETTE_ZOOM	THEME->GetMetricF("MusicWheelItem","RouletteZoom")
#define GRADE_X( p )	THEME->GetMetricF("MusicWheelItem",ssprintf("GradeP%dX",p+1))
#define ABC_X	THEME->GetMetricF("MusicWheelItem","AbcX")
#define ABC_ZOOMX	THEME->GetMetricF("MusicWheelItem","AbcZoomX")
#define ABC_ZOOMY	THEME->GetMetricF("MusicWheelItem","AbcZoomY")
#define MAX_TEXT_WIDTH	THEME->GetMetricF("MusicWheelItem","MaxTextWidth")

#define CARD_X		THEME->GetMetricF("TextBanner","CardX")
#define CARD_Y		THEME->GetMetricF("TextBanner","CardY")
#define CARD_SECTION_X	THEME->GetMetricF("MusicWheelItem","CardSectionX")
#define CARD_SECTION_Y	THEME->GetMetricF("MusicWheelItem","CardSectionY")
#define CARD_COURSE_X	THEME->GetMetricF("MusicWheelItem","CardCourseX")
#define CARD_COURSE_Y	THEME->GetMetricF("MusicWheelItem","CardCourseY")
#define CARD_ROULETTE_X	THEME->GetMetricF("MusicWheelItem","CardRouletteX")
#define CARD_ROULETTE_Y	THEME->GetMetricF("MusicWheelItem","CardRouletteY")
#define TEXTBANNER2_ONCOMMAND	THEME->GetMetric("MusicWheelItem","TextBanner2OnCommand")

WheelItemData::WheelItemData( WheelItemType wit, Song* pSong, CString sSectionName, Course* pCourse, RageColor color, SortOrder so )
{
	m_Type = wit;
	m_pSong = pSong;
	m_sSectionName = sSectionName;
	m_pCourse = pCourse;
	m_color = color;
	m_Flags = WheelNotifyIcon::Flags();
	m_SortOrder = so;
}


MusicWheelItem::MusicWheelItem()
{
	data = NULL;

	SetName( "MusicWheelItem" );

	m_fPercentGray = 0;
	m_WheelNotifyIcon.SetXY( ICON_X, 0 );
	
	m_TextBanner.SetName( "TextBanner" );
	m_TextBanner.SetHorizAlign( align_left );
	m_TextBanner.SetXY( SONG_NAME_X, 0 );
	m_All.AddChild( &m_TextBanner );
	
	m_TextBanner2.SetName( "TextBanner2" );
	m_TextBanner2.SetHorizAlign( align_left );
	m_TextBanner2.SetXY( SONG_NAME_X, 0 );
	m_TextBanner2.Command( TEXTBANNER2_ONCOMMAND );
	m_TextBanner2.m_sprCard.Command( TEXTBANNER2_ONCOMMAND );
	m_TextBanner2.m_textTitle.Command( TEXTBANNER2_ONCOMMAND );
	m_TextBanner2.m_textSubTitle.Command( TEXTBANNER2_ONCOMMAND );
	m_TextBanner2.m_textArtist.Command( TEXTBANNER2_ONCOMMAND );
	
	m_All.AddChild( &m_TextBanner2 );

	m_sprSongBar.Load( THEME->GetPathToG("MusicWheelItem song") );
	m_sprSongBar.SetXY( 0, 0 );
	m_All.AddChild( &m_sprSongBar );

	m_sprSectionBar.Load( THEME->GetPathToG("MusicWheelItem section") );
	m_sprSectionBar.SetXY( 0, 0 );
	m_All.AddChild( &m_sprSectionBar );

	m_textSectionName.SetName("SectionName");
	m_textSectionName.LoadFromFont( THEME->GetPathToF("MusicWheelItem section") );
	m_textSectionName.SetShadowLength( 0 );
	m_textSectionName.SetVertAlign( align_middle );
	m_textSectionName.SetXY( SECTION_NAME_X, 0 );
	m_textSectionName.SetZoomX( SECTION_ZOOM_X );
	m_textSectionName.SetZoomY( SECTION_ZOOM_Y );
	ON_COMMAND(m_textSectionName);
	m_All.AddChild( &m_textSectionName );

	m_textRoulette.LoadFromFont( THEME->GetPathToF("MusicWheelItem roulette") );
	m_textRoulette.SetShadowLength( 0 );
	m_textRoulette.TurnRainbowOn();
	m_textRoulette.SetZoom( ROULETTE_ZOOM );
	m_textRoulette.SetXY( ROULETTE_X, 0 );
	m_All.AddChild( &m_textRoulette );

	FOREACH_PlayerNumber( p )
	{
		m_GradeDisplay[p].Load( THEME->GetPathToG("MusicWheelItem grades") );
		m_GradeDisplay[p].SetZoom( 1.0f );
		m_GradeDisplay[p].SetXY( GRADE_X(p), 0 );
		m_All.AddChild( &m_GradeDisplay[p] );
	}

	m_textCourse.SetName( "CourseName" );
	m_textCourse.LoadFromFont( THEME->GetPathToF("MusicWheelItem course") );
	SET_XY_AND_ON_COMMAND( &m_textCourse );
	m_All.AddChild( &m_textCourse );

	m_textSort.SetName( "Sort" );
	m_textSort.LoadFromFont( THEME->GetPathToF("MusicWheelItem sort") );
	SET_XY_AND_ON_COMMAND( &m_textSort );
	m_All.AddChild( &m_textSort );

	m_sprCard.SetName("Card");
	m_All.AddChild( &m_sprCard );
}


void MusicWheelItem::LoadFromWheelItemData( WheelItemData* pWID )
{
	ASSERT( pWID != NULL );
	
	CString temp;
	
	data = pWID;
	/*
	// copy all data items
	this->m_Type	= pWID->m_Type;
	this->m_sSectionName	= pWID->m_sSectionName;
	this->m_pCourse			= pWID->m_pCourse;
	this->m_pSong			= pWID->m_pSong;
	this->m_color			= pWID->m_color;
	this->m_Type		= pWID->m_Type; */

	m_bHasCard = false;
	m_sprCard.SetXY(CARD_X,CARD_Y);

	// init type specific stuff
	switch( pWID->m_Type )
	{
	case TYPE_SECTION:
	case TYPE_COURSE:
	case TYPE_SORT:
		{
			CString sDisplayName, sTranslitName;
			BitmapText *bt = NULL;
			switch( pWID->m_Type )
			{
				case TYPE_SECTION:
					temp = "Songs/" + data->m_sSectionName + "/card.png";
					m_bHasCard = IsAFile(temp);

					if (m_bHasCard)
					{
						m_sprCard.SetXY(CARD_SECTION_X,CARD_SECTION_Y);
						m_sprCard.SetZoom(1);
						m_sprCard.Load(temp);
						m_sprCard.SetDiffuse( data->m_color );
						sDisplayName = "";
					} else
					{
						sDisplayName = SONGMAN->ShortenGroupName(data->m_sSectionName);
					}
					m_textSectionName.LoadFromFont( THEME->GetPathToF("MusicWheelItem section") );
					bt = &m_textSectionName;
					break;
				case TYPE_COURSE:
					temp = SetExtension(data->m_pCourse->m_sPath, "");
					//temp.Left( temp.GetLength()-4 );
					temp = temp + "-card.png";
					m_bHasCard = IsAFile(temp);
					
					if (m_bHasCard)
					{
						m_sprCard.SetXY(CARD_COURSE_X,CARD_COURSE_Y);
						m_sprCard.SetZoom(1);
						m_sprCard.Load(temp);
						m_sprCard.SetDiffuse( data->m_color );
						sDisplayName = "";
					} else
					{
						sDisplayName = data->m_pCourse->GetFullDisplayTitle();
					}
					sTranslitName = data->m_pCourse->GetFullTranslitTitle();
					bt = &m_textCourse;
					break;
				case TYPE_SORT:
					sDisplayName = data->m_sLabel;
					bt = &m_textSort;
					break;
				default:
					ASSERT(0);
			}

			
			if (sDisplayName == "NUM" && pWID->m_Type == TYPE_SECTION) {
				m_textSectionName.LoadFromFont( THEME->GetPathToF("MusicWheelItem abc") );
				bt->SetZoomX( ABC_ZOOMX );
				bt->SetZoomY( ABC_ZOOMY );
				m_textSectionName.SetXY( ABC_X, 0 );
				sDisplayName = "[";
			} else if (sDisplayName.length() == 1 && pWID->m_Type == TYPE_SECTION) {
				m_textSectionName.LoadFromFont( THEME->GetPathToF("MusicWheelItem abc") );
				bt->SetZoomX( ABC_ZOOMX );
				bt->SetZoomY( ABC_ZOOMY );
				m_textSectionName.SetXY( ABC_X, 0 );
			} else {
				bt->SetZoomX( SECTION_ZOOM_X );
				bt->SetZoomY( SECTION_ZOOM_Y );
				m_textSectionName.SetXY( SECTION_NAME_X, 0 );
			}
			bt->SetText( sDisplayName, sTranslitName );
			bt->SetDiffuse( data->m_color );
			bt->TurnRainbowOff();
			
			const float fSourcePixelWidth = (float)bt->GetUnzoomedWidth() * bt->GetZoomX();
			const float fMaxTextWidth = MAX_TEXT_WIDTH;
			if( fSourcePixelWidth > fMaxTextWidth  )
			{
				const float factor = fMaxTextWidth / fSourcePixelWidth;
				bt->SetZoomX( bt->GetZoomX() * factor );
				bt->SetZoomY( bt->GetZoomY() * factor );
			}
		}
		break;
	case TYPE_SONG:
		{
			m_TextBanner.LoadFromSong( data->m_pSong );
			m_TextBanner.SetDiffuse( data->m_color );
			
			RageColor tempcolor;

			if ((data->m_color.r == 0.0) && (data->m_color.b == 1.0) && (data->m_color.a == 1.0)
				&& (data->m_color.g >= 0.7) && (data->m_color.g <= 0.85))
			{
				tempcolor = RageColor(0,0,1,1);
			} else tempcolor = RageColor(0,0,0,0);

			
			m_TextBanner2.LoadFromSong( data->m_pSong );
			m_TextBanner2.SetDiffuse( tempcolor );
			
			m_WheelNotifyIcon.SetFlags( data->m_Flags );
			RefreshGrades();
		}
		break;
	case TYPE_ROULETTE:
		m_sprCard.SetXY(CARD_ROULETTE_X,CARD_ROULETTE_Y);
		m_sprCard.SetDiffuse( RageColor(1,1,1,1) );
		m_sprCard.SetZoom(1);
		m_sprCard.Load(THEME->GetPathToG("MusicWheelItem roulette"));
		m_bHasCard = true;
		break;

	case TYPE_RANDOM:
		m_sprCard.SetXY(CARD_ROULETTE_X,CARD_ROULETTE_Y);
		m_sprCard.SetDiffuse( RageColor(1,1,1,1) );
		m_sprCard.SetZoom(1);
		m_sprCard.Load(THEME->GetPathToG("MusicWheelItem random"));
		m_bHasCard = true;
		break;

	case TYPE_PORTAL:
		m_textRoulette.SetText( THEME->GetMetric("MusicWheel","Portal") );
		break;

	default:
		ASSERT( 0 );	// invalid type
	}
}

void MusicWheelItem::RefreshGrades()
{
	// Refresh Grades
	FOREACH_PlayerNumber( p )
	{
		if( !data->m_pSong  ||	// this isn't a song display
			!GAMESTATE->IsHumanPlayer(p) )
		{
			m_GradeDisplay[p].SetDiffuse( RageColor(1,1,1,0) );
			continue;
		}

		Difficulty dc;
		if( GAMESTATE->m_pCurSteps[p] )
			dc = GAMESTATE->m_pCurSteps[p]->GetDifficulty();
		else
			dc = GAMESTATE->m_PreferredDifficulty[p];
		Grade grade;
		if( PROFILEMAN->IsUsingProfile((PlayerNumber)p) )
			grade = PROFILEMAN->GetHighScoreForDifficulty( data->m_pSong, GAMESTATE->GetCurrentStyle(), (ProfileSlot)p, dc ).grade;
		else
			grade = PROFILEMAN->GetHighScoreForDifficulty( data->m_pSong, GAMESTATE->GetCurrentStyle(), PROFILE_SLOT_MACHINE, dc ).grade;
		if (grade == GRADE_TIER_1)
			grade = GRADE_TIER_2;
		if (grade == GRADE_NO_DATA)
			grade = GRADE_TIER_1;

		m_GradeDisplay[p].SetGrade( (PlayerNumber)p, grade );
	}

}


void MusicWheelItem::Update( float fDeltaTime )
{
	Actor::Update( fDeltaTime );

	if (m_bHasCard)
	m_sprCard.Update( fDeltaTime );

	switch( data->m_Type )
	{
	case TYPE_SECTION:
		m_sprSectionBar.Update( fDeltaTime );
		m_textSectionName.Update( fDeltaTime );
		break;
	case TYPE_ROULETTE:
	case TYPE_RANDOM:
	case TYPE_PORTAL:
		m_sprSectionBar.Update( fDeltaTime );
		m_textRoulette.Update( fDeltaTime );
		break;
	case TYPE_SONG:
		{
			m_sprSongBar.Update( fDeltaTime );
			m_WheelNotifyIcon.Update( fDeltaTime );
			m_TextBanner.Update( fDeltaTime );
			m_TextBanner2.Update( fDeltaTime );
			FOREACH_PlayerNumber( p )
				m_GradeDisplay[p].Update( fDeltaTime );
		}
		break;
	case TYPE_COURSE:
		m_sprSongBar.Update( fDeltaTime );
		m_textCourse.Update( fDeltaTime );
		break;
	case TYPE_SORT:
		m_sprSectionBar.Update( fDeltaTime );
		m_textSort.Update( fDeltaTime );
		break;
	default:
		ASSERT(0);
	}
}

void MusicWheelItem::DrawPrimitives()
{
	Sprite *bar = NULL;
	switch( data->m_Type )
	{
	case TYPE_SECTION: 
	case TYPE_ROULETTE:
	case TYPE_RANDOM:
	case TYPE_PORTAL:
	case TYPE_SORT:
		bar = &m_sprSectionBar; 
		break;
	case TYPE_SONG:		
	case TYPE_COURSE:
		bar = &m_sprSongBar; 
		break;
	default: ASSERT(0);
	}
	
	bar->Draw();

	if (m_bHasCard) m_sprCard.Draw();

	switch( data->m_Type )
	{
	case TYPE_SECTION:
		m_textSectionName.Draw();
		break;
	case TYPE_ROULETTE:
	case TYPE_RANDOM:
	case TYPE_PORTAL:
		m_textRoulette.Draw();
		break;
	case TYPE_SONG:		
		m_TextBanner.Draw();
		m_TextBanner2.Draw();
		m_WheelNotifyIcon.Draw();
		FOREACH_PlayerNumber( p )
			m_GradeDisplay[p].Draw();
		break;
	case TYPE_COURSE:
		m_textCourse.Draw();
		break;
	case TYPE_SORT:
		m_textSort.Draw();
		break;
	default:
		ASSERT(0);
	}

	if( m_fPercentGray > 0 )
	{
		bar->SetGlow( RageColor(0,0,0,m_fPercentGray) );
		bar->SetDiffuse( RageColor(0,0,0,0) );
		bar->Draw();
		bar->SetDiffuse( RageColor(1,1,1,1) ); //make the background fade to black properly -beware
		bar->SetGlow( RageColor(0,0,0,0) );
	}
}


void MusicWheelItem::SetZTestMode( ZTestMode mode )
{
	ActorFrame::SetZTestMode( mode );

	// set all sub-Actors
	m_All.SetZTestMode( mode );
}

void MusicWheelItem::SetZWrite( bool b )
{
	ActorFrame::SetZWrite( b );

	// set all sub-Actors
	m_All.SetZWrite( b );
}

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez, Glenn Maynard
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
