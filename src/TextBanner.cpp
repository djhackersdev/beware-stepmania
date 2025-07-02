#include "global.h"
#include "TextBanner.h"
#include "RageUtil.h"
#include "song.h"
#include "PrefsManager.h"
#include "ThemeManager.h"
#include "SongManager.h"
#include "RageTextureManager.h"
#include "ActorUtil.h"
#include "RageLog.h"

CachedThemeMetric ARTIST_PREPEND_STRING			("TextBanner","ArtistPrependString");
CachedThemeMetric TWO_LINES_TITLE_COMMAND		("TextBanner","TwoLinesTitleCommand");
CachedThemeMetric TWO_LINES_SUBTITLE_COMMAND	("TextBanner","TwoLinesSubtitleCommand");
CachedThemeMetric TWO_LINES_ARTIST_COMMAND		("TextBanner","TwoLinesArtistCommand");
CachedThemeMetric THREE_LINES_TITLE_COMMAND		("TextBanner","ThreeLinesTitleCommand");
CachedThemeMetric THREE_LINES_SUBTITLE_COMMAND	("TextBanner","ThreeLinesSubtitleCommand");
CachedThemeMetric THREE_LINES_ARTIST_COMMAND	("TextBanner","ThreeLinesArtistCommand");
#define CARD_X		THEME->GetMetricF("TextBanner","CardX")
#define CARD_Y		THEME->GetMetricF("TextBanner","CardY")

void TextBanner::Init()
{
	if( m_bInitted )
		return;
	m_bInitted = true;

	ASSERT( m_sName != "" );
	ARTIST_PREPEND_STRING.Refresh( m_sName );
	TWO_LINES_TITLE_COMMAND.Refresh( m_sName );
	TWO_LINES_SUBTITLE_COMMAND.Refresh( m_sName );
	TWO_LINES_ARTIST_COMMAND.Refresh( m_sName );
	THREE_LINES_TITLE_COMMAND.Refresh( m_sName );
	THREE_LINES_SUBTITLE_COMMAND.Refresh( m_sName );
	THREE_LINES_ARTIST_COMMAND.Refresh( m_sName );

	m_textTitle.SetName( "Title" );
	m_textTitle.LoadFromFont( THEME->GetPathToF("TextBanner") );
	SET_XY_AND_ON_COMMAND( m_textTitle );
	this->AddChild( &m_textTitle );

	m_textSubTitle.SetName( "Subtitle" );
	m_textSubTitle.LoadFromFont( THEME->GetPathToF("TextBanner") );
	SET_XY_AND_ON_COMMAND( m_textSubTitle );
	this->AddChild( &m_textSubTitle );

	m_textArtist.SetName( "Artist" );
	m_textArtist.LoadFromFont( THEME->GetPathToF("TextBanner") );
	SET_XY_AND_ON_COMMAND( m_textArtist );
	this->AddChild( &m_textArtist );

	m_sprCard.SetName("Card");
	m_sprCard.SetXY(CARD_X,CARD_Y);
	this->AddChild( &m_sprCard );
}

TextBanner::TextBanner()
{
	m_bInitted = false;
}


void TextBanner::LoadFromString( 
	CString sDisplayTitle, CString sTranslitTitle, 
	CString sDisplaySubTitle, CString sTranslitSubTitle, 
	CString sDisplayArtist, CString sTranslitArtist )
{
	Init();

	m_sprCard.SetZoom(0);

	bool bTwoLines = sDisplaySubTitle.size() == 0;

	if( bTwoLines )
	{
		m_textTitle.Command( TWO_LINES_TITLE_COMMAND );
		m_textSubTitle.Command( TWO_LINES_SUBTITLE_COMMAND );
		m_textArtist.Command( TWO_LINES_ARTIST_COMMAND );
	}
	else
	{
		m_textTitle.Command( THREE_LINES_TITLE_COMMAND );
		m_textSubTitle.Command( THREE_LINES_SUBTITLE_COMMAND );
		m_textArtist.Command( THREE_LINES_ARTIST_COMMAND );
	}

	m_textTitle.SetText( sDisplayTitle, sTranslitTitle );
	m_textSubTitle.SetText( sDisplaySubTitle, sTranslitSubTitle );
	m_textArtist.SetText( sDisplayArtist, sTranslitArtist );
}

void TextBanner::LoadFromSong( const Song* pSong )
{
	Init();

	CString temp = pSong ? (pSong->GetSongDir() + "card.png") : CString("");
	m_bHasCard = IsAFile(temp);
	if (m_bHasCard)
	{
		LoadFromString( CString(""), CString(""), CString(""), CString(""), CString(""), CString(""));
		m_sprCard.SetZoom(1);
		m_sprCard.Load(temp);
		return;
	}

	CString sDisplayTitle = pSong ? pSong->GetDisplayMainTitle() : CString("");
	CString sTranslitTitle = pSong ? pSong->GetTranslitMainTitle() : CString("");
	CString sDisplaySubTitle = pSong ? pSong->GetDisplaySubTitle() : CString("");
	CString sTranslitSubTitle = pSong ? pSong->GetTranslitSubTitle() : CString("");
	CString sDisplayArtist = pSong ? (CString)ARTIST_PREPEND_STRING + pSong->GetDisplayArtist() : CString("");
	CString sTranslitArtist = pSong ? (CString)ARTIST_PREPEND_STRING + pSong->GetTranslitArtist() : CString("");

	LoadFromString( 
		sDisplayTitle, sTranslitTitle, 
		sDisplaySubTitle, sTranslitSubTitle, 
		sDisplayArtist, sTranslitArtist );
}

/*
 * (c) 2001-2004 Chris Danford
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
