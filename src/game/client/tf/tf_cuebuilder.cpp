#include "cbase.h"
#include "tf_cuebuilder.h"
#include "tier3/tier3.h"
#include "cdll_util.h"
#include "engine/IEngineSound.h"
#include "soundenvelope.h"
#include "c_script_parser.h"
#include "c_tf_player.h"
#include "tf_gamerules.h"
#include "c_playerresource.h"
#include "c_tf_playerresource.h"

const char *g_aCueMood[MOOD_COUNT] =
{
	"MOOD_NEUTRAL",
	"MOOD_DANGER",
	"MOOD_DEATH"
};

const char *g_aCueLayer[LAYER_COUNT] =
{
	"LAYER_MAIN",
	"LAYER_BASS",
	"LAYER_PERC",
	"LAYER_MISC"
};


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static CTFCueBuilder g_TFCueBuilder;
CTFCueBuilder *GetCueBuilder()
{
	return &g_TFCueBuilder;
}

class CTFMusicScriptParser : public C_ScriptParser
{
public:
	DECLARE_CLASS_GAMEROOT(CTFMusicScriptParser, C_ScriptParser);

	void Parse(KeyValues *pKeyValuesData, bool bWildcard, const char *szFileWithoutEXT)
	{
		int id = -1;
		CueTrack *pTrack = new CueTrack(GetCueBuilder(), szFileWithoutEXT);

		for (KeyValues *pData = pKeyValuesData->GetFirstSubKey(); pData != NULL; pData = pData->GetNextKey())
		{
			id++;

			CueSequence pSeq;
			pSeq.id = id;
			Q_strncpy(pSeq.sName, pData->GetString("name", ""), sizeof(pSeq.sName));
			pSeq.volume = pData->GetInt("volume", 0);
			pSeq.pitch = pData->GetInt("pitch", PITCH_NORM);
			if (!Q_strncasecmp(pData->GetString("soundlevel", ""), "SNDLVL_", strlen("SNDLVL_")))
			{
				pSeq.soundlevel = TextToSoundLevel(pData->GetString("soundlevel", "SNDLVL_NONE"));
			}
			else
			{
				pSeq.soundlevel = (soundlevel_t)pData->GetInt("soundlevel", ATTN_TO_SNDLVL(ATTN_NORM));
			}

			for (int i = 0; i < MOOD_COUNT * LAYER_COUNT; i++)
			{
				Q_strncpy(pSeq.pTracks[i], "", sizeof(pSeq.pTracks[i]));
			}
			for (KeyValues *pLayerData = pData->GetFirstSubKey(); pLayerData != NULL; pLayerData = pLayerData->GetNextKey())
			{
				for (int i = LAYER_MAIN; i < LAYER_COUNT; i++)
				{
					if (!Q_stricmp(pLayerData->GetName(), g_aCueLayer[i]))
					{
						for (KeyValues *pMoodData = pLayerData->GetFirstSubKey(); pMoodData != NULL; pMoodData = pMoodData->GetNextKey())
						{
							for (int j = MOOD_NEUTRAL; j < MOOD_COUNT; j++)
							{
								if (!Q_stricmp(pMoodData->GetName(), g_aCueMood[j]))
								{
									char sTrackName[64];
									Q_strncpy(sTrackName, pMoodData->GetString("wave", ""), sizeof(sTrackName));
									pSeq.AddTrack(sTrackName, (CueLayer)i, (CueMood)j);
								}
							}
						}
					}
				}
			}
			pTrack->AddSequence(pSeq, pData->GetName());
		}
		pTFCueBuilder->AddTrack(szFileWithoutEXT, pTrack);
	};

	void SetCueBuilder(CTFCueBuilder *pBuilder)
	{
		pTFCueBuilder = pBuilder;
	}

private:
	CTFCueBuilder *pTFCueBuilder;
	CUtlDict< CueSequence, unsigned short > m_TrackInfoDatabase;
};
CTFMusicScriptParser g_TFMusicScriptParser;


