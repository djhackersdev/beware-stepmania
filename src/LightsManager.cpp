#include "global.h"
#include "LightsManager.h"
#include "GameState.h"
#include "RageTimer.h"
#include "arch/Lights/LightsDriver.h"
#include "RageUtil.h"
#include "GameInput.h"	// for GameController
#include "InputMapper.h"
#include "Game.h"
#include "PrefsManager.h"


static const CString CabinetLightNames[NUM_CABINET_LIGHTS] = {
	"MarqueeUpLeft",
	"MarqueeUpRight",
	"MarqueeLrLeft",
	"MarqueeLrRight",
	"ButtonsLeft",
	"ButtonsRight",
	"BassLeft",
	"BassRight",
};
XToString( CabinetLight );

static const CString LightsModeNames[NUM_LIGHTS_MODES] = {
	"Attract",
	"Joining",
	"Menu",
	"Demonstration",
	"Gameplay",
	"Stage",
	"Cleared",
	"Test",
};
XToString( LightsMode );


LightsManager*	LIGHTSMAN = NULL;	// global and accessable from anywhere in our program

LightsManager::LightsManager(CString sDriver)
{
	m_LightsMode = LIGHTSMODE_JOINING;

	m_pDriver = MakeLightsDriver(sDriver);

	ZERO( m_fSecsLeftInCabinetLightBlink );
	ZERO( m_fSecsLeftInGameButtonBlink );
}

LightsManager::~LightsManager()
{
	SAFE_DELETE( m_pDriver );
}