void PlayDynamic(const CCommand &args)
{
	const char* sName = args[1];
	GetCueBuilder()->StopCue();
	GetCueBuilder()->SetCurrentTrack(sName);
	GetCueBuilder()->StartCue();
}
ConCommand playdynamic("playdynamic", PlayDynamic);

void StopDynamic(const CCommand &args)
{
	GetCueBuilder()->StopCue();
}
ConCommand stopdynamic("stopdynamic", StopDynamic);

void SkipDynamic(const CCommand &args)
{
	GetCueBuilder()->SetShouldSkip(true);
}
ConCommand skipdynamic("skipdynamic", SkipDynamic);

void SetMood(const CCommand &args)
{
	int iMood = atoi(args[1]);
	GetCueBuilder()->SetMood((CueMood)iMood);
}
ConCommand playdefault("setmood", SetMood);


//-----------------------------------------------------------------------------
// Purpose: constructor
//-----------------------------------------------------------------------------
CTFCueBuilder::CTFCueBuilder() : CAutoGameSystemPerFrame("CTFCueBuilder")
{
	if (!filesystem)
		return;

	m_bInited = false;
	Init();
}

CTFCueBuilder::~CTFCueBuilder()
{
	m_PlaylistDatabase.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Initializer
//-----------------------------------------------------------------------------
bool CTFCueBuilder::Init()
{
	if (!m_bInited)
	{
		m_iGlobalMood = MOOD_NEUTRAL;
		m_iCurrentTrack = -1;

		g_TFMusicScriptParser.SetCueBuilder(this);
		g_TFMusicScriptParser.InitParser("scripts/tf_music_*.txt", true, false);

		ListenForGameEvent("server_spawn");
		ListenForGameEvent("localplayer_changeteam");
		ListenForGameEvent("player_death");
		ListenForGameEvent("teamplay_win_panel");
		ListenForGameEvent("teamplay_round_start");
		m_bInited = true;
	}

	return true;
}

void CTFCueBuilder::Update(float frametime)
{
	for (unsigned int i = 0; i < m_PlaylistDatabase.Count(); i++)
	{
		m_PlaylistDatabase[i]->Update();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Event handler
//-----------------------------------------------------------------------------
void CTFCueBuilder::FireGameEvent(IGameEvent *event)
{
	const char *type = event->GetName();

	if (!TFGameRules())
		return;

	if (0 == Q_strcmp(type, "localplayer_changeteam"))
	{
		if (TFGameRules()->IsDeathmatch())
		{
			GetCueBuilder()->StopCue();
			GetCueBuilder()->SetCurrentTrack("tf_music_deathmatch");
			GetCueBuilder()->ResetAndStartCue();
		}
	}

	if (0 == Q_strcmp(type, "teamplay_round_start"))
	{
		if (TFGameRules()->IsDeathmatch())
		{
			GetCueBuilder()->StopCue();
			GetCueBuilder()->SetCurrentTrack("tf_music_deathmatch");
			GetCueBuilder()->ResetAndStartCue();
		}
	}

	if (0 == Q_strcmp(type, "server_spawn"))
	{
		if (TFGameRules()->IsDeathmatch())
		{
			GetCueBuilder()->StopCue();
		}
	}

	if (0 == Q_strcmp(type, "teamplay_win_panel"))
	{
		if (TFGameRules()->IsDeathmatch())
		{
			GetCueBuilder()->StopCue();
		}
	}

	if (0 == Q_strcmp(type, "player_death"))
	{
		if (TFGameRules()->IsDeathmatch())
		{
			C_TF_PlayerResource *tf_PR = dynamic_cast<C_TF_PlayerResource *>(g_PR);
			if (!tf_PR)
				return;

			int iSeqID = GetCueBuilder()->GetCurrentTrack()->GetCurrentSeqID();
			int iLocalIndex = GetLocalPlayerIndex();
			int iLocalScore = tf_PR->GetTotalScore(iLocalIndex);
			int iLocalKillstreak = tf_PR->GetKillstreak(iLocalIndex);

			if (iLocalScore > 1 && iSeqID == 0)
			{
				GetCueBuilder()->GetCurrentTrack()->SetShouldSkip(true);
			}
			
			int userid = event->GetInt("userid");
			if (userid == iLocalIndex + 1)
			{
				GetCueBuilder()->SetMood(MOOD_NEUTRAL);
			}
			else if (iLocalKillstreak > 3)
			{
				GetCueBuilder()->SetMood(MOOD_DANGER);
			}
		}
	}
}

void CTFCueBuilder::AddTrack(const char* name, CueTrack* pCueTrack)
{
	m_PlaylistDatabase.Insert(name, pCueTrack);
}

void CTFCueBuilder::SetMood(CueMood mood)
{
	DevMsg("Mood set to %s\n", g_aCueMood[mood]);
	m_iGlobalMood = mood;
	GetCueBuilder()->GetCurrentTrack()->SetVolumes();
};

void CTFCueBuilder::StartCue()
{
	CueTrack *pCurrentTrack = GetCurrentTrack();
	DevMsg("Playing track %s\n", pCurrentTrack->GetTrackName());	
	pCurrentTrack->StartPlaying();
}

void CTFCueBuilder::ResetAndStartCue()
{
	GetCueBuilder()->GetCurrentTrack()->SetCurrentSeqID(-1);
	GetCueBuilder()->SetMood(MOOD_NEUTRAL);
	GetCueBuilder()->StartCue();
}

void CTFCueBuilder::StopCue()
{
	int iCurrentID = GetCueBuilder()->GetCurrentTrackID();
	if (iCurrentID < 0)
		return;

	CueTrack *pCurrentTrack = GetCurrentTrack();
	DevMsg("Stop playing track %s\n", pCurrentTrack->GetTrackName());
	pCurrentTrack->StopPlaying();
}



//-----------------------------------------------------------------------------
// Purpose: constructor
//-----------------------------------------------------------------------------
CueTrack::CueTrack(CTFCueBuilder *g_TFCueBuilder, const char* sName)
{
	pTFCueBuilder = g_TFCueBuilder;
	Q_strncpy(m_sName, sName, sizeof(m_sName));
	m_iCurrentSequence = -1;
	for (int i = 0; i < LAYER_COUNT * MOOD_COUNT; i++)
		m_pPlayList[i] = NULL;
	m_TrackInfoDatabase.RemoveAll();
	m_bPlay = false;
}

void CueTrack::Update()
{
	if (!m_bPlay)
		return;

	C_TFPlayer *pLocalPlayer = C_TFPlayer::GetLocalTFPlayer();
	if (!pLocalPlayer)
		return;

	bool bPlaying = IsStillPlaying();
	bool bLoopEnded = false;
	if (GetCurrentSeqID() > -1)
	{
		CueSequence pSeqInfo = GetSequenceInfo(GetCurrentSeqID());
		float fDuration = enginesound->GetSoundDuration(pSeqInfo.GetTrack(LAYER_MAIN, MOOD_NEUTRAL));
		bLoopEnded = m_fCurrentDuration + fDuration < gpGlobals->curtime;
	}
	if (bLoopEnded)
	{
		DevMsg("Loop ended\n");
		m_fCurrentDuration = gpGlobals->curtime;
	}
	if (!bPlaying || (bLoopEnded && GetShouldSkip()))
	{
		m_fCurrentDuration = gpGlobals->curtime;
		SetShouldSkip(false);
		Stop();
		NextSeq();
		Play();
	}
}

GUID CueTrack::PlayLayer(int ID, CueLayer Layer, CueMood Mood)
{
	char m_pzMusicLink[64];
	CueSequence pSongInfo = GetSequenceInfo(ID);

	Q_strncpy(m_pzMusicLink, pSongInfo.GetTrack(Layer, Mood), sizeof(m_pzMusicLink));
	if (m_pzMusicLink[0] == '\0')
	{
		Q_strncpy(m_pzMusicLink, pSongInfo.GetTrack(Layer, MOOD_NEUTRAL), sizeof(m_pzMusicLink));
	}

	if (m_pzMusicLink[0] == '\0')
		return 0;
	
	CLocalPlayerFilter filter;
	enginesound->EmitSound(filter, SOUND_FROM_LOCAL_PLAYER, CHAN_AUTO, m_pzMusicLink, pSongInfo.volume, pSongInfo.soundlevel, 0, pSongInfo.pitch);
	GUID guid = enginesound->GetGuidForLastSoundEmitted();
	SetGuid(guid, Layer, Mood);
	return guid;
}


void CueTrack::AddSequence(CueSequence pSequence, const char* name)
{
	m_TrackInfoDatabase.Insert(name, pSequence);
}

CueSequence CueTrack::GetSequenceInfo(int ID)
{
	return m_TrackInfoDatabase[ID];
}

int CueTrack::GetSeqCount()
{
	return m_TrackInfoDatabase.Count();
}

bool CueTrack::GetShouldSkip() 
{
	return pTFCueBuilder->GetShouldSkip(); 
};

void CueTrack::SetShouldSkip(bool bSkip)
{
	if (bSkip)
	{
		DevMsg("Skipping to the next loop\n");
	}
	pTFCueBuilder->SetShouldSkip(bSkip); 
};

void CueTrack::SetVolumes()
{
	for (int i = 0; i < LAYER_COUNT; i++)
	{
		for (int j = 0; j < MOOD_COUNT; j++)
		{
			float fVolume = ((j == GetGlobalMood()) ? 1.0f : 0.01f);
			GUID guid = GetGuid((CueLayer)i, (CueMood)j);
			if (guid && enginesound->IsSoundStillPlaying(guid))
				enginesound->SetVolumeByGuid(guid, fVolume);
		}
	}
}


CueMood CueTrack::GetGlobalMood()
{
	return pTFCueBuilder->GetMood();
}

void CueTrack::Play()
{
	if (GetCurrentSeqID() >= GetSeqCount())
	{
		SetCurrentSeqID(-1);
		return;
	}	
		
	bool bPlaying = IsStillPlaying();
	if (!bPlaying)
	{
		CueSequence Part = GetSequenceInfo(GetCurrentSeqID());
		DevMsg("Playing part %s\n", Part.sName);

		for (int i = 0; i < LAYER_COUNT; i++)
		{
			for (int j = 0; j < MOOD_COUNT; j++)
			{
				PlayLayer(GetCurrentSeqID(), (CueLayer)i, (CueMood)j);
			}
		}
	}
	SetVolumes();
}

void CueTrack::Stop()
{
	for (int i = 0; i < LAYER_COUNT; i++)
	{
		for (int j = 0; j < MOOD_COUNT; j++)
		{
			enginesound->StopSoundByGuid(GetGuid((CueLayer)i, (CueMood)j));
		}
	}
}

void CueTrack::StopPlaying()
{
	m_bPlay = false;
	if (!IsStillPlaying())
		return;
	Stop();
}

void CueTrack::StartPlaying()
{
	m_bPlay = true;
	//Play();
}

bool CueTrack::IsStillPlaying()
{
	for (int j = 0; j < MOOD_COUNT; j++)
	{
		if (enginesound->IsSoundStillPlaying(GetGuid(LAYER_MAIN, (CueMood)j)))
			return true;
	}
	return false;
}

void CueTrack::SetGuid(GUID guid, CueLayer Layer, CueMood Mood)
{
	GUID id = (Layer * MOOD_COUNT) + Mood;
	m_pPlayList[id] = guid;
}

GUID CueTrack::GetGuid(CueLayer Layer, CueMood Mood)
{
	GUID id = (Layer * MOOD_COUNT) + Mood;
	return m_pPlayList[id];
}