void LightsManager::Update( float fDeltaTime )
{
	// update lights falloff
	{
		FOREACH_CabinetLight( cl )
			fapproach( m_fSecsLeftInCabinetLightBlink[cl], 0, fDeltaTime );
		FOREACH_GameController( gc )
			FOREACH_GameButton( gb )
				fapproach( m_fSecsLeftInGameButtonBlink[gc][gb], 0, fDeltaTime );
	}

	//
	// Set new lights state cabinet lights
	//
	{
		ZERO( m_LightsState.m_bCabinetLights );
		ZERO( m_LightsState.m_bGameButtonLights );
	}
	int iFrame;
	bool bStuck = false;
	switch( m_LightsMode )
	{
	case LIGHTSMODE_JOINING:
		{
//			int iBeat = (int)(GAMESTATE->m_fSongBeat);
//			bool bBlinkOn = (iBeat%2)==0;

			FOREACH_PlayerNumber( pn )
			{
				if( GAMESTATE->m_bSideIsJoined[pn] )
				{
					m_LightsState.m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT+pn] = true;
					m_LightsState.m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT+pn] = true;
					m_LightsState.m_bCabinetLights[LIGHT_BUTTONS_LEFT+pn] = true;
					m_LightsState.m_bCabinetLights[LIGHT_BASS_LEFT+pn] = true;
				}
			}
		}
		break;
	case LIGHTSMODE_ATTRACT:
		{
			int iSec = (int)RageTimer::GetTimeSinceStart();
			int iTopIndex = iSec % 4;
			switch( iTopIndex )
			{
			case 0:	m_LightsState.m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT]  = true;	break;
			case 1:	m_LightsState.m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT] = true;	break;
			case 2:	m_LightsState.m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT] = true;	break;
			case 3:	m_LightsState.m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT]  = true;	break;
			default:	ASSERT(0);
			}

			bool bOn = (iSec%4)==0;
			m_LightsState.m_bCabinetLights[LIGHT_BASS_LEFT]			= bOn;
			m_LightsState.m_bCabinetLights[LIGHT_BASS_RIGHT]		= bOn;
		}
		break;
	case LIGHTSMODE_MENU:
		{
			FOREACH_CabinetLight( cl )
				m_LightsState.m_bCabinetLights[cl] = false;

			iFrame = ((int)((RageTimer::GetTimeSinceStart() - m_fLightsModeSince) / 0.266)) % 4;
			m_LightsState.m_bCabinetLights[iFrame] = true;

			bool bBlinkOn = (iFrame & 2)!=0;

			FOREACH_PlayerNumber( pn )
			{
				if( GAMESTATE->m_bSideIsJoined[pn] )
				{
					m_LightsState.m_bCabinetLights[LIGHT_BUTTONS_LEFT+pn] = bBlinkOn;
				}
			}
		}
		break;
	case LIGHTSMODE_DEMONSTRATION:
	case LIGHTSMODE_GAMEPLAY:
		{
			bool bBlinkOn = false;

			for (int i = 0; i < 4; i++) if (m_fSecsLeftInCabinetLightBlink[i] > 0) bBlinkOn = true;
		
			FOREACH_CabinetLight( cl )
			if ((cl != 4) && (cl != 5))
				m_LightsState.m_bCabinetLights[cl] = bBlinkOn;

			float timesince = RageTimer::GetTimeSinceStart() - m_fLightsModeSince;
			if ((timesince > 3.8) && (timesince < 4.8))
				for (int i = 0; i < 4; i++) m_LightsState.m_bCabinetLights[i] = true;

			if (timesince < 2.5)
				bStuck = true;

			break;
		}
	case LIGHTSMODE_ALL_CLEARED:
		{
			iFrame = ((int)((RageTimer::GetTimeSinceStart() - m_fLightsModeSince) / 0.066)) % 4;
			m_LightsState.m_bCabinetLights[iFrame] = true;
		}
		break;	

	case LIGHTSMODE_STAGE:
		{
			bStuck = true;
		}
		break;
	case LIGHTSMODE_EVALUATION:
		break;	
	case LIGHTSMODE_TEST:
		{
			int iSec = (int)RageTimer::GetTimeSinceStart();
			FOREACH_CabinetLight( cl )
			{
				bool bOn = (iSec%NUM_CABINET_LIGHTS) == cl;
				m_LightsState.m_bCabinetLights[cl] = bOn;
			}
		}
		break;
	default:
		ASSERT(0);
	}


	// If not joined, has enough credits, and not too late to join, then 
	// blink the menu buttons rapidly so they'll press Start
	{
		int iBeat = (int)(GAMESTATE->m_fSongBeat*4);
		bool bBlinkOn = (iBeat%2)==0;
		FOREACH_PlayerNumber( pn )
		{
			if( !GAMESTATE->m_bSideIsJoined[pn] && GAMESTATE->PlayersCanJoin() && GAMESTATE->EnoughCreditsToJoin() )
			{
				m_LightsState.m_bCabinetLights[LIGHT_BUTTONS_LEFT+pn] = bBlinkOn;
			}
		}
	}


	//
	// Update game controller lights
	//
	// FIXME:  Works only with Game==dance
	// FIXME:  lights pads for players who aren't playing
	switch( m_LightsMode )
	{
	case LIGHTSMODE_ATTRACT:
	case LIGHTSMODE_DEMONSTRATION:
		{
			ZERO( m_LightsState.m_bGameButtonLights );
		}
		break;
	case LIGHTSMODE_ALL_CLEARED:
		{
			iFrame = ((int)((RageTimer::GetTimeSinceStart() - m_fLightsModeSince) * 15)) % 4;
			switch (iFrame)
			{
			case 0:
			m_LightsState.m_bGameButtonLights[0][3] = true;
			m_LightsState.m_bGameButtonLights[1][3] = true;
			break;
			case 1:
			m_LightsState.m_bGameButtonLights[0][1] = true;
			m_LightsState.m_bGameButtonLights[1][0] = true;
			break;
			case 2:
			m_LightsState.m_bGameButtonLights[0][2] = true;
			m_LightsState.m_bGameButtonLights[1][2] = true;
			break;
			case 3:
			m_LightsState.m_bGameButtonLights[0][0] = true;
			m_LightsState.m_bGameButtonLights[1][1] = true;
			}

			FOREACH_GameController( gc )
			{

				// don't blink unjoined sides
				if( !GAMESTATE->m_bSideIsJoined[gc] )
				continue;
				FOREACH_GameButton( gb )
				{
					bool bOn = INPUTMAPPER->IsButtonDown( GameInput(gc,gb) );
					m_LightsState.m_bGameButtonLights[gc][gb] |= bOn;
				}
			}
		}
		break;	
	case LIGHTSMODE_STAGE:
	case LIGHTSMODE_JOINING:
		break;
	case LIGHTSMODE_MENU:
	case LIGHTSMODE_GAMEPLAY:
		{
			if( m_LightsMode == LIGHTSMODE_GAMEPLAY  &&  PREFSMAN->m_bBlinkGameplayButtonLightsOnNote )
			{
				//
				// Blink on notes.
				//
				FOREACH_GameController( gc )
				{
					FOREACH_GameButton( gb )
					{
						m_LightsState.m_bGameButtonLights[gc][gb] = m_fSecsLeftInGameButtonBlink[gc][gb] > 0 ;
					}
				}
			}
			else
			{
				//
				// Blink on button pressess.
				//
				FOREACH_GameController( gc )
				{
					// don't blink unjoined sides
					if( !GAMESTATE->m_bSideIsJoined[gc] )
						continue;

					FOREACH_GameButton( gb )
					{
						bool bOn = INPUTMAPPER->IsButtonDown( GameInput(gc,gb) );
						m_LightsState.m_bGameButtonLights[gc][gb] = bOn;
					}
				}
			}
		}
		break;
	case LIGHTSMODE_EVALUATION:
		break;	

	case LIGHTSMODE_TEST:
		{
			int iSec = (int)RageTimer::GetTimeSinceStart();

			int iNumGameButtonsToShow = GAMESTATE->GetCurrentGame()->GetNumGameplayButtons();

			FOREACH_GameController( gc )
			{
				FOREACH_GameButton( gb )
				{
					bool bOn = gb==(iSec%iNumGameButtonsToShow);
					m_LightsState.m_bGameButtonLights[gc][gb] = bOn;
				}
			}
		}
		break;
	default:
		ASSERT(0);
	}

	// apply new light values we set above
	if (!bStuck) m_pDriver->Set( &m_LightsState );
}

void LightsManager::BlinkCabinetLight( CabinetLight cl )
{
	m_fSecsLeftInCabinetLightBlink[cl] = 0.25 / GAMESTATE->m_fCurBPS; //LIGHTS_FALLOFF_SECONDS;
}

void LightsManager::BlinkGameButton( GameInput gi )
{
	m_fSecsLeftInGameButtonBlink[gi.controller][gi.button] = LIGHTS_FALLOFF_SECONDS;
}

void LightsManager::SetLightsMode( LightsMode lm )
{
	if (m_LightsMode != lm) m_fLightsModeSince = (int)RageTimer::GetTimeSinceStart();
	m_LightsMode = lm;
}

LightsMode LightsManager::GetLightsMode()
{
	return m_LightsMode;
}

/*
 * (c) 2003-2004 Chris Danford
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
