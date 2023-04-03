// MQ2Targets.cpp : Defines the entry point for the DLL application.
//
// PLUGIN_API is only to be used for callbacks.  All existing callbacks at this time
// are shown below. Remove the ones your plugin does not use.  Always use Initialize
// and Shutdown for setup and cleanup, do NOT do it in DllMain.

/* History:
1.40 12/31/06 - Initial by DrunkDwarf with htw mods
1.41 01/01/06 - Added watch debugging to debugspew for T/S (/watch debug to toggle)
1.42 01/01/06 - Fixed target load/search on plugin load, if you're already in game.
1.43 01/01/06 - Removed redundant check for guildid, preventing erasure of a guild entry in .ini if char is unguilded.
x.xx -------- - Various code over the last couple years added by htw, spawn/despawn customizations, sounds, etc.
*/

#include <mq/Plugin.h>

#include <list>
#include <vector>

#include <direct.h>                 // getcwd
#include <mmsystem.h>               // PlaySound
#pragma comment(lib, "Winmm.lib")   // PlaySound lib


#define MakeUpper(yourstring) transform (yourstring.begin(),yourstring.end(), yourstring.begin(), toupper);

//some defaults
#define NUMTARGETS          5   // display closest <x> targets to user
#define HUDXSTART           165
#define HUDYSTART           5
#define HUDYINCREMENT       12
#define FONTSIZE            2
#define HUDCOLOR            0xFFFFEA08   // yellow
#define GUILDHUDCOLOR       0xFFFF00FF   // purple
#define DEADHUDCOLOR        0xFF888888   // gray
#define TARGETHUDCOLOR      0xFFFF0000   // red
#define COLORCA				0x00
#define POPUPCOLORADD		0x0E
#define CHATCOLORADD		0x009933
#define POPUPCOLORREM		0x0D
#define CHATCOLORREM		0xCC0033
#define TARGETHUDSTRING      "&clr${Target.CleanName} ${Target.Level} ${Target.Class.ShortName}  ${Target.Distance}&arr(${Target.HeadingTo})"
#define NOTIFYHUDSTRING      "${Target.CleanName}(${Target.Level} ${Target.Class.ShortName})"
#define NOTIFYCHATSTRING      "${Target.CleanName} (${Target.Level} ${Target.Class.ShortName}) ${If[${Target.Guild.NotEqual[\"NULL\"]},in ${Target.Guild},]}${If[${Target.Guild.NotEqual[\"NULL\"]},${If[${Target.GuildStatus.NotEqual[\"member\"]}, (${Target.GuildStatus}),]},]}"

#define POPUPINTERVAL       2         // interval to show up any popup text in seconds
#define SECONDINTERVAL      1         // 1 second interval

PreSetup("MQ2Targets");
PLUGIN_VERSION(3.24);

using namespace std;

enum SORTTYPE { SORT_PRIORITY = 0, SORT_DISTANCE = 1, SORT_LEVEL = 2, SORT_NAME = 3, SORT_NONE = 4 };
SORTTYPE SortType = SORT_DISTANCE;
SORTTYPE nsSortType = SORT_NONE;
enum SORTORDER { SORT_NORMAL = 0, SORT_REVERSE = 1 };
SORTORDER SortOrder = SORT_NORMAL;
SORTORDER nsSortOrder = SORT_REVERSE;

const char* SortTypeNames[] = { "priority", "distance", "level", "name", "none", "" };
const char* SortOrderNames[] = { "normal", "reverse", "" };

int ColorOption[] = { 1, 2, 4, 5, 7, 13, 14, 15, 16, 18, -1 };
const char* ColorName[] = { "gray", "green", "blue", "purple", "white", "red", "lightgreen", "yellow", "lightblue", "cyan", "" };

struct SearchStringRecord
{
	std::string SearchString;
	bool bNotify;
	bool bHUD;
	int nSound;
	int nIndex;
	int Priority;
};

struct NotifyRecord
{
	uint32_t SpawnID;
	std::string DisplayedName;

	bool bNotified;
	int nSound;
	int Priority;
};

const char* szDirection[] = {
	"-^-",   //0
	"<--",   //1
	"-->"    //2
};

list<SearchStringRecord>       g_SearchStrings;    // list of spawns to watch for
list<NotifyRecord>        g_NotifySpawns;     // list of items to notify user of

struct TargetEntryInt
{
	SPAWNINFO* pSpawn;
	int        Int;
};
struct TargetEntryFloat
{
	SPAWNINFO* pSpawn;
	float      Float;
};

std::vector<TargetEntryFloat>  g_Targets;          // keeps track of targets matching search strings (spawninfo/dist)
std::vector<TargetEntryInt>    g_nsTargets;        // keeps track of targets matching search strings (spawninfo/dist)

size_t g_numWatchedTargets = NUMTARGETS;   // how many targets to track and display.  All targets
// are tracked, but only the <numWatchedTargets> closest are
// shown to the user
unsigned int g_HUDXStart = HUDXSTART;
unsigned int g_HUDYStart = HUDYSTART;
unsigned int g_HUDYIncrement = HUDYINCREMENT;
unsigned int g_HUDColor = HUDCOLOR;
unsigned int g_PopupColorAdd = POPUPCOLORADD;
unsigned int g_ChatColorAdd = CHATCOLORADD;
unsigned int g_PopupColorRem = POPUPCOLORREM;
unsigned int g_ChatColorRem = CHATCOLORREM;
bool g_UseConColor = true;
bool gVerbose = false;
unsigned int g_GuildHUDColor = GUILDHUDCOLOR;
unsigned int g_DeadHUDColor = DEADHUDCOLOR;
unsigned int g_TargetHUDColor = TARGETHUDCOLOR;
CHAR g_HUDString[MAX_STRING] = { 0 };
CHAR g_NotifyHUDString[MAX_STRING] = { 0 };
CHAR g_NotifyChatString[MAX_STRING] = { 0 };
struct NotificationsStruct {
	string notifyText;
	int spawnID;
};
vector<NotificationsStruct> Notifications;

bool DisplayEnabled = true;
bool DEBUGGING = false;
bool g_CheckForTargets = false;   // during some times (zoning, etc) we don't want to be checking
// for targets (just a waste).  This global lets us toggle this
bool g_bReadyToSearch = false;
bool g_UseMQ2Chat = true;
unsigned int g_mp3Length = 3000;  // 3 seconds
unsigned int g_wavLength = 0;     // entire file
bool g_useTimeStamp = false;
bool g_useChatReport = true;
bool g_useAllZone = true;
bool AlreadyShown = false;
bool bBGUpdate = true;
CHAR g_TimeStampFormat[MAX_STRING] = { 0 };
DWORD nFontSize = FONTSIZE;
bool bEQHasFocus = true;
time_t g_timerSeconds;
time_t g_timerOneSecond;

CRITICAL_SECTION g_csTargetSection;

// prototypes
// ini file reading/writing
void LoadPluginSettings();
void SavePluginSettings();
void LoadHUDString();
void LoadChatString();
// ini file targets
void ReadTargetsFromINI(PCHAR zone);
void DumpTargetsToINI(PCHAR zone);
// memory
void LoadZoneTargets();
void ClearTargets();
BOOL AddToTargetList(PCHAR targetName, bool bNotify, int nSound, int Priority, bool bHUD, bool bVerbose = false);
BOOL RemoveFromTargetList(PCHAR targetName, bool bNotify);
// command handlers
void WatchHandler(PSPAWNINFO pChar, PCHAR szLine);
void WatchList(PCHAR zone);
void WatchAdd(PCHAR zone, PCHAR targetName, int Priority, bool bNotify, int Sound, bool bHUD);
void WatchRemove(PCHAR zone, PCHAR targetName, bool bNotify = false);
void WatchTargets(PCHAR pTargets);
void WatchXStart(PCHAR pXStart);
void WatchYStart(PCHAR pYStart);
void WatchYIncrement(PCHAR pYIncrement);
void WatchFont(PCHAR nSize);
void WatchColor(PCHAR HUDType, PCHAR HUDColor);
void WatchTime(PCHAR pLine);
void WatchHUD(PCHAR pLine);
void WatchNotifyHUD(PCHAR pLine);
void WatchChatHUD(PCHAR pLine);
void WatchSound(PCHAR pZone, PCHAR pLine);
void WatchUsage(bool bHelp = false);
// spam to window
void WriteToChat(PCHAR szText, DWORD Color = USERCOLOR_DEFAULT);

BOOL InGame();
// target display
void StartSearchingForTargets();
void CheckForTargets(bool bState);
int CheckTargets();
int FindMatchingSpawns(MQSpawnSearch* pSearchSpawn, PSPAWNINFO pOrigin, bool bNotify = false, int nSound = 0, int Priority = 0, bool bHUD = true);
void CleanHUDTargets();
// sorting
static bool pMQRankCompare(const TargetEntryFloat& A, const TargetEntryFloat& B);
static bool pMQRankCompareNS(const TargetEntryInt& A, const TargetEntryInt& B);
// popup text on screen for any targets which need notifying
int AddToNotify(PSPAWNINFO pSpawn, int nSound = 0, int Priority = 0);
void RemoveFromNotify(uint32_t SpawnID, const char* DisplayedName, bool bPopup = false);
void RemoveNotifySpawns(PCHAR searchString);
void PopupNotifyTarget();
void DisplayHUDTarget(TargetEntryFloat& targInfo, DWORD X, DWORD Y, DWORD color);
// notify sound functions
void PlayNotifySound(PCHAR pFileName);
void PlayNotifySound(int nSoundID);
void StopNotifySound();
void AssignSound(PCHAR pLine);
void ListSounds();
DWORD GetColor(char* TColor, BYTE CA = 0xFF);

class MQ2TargetsType* pTargetsType = 0;
class MQ2TargetsType : public MQ2Type
{
public:
	enum TargetsMembers
	{
		Spawn = 1,
		Priority = 2,
		Count = 3,
	};

	MQ2TargetsType() :MQ2Type("Targ")
	{
		TypeMember(Spawn);
		TypeMember(Priority);
		TypeMember(Count);
	}

	virtual bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override
	{
		using namespace mq::datatypes;
		MQTypeMember* pMember = MQ2TargetsType::FindMember(Member);
		if (!pMember)
			return false;
		switch ((TargetsMembers)pMember->ID)
		{
		case Spawn:
			if (Index[0]) {
				if (IsNumber(Index)) {
					DWORD xCount = atoi(Index) - 1;
					if (xCount >= 0 && xCount < g_nsTargets.size()) {
						if (Dest.Ptr = g_nsTargets[xCount].pSpawn) {
							Dest.Type = pSpawnType;
							return true;
						}
					}
				}
				else {
					BOOL bExact = FALSE;
					PCHAR pName = Index;
					if (*pName == '=') {
						bExact = TRUE;
						pName++;
					}
					_strlwr_s(pName, MAX_STRING);
					CHAR Temp[MAX_STRING] = { 0 };
					for (unsigned long xCount = 0; xCount < g_nsTargets.size(); xCount++) {
						if (bExact) {
							if (g_nsTargets[xCount].pSpawn && !_stricmp(pName, g_nsTargets[xCount].pSpawn->DisplayedName)) {
								Dest.Ptr = g_nsTargets[xCount].pSpawn;
								Dest.Type = pSpawnType;
								return true;
							}
						}
						else {
							strcpy_s(Temp, g_nsTargets[xCount].pSpawn->DisplayedName);
							_strlwr_s(Temp);
							if (g_nsTargets[xCount].pSpawn && strstr(Temp, pName)) {
								Dest.Ptr = g_nsTargets[xCount].pSpawn;
								Dest.Type = pSpawnType;
								return true;
							}
						}
					}
				}
			}
			return false;
		case Priority:
			if (Index[0]) {
				if (IsNumber(Index)) {
					DWORD xCount = atoi(Index) - 1;
					if (xCount >= 0 && xCount < g_nsTargets.size()) {
						if (Dest.Ptr = g_nsTargets[xCount].pSpawn) {
							Dest.Type = pIntType;
							Dest.Int = g_nsTargets[xCount].Int;
							return true;
						}
					}
				}
				else {
					BOOL bExact = FALSE;
					PCHAR pName = Index;
					if (*pName == '=') {
						bExact = TRUE;
						pName++;
					}
					_strlwr_s(pName, MAX_STRING);
					CHAR Temp[MAX_STRING] = { 0 };
					for (unsigned long xCount = 0; xCount < g_nsTargets.size(); xCount++) {
						if (bExact) {
							if (g_nsTargets[xCount].pSpawn && !_stricmp(pName, g_nsTargets[xCount].pSpawn->DisplayedName)) {
								Dest.Type = pIntType;
								Dest.Int = g_nsTargets[xCount].Int;
								return true;
							}
						}
						else {
							strcpy_s(Temp, g_nsTargets[xCount].pSpawn->DisplayedName);
							_strlwr_s(Temp);
							if (g_nsTargets[xCount].pSpawn && strstr(Temp, pName)) {
								Dest.Type = pIntType;
								Dest.Int = g_nsTargets[xCount].Int;
								return true;
							}
						}
					}
				}
			}
			Dest.Type = pIntType;
			Dest.Int = -1;
			return true;
		case Count:
			Dest.Int = (int32_t)g_nsTargets.size();
			Dest.Type = pIntType;
			return true;
		}
		return false;
	}

	virtual bool ToString(MQVarPtr VarPtr, char* Destination) override
	{
		strcpy_s(Destination, MAX_STRING, "TRUE");
		return true;
	}
};

bool dataTargets(const char* szName, MQTypeVar& Dest)
{
	Dest.DWord = 1;
	Dest.Type = pTargetsType;
	return true;
}

void DebugChat(PCHAR szFormat, ...)
{
	if (!DEBUGGING)
		return;
	CHAR szOutput[MAX_STRING] = { 0 };
	va_list vaList;
	va_start(vaList, szFormat);
	vsprintf_s(szOutput, szFormat, vaList);
	WriteChatColor(szOutput);
}

// Called once, when the plugin is to initialize
PLUGIN_API void InitializePlugin()
{
	char szTemp[MAX_STRING] = { 0 };
	DebugSpewAlways("Initializing MQ2Targets");
	if (GetPrivateProfileString("Settings", "Verbose", NULL, szTemp, MAX_STRING, INIFileName)) {
		if (!_stricmp(szTemp, "yes") || !_stricmp(szTemp, "on"))
			gVerbose = true;
		else
			gVerbose = false;
	}
	else {
		gVerbose = false;
		WritePrivateProfileString("Settings", "Verbose", gVerbose ? "yes" : "no", INIFileName);
	}

	SortType = SORT_DISTANCE;
	if (GetPrivateProfileString("Settings", "HUDSortType", NULL, szTemp, MAX_STRING, INIFileName)) {
		int Index = 0;
		while (SortTypeNames[Index][0]) {
			if (!_stricmp(szTemp, SortTypeNames[Index]))
				SortType = (SORTTYPE)Index;
			Index++;
		}
	}
	else
		WritePrivateProfileString("Settings", "HUDSortType", SortTypeNames[(int)SortType], INIFileName);
	SortOrder = SORT_NORMAL;
	if (GetPrivateProfileString("Settings", "HUDSortOrder", NULL, szTemp, MAX_STRING, INIFileName)) {
		int Index = 0;
		while (SortOrderNames[Index][0]) {
			if (!_stricmp(szTemp, SortOrderNames[Index]))
				SortOrder = (SORTORDER)Index;
			Index++;
		}
	}
	else
		WritePrivateProfileString("Settings", "HUDSortOrder", SortOrderNames[(int)SortOrder], INIFileName);

	nsSortType = SORT_PRIORITY;
	if (GetPrivateProfileString("Settings", "SortType", NULL, szTemp, MAX_STRING, INIFileName)) {
		int Index = 0;
		while (SortTypeNames[Index][0]) {
			if (!_stricmp(szTemp, SortTypeNames[Index]))
				nsSortType = (SORTTYPE)Index;
			Index++;
		}
	}
	else
		WritePrivateProfileString("Settings", "SortType", SortTypeNames[(int)nsSortType], INIFileName);
	nsSortOrder = SORT_NORMAL;
	if (GetPrivateProfileString("Settings", "SortOrder", NULL, szTemp, MAX_STRING, INIFileName)) {
		int Index = 0;
		while (SortOrderNames[Index][0]) {
			if (!_stricmp(szTemp, SortOrderNames[Index]))
				nsSortOrder = (SORTORDER)Index;
			Index++;
		}
	}
	else
		WritePrivateProfileString("Settings", "SortOrder", SortOrderNames[(int)nsSortOrder], INIFileName);

	DisplayEnabled = true;
	if (GetPrivateProfileString("Settings", "DisplayHUDElements", NULL, szTemp, MAX_STRING, INIFileName)) {
		if (!_stricmp(szTemp, "yes") || !_stricmp(szTemp, "on"))
			DisplayEnabled = true;
		else
			DisplayEnabled = false;
	}
	else
		WritePrivateProfileString("Settings", "DisplayHUDElements", DisplayEnabled ? "on" : "off", INIFileName);

	Notifications.clear();
	InitializeCriticalSection(&g_csTargetSection);

	StopNotifySound();

	CheckForTargets(false);

	LoadPluginSettings();

	if (gGameState == GAMESTATE_INGAME)
	{
		StartSearchingForTargets();
	}
	AddCommand("/watch", WatchHandler);
	AddMQ2Data("Targ", dataTargets);
	pTargetsType = new MQ2TargetsType;
}

// Called once, when the plugin is to shutdown
PLUGIN_API void ShutdownPlugin()
{
	DebugSpewAlways("Shutting down MQ2Targets");
	AlreadyShown = true;
	SavePluginSettings();

	CleanHUDTargets();

	EnterCriticalSection(&g_csTargetSection);
	DeleteCriticalSection(&g_csTargetSection);

	StopNotifySound();
	RemoveCommand("/watch");
	RemoveMQ2Data("Targ");
	delete pTargetsType;
}

PLUGIN_API void OnBeginZone()
{
	DebugSpewAlways("MQ2Targets::OnBeginZone()");
	Notifications.clear();
	g_bReadyToSearch = false;

	// get rid of current items on the HUD
	CleanHUDTargets();

	CheckForTargets(false);
}

PLUGIN_API void OnEndZone()
{
	g_bReadyToSearch = true;
}

// Called every frame that the "HUD" is drawn -- e.g. net status / packet loss bar
PLUGIN_API void OnDrawHUD()
{
	if (!InGame())
		return;

	int i = 0;

	if (!pLocalPC)
		return;
	PSPAWNINFO me = pLocalPlayer;
	if (!me)
		return;

	// do this at least once a second, even without spawns adding or removing
	if (time(NULL) > (g_timerOneSecond + SECONDINTERVAL))
	{
		CleanHUDTargets();
		CheckTargets();
		g_timerOneSecond = time(NULL);
	}

	size_t nFound = g_Targets.size();
	for (size_t N = 0; N < nFound; N++)
	{
		PSPAWNINFO theTarget = g_Targets[N].pSpawn;
		if (theTarget) {
			if (SortType == SORT_LEVEL)
				g_Targets[N].Float = (float)theTarget->Level;
			else if (SortType == SORT_DISTANCE)
				g_Targets[N].Float = GetDistance(me, theTarget);
			else if (SortType == SORT_PRIORITY) {
				int lPriority = 0;
				for (size_t X = 0; X < g_nsTargets.size(); X++)
				{
					if (!g_nsTargets[X].pSpawn)
						continue;
					if (g_nsTargets[X].pSpawn->SpawnID == theTarget->SpawnID)
						lPriority = g_nsTargets[X].Int;
				}
				g_Targets[N].Float = (float)lPriority;
			}
			else
				g_Targets[N].Float = 0.0;
		}
	}
	if (SortOrder != SORT_NONE && !g_Targets.empty())
		std::sort(g_Targets.begin(), g_Targets.end(), pMQRankCompare);

	if (nsSortOrder != SORT_NONE && !g_nsTargets.empty())
		std::sort(g_nsTargets.begin(), g_nsTargets.end(), pMQRankCompareNS);

	// now draw the targets until the desired number is found
	if (DisplayEnabled)
	{
		if (nFound > 0)
		{
			CHAR szHUD[MAX_STRING] = { 0 };

			for (size_t N = 0; N < std::min(nFound, g_numWatchedTargets); N++)
			{
				PSPAWNINFO theTarget = g_Targets[N].pSpawn;
				if (theTarget)
				{
					DWORD SX = 0;
					DWORD SY = 0;
					if (pScreenX && pScreenY)
					{
						SX = ScreenX;
						SY = ScreenY;
					}
					// where on the screen to show the targets
					DWORD X, Y;
					X = SX + g_HUDXStart;
					Y = SX + (g_HUDYStart + (g_HUDYIncrement * i++));

					unsigned int color = g_HUDColor;
					if (g_UseConColor)
						color = ConColorToARGB(ConColor(theTarget));

					DisplayHUDTarget(g_Targets[N], X, Y, color);
				}
			}
		}
	}
	// if at least n seconds have elapsed since the last timer, do the next pending popup
	if (time(NULL) > (g_timerSeconds + POPUPINTERVAL))
	{
		// reset "timer"
		//DebugSpewAlways("MQ2Targets::OnDrawHUD() - Popping up any Notify Targets");
		g_timerSeconds = time(NULL);
		PopupNotifyTarget();
	}
}

void DisplayHUDTarget(TargetEntryFloat& targInfo, DWORD X, DWORD Y, DWORD color)
{
	static int NSkip = 0;
	if (!InGame())
		return;

	if (++NSkip > 10) {
		NSkip = 0;
		if (!bBGUpdate && !gbInForeground)
			bEQHasFocus = false;
		else
			bEQHasFocus = true;
	}
	if (!bEQHasFocus) return;

	PSPAWNINFO theTarget = targInfo.pSpawn;
	if (theTarget)
	{
		char outText[MAX_STRING];
		string result = g_HUDString;

		// figure out heading
		float HeadingTo = (float)(atan2f(pLocalPlayer->Y - theTarget->Y, theTarget->X - pLocalPlayer->X) * 180.0f / PI + 90.0f);
		if (HeadingTo < 0.0f)
			HeadingTo += 360.0f;
		else if (HeadingTo >= 360.0f)
			HeadingTo -= 360.0f;

		// replace &arr with direction arrow
		if (find_substr(result, "&arr") != -1)
		{
			float MyHeading = pLocalPlayer->Heading * 0.703125f;

			int arrow;
			if (((((int)MyHeading - (int)HeadingTo) + 375) % 360) < 30)
			{
				arrow = 0;   // straight ahead (+-30degrees)
			}
			else if (((((int)MyHeading - (int)HeadingTo) + 360) % 360) < 180)
			{
				arrow = 2;   // to the left
			}
			else
				arrow = 1;
			result = replace(result, "&arr", szDirection[arrow]);
		}

		// replace &dst with distance
		if (find_substr(result, "&dst") != -1)
		{
			CHAR szDist[MAX_STRING] = { 0 };
			if (pLocalPlayer->Z - theTarget->Z > 10)
			{
				sprintf_s(szDist, "-%.1f", targInfo.Float);
			}
			else if (theTarget->Z - pLocalPlayer->Z > 10)
			{
				sprintf_s(szDist, "+%.1f", targInfo.Float);
			}
			else
				sprintf_s(szDist, "%.1f", targInfo.Float);
			result = replace(result, "&dst", szDist);
		}

		// replace &clr with target indicators if targeted, also use target colors if &high is found
		bool bTarget = false;
		bool bHighLight = false;
		if (find_substr(result, "&clr") != -1)
		{
			// color of the HUD display
			// Yellow
			//         ARGBCOLOR Color;
			//         Color.A = 0xFF;
			//         Color.R = 255;
			//         Color.G = 234;
			//         Color.B = 8;

			// if we want to highlight/color stuff
			bHighLight = true;
			// highlight targeted spawn
			CHAR szBrackets[MAX_STRING] = { 0 };
			if (pTarget && pTarget->SpawnID == theTarget->SpawnID)
			{
				strcpy_s(szBrackets, ">>");
				bTarget = true;
			}
			result = replace(result, "&clr", szBrackets);
		}

		if (true == bHighLight)
		{
			if (pLocalPlayer->GuildID != -1 && theTarget->GuildID == pLocalPlayer->GuildID)
				color = g_GuildHUDColor;
			if (true == bTarget)
			{
				color = g_TargetHUDColor;   // red
				X -= 12;
			}
			if (theTarget->StandState == STANDSTATE_DEAD)
				color = g_DeadHUDColor;
		}

		// replace "${Target." with "${Spawn[<spawnid>]."
		CHAR szSpawnID[20] = { 0 };
		sprintf_s(szSpawnID, "${Spawn[%d].", theTarget->SpawnID);
		if (find_substr(result, "${Target.") != -1)
		{
			result = replace(result, "${Target.", szSpawnID);
		}

		// now parse like MQ2HUD ini string
		strcpy_s(outText, result.c_str());
		ParseMacroParameter(pLocalPlayer, outText);
		if (outText[0] && strcmp(outText, "NULL"))
		{
			DrawHUDText(outText, X, Y, color, nFontSize);
		}
	}
}

// Called once directly after initialization, and then every time the gamestate changes
PLUGIN_API void SetGameState(DWORD GameState)
{
	DebugSpewAlways("MQ2Targets::SetGameState (%d)", GameState);

	if (GameState == GAMESTATE_INGAME && true == g_bReadyToSearch)
	{
		char szINIFileName[MAX_STRING] = { 0 }, szTemp[MAX_STRING] = { 0 };
		sprintf_s(szINIFileName, "%s\\%s_%s.ini", gPathConfig, GetServerShortName(), pLocalPC->Name);
		DisplayEnabled = true;
		if (GetPrivateProfileString(mqplugin::PluginName, "DisplayHUDElements", NULL, szTemp, MAX_STRING, szINIFileName)) {
			if (!_stricmp(szTemp, "yes") || !_stricmp(szTemp, "on"))
				DisplayEnabled = true;
			else
				DisplayEnabled = false;
		}
		else
			WritePrivateProfileString(mqplugin::PluginName, "DisplayHUDElements", DisplayEnabled ? "on" : "off", szINIFileName);
		StartSearchingForTargets();
	}
}

// This is called every time WriteChatColor is called by MQ2Main or any plugin,
// IGNORING FILTERS, IF YOU NEED THEM MAKE SURE TO IMPLEMENT THEM. IF YOU DONT
// CALL CEverQuest::dsp_chat MAKE SURE TO IMPLEMENT EVENTS HERE (for chat plugins)
PLUGIN_API DWORD OnWriteChatColor(PCHAR Line, DWORD Color, DWORD Filter)
{
	if (_stricmp(Line, "plugin 'MQ2targets' Loaded.") == 0)
	{
		//DebugSpewAlways("MQ2Targets::OnWriteChatColor(%s)", Line);
		g_bReadyToSearch = true;
		// use that function to simulate starting the game
		SetGameState(GAMESTATE_INGAME);
	}
	return 0;
}

// This is called each time a spawn is added to a zone (inserted into EQ's list of spawns),
// or for each existing spawn when a plugin first initializes
// NOTE: When you zone, these will come BEFORE OnZoned
PLUGIN_API void OnAddSpawn(PSPAWNINFO pNewSpawn)
{
	if (gGameState == GAMESTATE_INGAME && true == g_bReadyToSearch && pNewSpawn && pNewSpawn->SpawnID > 0 && pNewSpawn->DisplayedName && strlen(pNewSpawn->DisplayedName) > 0)
	{
		if (DEBUGGING)
			DebugSpewAlways("MQ2Targets::OnAddSpawn(%s)", pNewSpawn->DisplayedName);

		CleanHUDTargets();

		CheckTargets();
	}
}

// This is called each time a spawn is removed from a zone (removed from EQ's list of spawns).
// It is NOT called for each existing spawn when a plugin shuts down.
PLUGIN_API void OnRemoveSpawn(PSPAWNINFO pSpawn)
{
	if (gGameState == GAMESTATE_INGAME && true == g_bReadyToSearch && pSpawn && pSpawn->SpawnID > 0 && pSpawn->DisplayedName && strlen(pSpawn->DisplayedName) > 0)
	{
		if (DEBUGGING)
			DebugSpewAlways("MQ2Targets::OnRemoveSpawn(%s)", pSpawn->DisplayedName);

		CleanHUDTargets();

		CheckTargets();

		// if it's in the notify list, update its text
		if (pSpawn) {
			RemoveFromNotify(pSpawn->SpawnID, pSpawn->DisplayedName, true);
		}
	}
}

// overall settings for Plugin
void LoadPluginSettings()
{
	char szTemp[MAX_STRING] = { 0 }, szDefault[MAX_STRING] = { 0 };
	ARGBCOLOR RGB;
	DebugSpewAlways("MQ2Targets::LoadPluginSettings()");

	g_numWatchedTargets = GetPrivateProfileInt("Settings", "NumDisplayed", NUMTARGETS, INIFileName);
	nFontSize = GetPrivateProfileInt("Settings", "HUDFontSize", FONTSIZE, INIFileName);
	g_HUDXStart = GetPrivateProfileInt("Settings", "HUDXStart", HUDXSTART, INIFileName);
	g_HUDYStart = GetPrivateProfileInt("Settings", "HUDYStart", HUDYSTART, INIFileName);
	g_HUDYIncrement = GetPrivateProfileInt("Settings", "HUDYIncrement", HUDYINCREMENT, INIFileName);

	sprintf_s(szDefault, "%08X", HUDCOLOR);
	GetPrivateProfileString("Settings", "HUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	if (strlen(szTemp) > 6) {
		g_HUDColor = GetPrivateProfileInt("Settings", "HUDColor", HUDCOLOR, INIFileName);
		RGB.ARGB = g_HUDColor;
		sprintf_s(szTemp, "%02X%02X%02X", RGB.R, RGB.G, RGB.B);
		WritePrivateProfileString("Settings", "HUDColor", szTemp, INIFileName);
	}
	GetPrivateProfileString("Settings", "HUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	g_HUDColor = GetColor(szTemp);

	sprintf_s(szDefault, "%08X", GUILDHUDCOLOR);
	GetPrivateProfileString("Settings", "GuildHUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	if (strlen(szTemp) > 6) {
		g_GuildHUDColor = GetPrivateProfileInt("Settings", "GuildHUDColor", GUILDHUDCOLOR, INIFileName);
		RGB.ARGB = g_GuildHUDColor;
		sprintf_s(szTemp, "%02X%02X%02X", RGB.R, RGB.G, RGB.B);
		WritePrivateProfileString("Settings", "GuildHUDColor", szTemp, INIFileName);
	}
	GetPrivateProfileString("Settings", "GuildHUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	g_GuildHUDColor = GetColor(szTemp);

	sprintf_s(szDefault, "%08X", DEADHUDCOLOR);
	GetPrivateProfileString("Settings", "DeadHUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	if (strlen(szTemp) > 6) {
		g_DeadHUDColor = GetPrivateProfileInt("Settings", "DeadHUDColor", DEADHUDCOLOR, INIFileName);
		RGB.ARGB = g_DeadHUDColor;
		sprintf_s(szTemp, "%02X%02X%02X", RGB.R, RGB.G, RGB.B);
		WritePrivateProfileString("Settings", "DeadHUDColor", szTemp, INIFileName);
	}
	GetPrivateProfileString("Settings", "DeadHUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	g_DeadHUDColor = GetColor(szTemp);

	sprintf_s(szDefault, "%08X", TARGETHUDCOLOR);
	GetPrivateProfileString("Settings", "TargetHUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	if (strlen(szTemp) > 6) {
		g_TargetHUDColor = GetPrivateProfileInt("Settings", "TargetHUDColor", TARGETHUDCOLOR, INIFileName);
		RGB.ARGB = g_TargetHUDColor;
		sprintf_s(szTemp, "%02X%02X%02X", RGB.R, RGB.G, RGB.B);
		WritePrivateProfileString("Settings", "TargetHUDColor", szTemp, INIFileName);
	}
	GetPrivateProfileString("Settings", "TargetHUDColor", szDefault, szTemp, MAX_STRING, INIFileName);
	g_TargetHUDColor = GetColor(szTemp);

	int x;
	char szColor[MAX_STRING] = { 0 };

	strcpy_s(szColor, "lightgreen");
	for (x = 0; ColorOption[x] > -1; x++)
		if (ColorOption[x] == POPUPCOLORADD)
			strcpy_s(szColor, ColorName[x]);
	GetPrivateProfileString("Settings", "PopupColorAdd", szColor, szTemp, MAX_STRING, INIFileName);
	for (x = 0; ColorOption[x] > -1; x++)
		if (!_stricmp(szTemp, ColorName[x])) {
			strcpy_s(szColor, ColorName[x]);
			g_PopupColorAdd = ColorOption[x];
		}
	WritePrivateProfileString("Settings", "PopupColorAdd", szColor, INIFileName);

	strcpy_s(szColor, "red");
	for (x = 0; ColorOption[x] > -1; x++)
		if (ColorOption[x] == POPUPCOLORREM)
			strcpy_s(szColor, ColorName[x]);
	GetPrivateProfileString("Settings", "PopupColorRem", szColor, szTemp, MAX_STRING, INIFileName);
	for (x = 0; ColorOption[x] > -1; x++)
		if (!_stricmp(szTemp, ColorName[x])) {
			strcpy_s(szColor, ColorName[x]);
			g_PopupColorRem = ColorOption[x];
		}
	WritePrivateProfileString("Settings", "PopupColorRem", szColor, INIFileName);

	sprintf_s(szDefault, "%08X", CHATCOLORADD);
	GetPrivateProfileString("Settings", "ChatColorAdd", szDefault, szTemp, MAX_STRING, INIFileName);
	if (strlen(szTemp) > 6) {
		g_ChatColorAdd = GetPrivateProfileInt("Settings", "ChatColorAdd", CHATCOLORADD, INIFileName);
		RGB.ARGB = g_ChatColorAdd;
		sprintf_s(szTemp, "%02X%02X%02X", RGB.R, RGB.G, RGB.B);
		WritePrivateProfileString("Settings", "ChatColorAdd", szTemp, INIFileName);
	}
	GetPrivateProfileString("Settings", "ChatColorAdd", szDefault, szTemp, MAX_STRING, INIFileName);
	g_ChatColorAdd = GetColor(szTemp, COLORCA);

	sprintf_s(szDefault, "%08X", CHATCOLORREM);
	GetPrivateProfileString("Settings", "ChatColorRem", szDefault, szTemp, MAX_STRING, INIFileName);
	if (strlen(szTemp) > 6) {
		g_ChatColorRem = GetPrivateProfileInt("Settings", "ChatColorRem", CHATCOLORREM, INIFileName);
		RGB.ARGB = g_ChatColorRem;
		sprintf_s(szTemp, "%02X%02X%02X", RGB.R, RGB.G, RGB.B);
		WritePrivateProfileString("Settings", "ChatColorRem", szTemp, INIFileName);
	}
	GetPrivateProfileString("Settings", "ChatColorRem", szDefault, szTemp, MAX_STRING, INIFileName);
	g_ChatColorRem = GetColor(szTemp, COLORCA);

	g_UseConColor = GetPrivateProfileInt("Settings", "UseConColor", true, INIFileName) & 0x01;
	g_UseMQ2Chat = GetPrivateProfileInt("Settings", "UseMQ2Chat", true, INIFileName) & 0x01;
	GetPrivateProfileString("Settings", "UpdateInBackground", "on", szTemp, MAX_STRING, INIFileName);
	bBGUpdate = _strnicmp(szTemp, "on", 2) ? false : true;
	g_mp3Length = GetPrivateProfileInt("Settings", "MP3Length", 3000, INIFileName);
	g_wavLength = GetPrivateProfileInt("Settings", "WavLength", 0, INIFileName);
	g_useTimeStamp = GetPrivateProfileInt("Settings", "UseTimeStamp", false, INIFileName) & 0x01;
	g_useChatReport = GetPrivateProfileInt("Settings", "UseChatReport", true, INIFileName) & 0x01;
	g_useAllZone = GetPrivateProfileInt("Settings", "UseAllZone", true, INIFileName) & 0x01;
	GetPrivateProfileString("Settings", "TimeStampFormat", "[%H:%M:%S]", g_TimeStampFormat, MAX_STRING, INIFileName);
	LoadHUDString();
	LoadChatString();
}

void SavePluginSettings()
{
	ARGBCOLOR Color;
	DebugSpewAlways("MQ2Targets::SavePluginSettings()");

	CHAR szTemp[MAX_STRING] = { 0 };
	// save Number of Targets to display
	sprintf_s(szTemp, "%d", (int)g_numWatchedTargets);
	WritePrivateProfileString("Settings", "NumDisplayed", szTemp, INIFileName);

	sprintf_s(szTemp, "%u", nFontSize);
	WritePrivateProfileString("Settings", "HUDFontSize", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_HUDXStart);
	WritePrivateProfileString("Settings", "HUDXStart", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_HUDYStart);
	WritePrivateProfileString("Settings", "HUDYStart", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_HUDYIncrement);
	WritePrivateProfileString("Settings", "HUDYIncrement", szTemp, INIFileName);

	Color.ARGB = g_HUDColor;
	sprintf_s(szTemp, "%02X%02X%02X", Color.R, Color.G, Color.B);
	WritePrivateProfileString("Settings", "HUDColor", szTemp, INIFileName);

	Color.ARGB = g_GuildHUDColor;
	sprintf_s(szTemp, "%02X%02X%02X", Color.R, Color.G, Color.B);
	WritePrivateProfileString("Settings", "GuildHUDColor", szTemp, INIFileName);

	Color.ARGB = g_DeadHUDColor;
	sprintf_s(szTemp, "%02X%02X%02X", Color.R, Color.G, Color.B);
	WritePrivateProfileString("Settings", "DeadHUDColor", szTemp, INIFileName);

	Color.ARGB = g_TargetHUDColor;
	sprintf_s(szTemp, "%02X%02X%02X", Color.R, Color.G, Color.B);
	WritePrivateProfileString("Settings", "TargetHUDColor", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_UseConColor ? 1 : 0);
	WritePrivateProfileString("Settings", "UseConColor", szTemp, INIFileName);

	sprintf_s(szTemp, "%s", g_HUDString);
	WritePrivateProfileString("Settings", "HUDString", szTemp, INIFileName);

	sprintf_s(szTemp, "%s", g_NotifyHUDString);
	WritePrivateProfileString("Settings", "NotifyHUDString", szTemp, INIFileName);

	sprintf_s(szTemp, "%s", g_NotifyChatString);
	WritePrivateProfileString("Settings", "NotifyChatString", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_UseMQ2Chat ? 1 : 0);
	WritePrivateProfileString("Settings", "UseMQ2Chat", szTemp, INIFileName);

	WritePrivateProfileString("Settings", "UpdateInBackground", bBGUpdate ? "on" : "off", INIFileName);

	sprintf_s(szTemp, "%u", g_mp3Length);
	WritePrivateProfileString("Settings", "MP3Length", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_wavLength);
	WritePrivateProfileString("Settings", "WavLength", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_useTimeStamp ? 1 : 0);
	WritePrivateProfileString("Settings", "UseTimeStamp", szTemp, INIFileName);

	sprintf_s(szTemp, "%s", g_TimeStampFormat);
	WritePrivateProfileString("Settings", "TimeStampFormat", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_useChatReport ? 1 : 0);
	WritePrivateProfileString("Settings", "UseChatReport", szTemp, INIFileName);

	sprintf_s(szTemp, "%d", g_useAllZone ? 1 : 0);
	WritePrivateProfileString("Settings", "UseAllZone", szTemp, INIFileName);

	char szColor[MAX_STRING] = { 0 };
	int x;

	strcpy_s(szColor, "lightgreen");
	for (x = 0; ColorOption[x] > -1; x++)
		if (ColorOption[x] == g_PopupColorAdd)
			strcpy_s(szColor, ColorName[x]);
	WritePrivateProfileString("Settings", "PopupColorAdd", szColor, INIFileName);

	strcpy_s(szColor, "red");
	for (x = 0; ColorOption[x] > -1; x++)
		if (ColorOption[x] == g_PopupColorRem)
			strcpy_s(szColor, ColorName[x]);
	WritePrivateProfileString("Settings", "PopupColorRem", szColor, INIFileName);

	Color.ARGB = g_ChatColorAdd;
	sprintf_s(szTemp, "%02X%02X%02X", Color.R, Color.G, Color.B);
	WritePrivateProfileString("Settings", "ChatColorAdd", szTemp, INIFileName);

	Color.ARGB = g_ChatColorRem;
	sprintf_s(szTemp, "%02X%02X%02X", Color.R, Color.G, Color.B);
	WritePrivateProfileString("Settings", "ChatColorRem", szTemp, INIFileName);
}

void LoadHUDString()
{
	GetPrivateProfileString("Settings", "HUDString", TARGETHUDSTRING, g_HUDString, MAX_STRING, INIFileName);

	GetPrivateProfileString("Settings", "NotifyHUDString", NOTIFYHUDSTRING, g_NotifyHUDString, MAX_STRING, INIFileName);
}

void LoadChatString()
{
	GetPrivateProfileString("Settings", "NotifyChatString", NOTIFYCHATSTRING, g_NotifyChatString, MAX_STRING, INIFileName);
}

// Target-related methods
void LoadZoneTargets()
{
	DWORD ZoneID;
	if (!InGame())
		return;
	ZoneID = pLocalPC->zoneId;
	if (ZoneID > MAX_ZONES)
		ZoneID &= 0x7FFF;
	if (ZoneID >= MAX_ZONES || ZoneID <= 0) {
		WriteChatf("\ar%s\aw::\ayBad zone ID: \ag%d", mqplugin::PluginName, ZoneID);
		return;
	}

	DebugSpewAlways("MQ2Targets::LoadZoneTargets()");

	CHAR zone[MAX_STRING] = ">unknown<";

	// clean out the old targets to watch for
	ClearTargets();

	if (pWorldData && pLocalPC)
	{
		// read new targets to watch for in ALL zones
		if (g_useAllZone)
			ReadTargetsFromINI("all");

		if (pWorldData)
			strcpy_s(zone, GetFullZone(pLocalPC->zoneId));
		// read new targets to watch for
		ReadTargetsFromINI(zone);
		// show the current watch list
		char szTemp[MAX_STRING] = { 0 };
		if (GetPrivateProfileString("Settings", "Verbose", NULL, szTemp, MAX_STRING, INIFileName)) {
			if (!_stricmp(szTemp, "yes") || !_stricmp(szTemp, "on"))
				gVerbose = true;
			else
				gVerbose = false;
		}
		else {
			gVerbose = false;
			WritePrivateProfileString("Settings", "Verbose", gVerbose ? "yes" : "no", INIFileName);
		}
		if (gVerbose)
			WatchList(zone);
	}
}

// dealing with targets in the INI file
void ReadTargetsFromINI(PCHAR zone)
{
	DebugSpewAlways("MQ2Targets::ReadTargetsFromINI(%s)", zone);

	CHAR szTemp[MAX_STRING];
	CHAR szBuffer[MAX_STRING];

	int i = 0;
	do
	{
		sprintf_s(szTemp, "spawn%d", i);
		DebugChat("MQ2Targets::ReadTargetsFromINI(%s): Trying to read '%s'", zone, szTemp);
		GetPrivateProfileString(zone, szTemp, "notfound", szBuffer, MAX_STRING, INIFileName);
		DebugChat("MQ2Targets::ReadTargetsFromINI(%s): Result is '%s'", zone, szBuffer);
		if (!strcmp(szBuffer, "notfound"))
			break;

		bool bNotify = false;
		int nSound = 0;
		int Priority = 0;
		bool bHUD = true;

		__try
		{
			CHAR seperator[] = "|";
			CHAR* token, * nt;
			// Establish string and get the first token:
			token = strtok_s(szBuffer, seperator, &nt);
			while (token != NULL)
			{
				// While there are tokens in "string"
				if (0 == _stricmp(token, "notify"))
					bNotify = true;
				else if (0 == _stricmp(token, "nohud"))
					bHUD = false;
				else if (0 == _stricmp(token, "sound"))
				{
					// Get next token (it'll be the sound ID)
					token = strtok_s(NULL, seperator, &nt);

					if (NULL != token && IsNumber(token))
					{
						nSound = atoi(token);
					}
					bNotify = true;
				}
				else if (0 == _stricmp(token, "priority"))
				{
					// Get next token (it'll be the priority)
					token = strtok_s(NULL, seperator, &nt);

					if (NULL != token && IsNumber(token))
					{
						Priority = atoi(token);
					}
				}

				// Get next token:
				token = strtok_s(NULL, seperator, &nt);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			DebugSpewAlways("MQ2Targets::ReadTargetsFromINI(%s): **strtok Exception**", zone);
		}

		AddToTargetList(szBuffer, bNotify, nSound, Priority, bHUD);
	} while (++i);
}

void DumpTargetsToINI(PCHAR zone)
{
	DebugSpewAlways("MQ2Targets::DumpTargetsToINI(%s)", zone);

	CHAR szTemp[MAX_STRING] = { 0 };
	// clean INI entries for entire zone
	WritePrivateProfileString(zone, NULL, NULL, INIFileName);
	// now rewrite INI entries for entire zone
	int i = 0;
	CHAR saveString[MAX_STRING] = { 0 };
	list<SearchStringRecord>::iterator pSearchStrings = g_SearchStrings.begin();
	while (pSearchStrings != g_SearchStrings.end())
	{
		sprintf_s(szTemp, "spawn%d", i++);
		strcpy_s(saveString, pSearchStrings->SearchString.c_str());
		if (true == pSearchStrings->bNotify)
		{
			strcat_s(saveString, "|notify");
		}
		if (!pSearchStrings->bHUD)
		{
			strcat_s(saveString, "|nohud");
		}
		if (0 != pSearchStrings->nSound)
		{
			CHAR soundString[MAX_STRING] = { 0 };
			sprintf_s(soundString, "|sound|%d", pSearchStrings->nSound);
			strcat_s(saveString, soundString);
		}
		if (0 != pSearchStrings->Priority)
		{
			CHAR priorityString[MAX_STRING] = { 0 };
			sprintf_s(priorityString, "|priority|%d", pSearchStrings->Priority);
			strcat_s(saveString, priorityString);
		}
		WritePrivateProfileString(zone, szTemp, saveString, INIFileName);
		pSearchStrings++;
	}
}

// dealing with Targets in memory
void ClearTargets()
{
	DebugSpewAlways("MQ2Targets::ClearTargets()");

	g_SearchStrings.clear();
	g_NotifySpawns.clear();
}

BOOL AddToTargetList(PCHAR targetName, bool bNotify, int nSound, int Priority, bool bHUD, bool bVerbose)
{
	DebugSpewAlways("MQ2Targets::AddToTargetList(%s, %s, %d)", targetName, bNotify ? "true" : "false", nSound);

	BOOL bReturn = FALSE;

	CHAR szMsg[MAX_STRING] = { 0 };
	list<SearchStringRecord>::iterator pSearchStrings = g_SearchStrings.begin();
	while (pSearchStrings != g_SearchStrings.end())
	{
		if (!strcmp(targetName, pSearchStrings->SearchString.c_str()))
		{
			sprintf_s(szMsg, "Already watching for \"%s\"", targetName);
			WriteToChat(szMsg, CONCOLOR_RED);
			return bReturn;
		}
		pSearchStrings++;
	}

	SearchStringRecord NewString;
	NewString.SearchString = targetName;
	NewString.bNotify = bNotify;
	NewString.nSound = nSound;
	NewString.Priority = Priority;
	NewString.bHUD = bHUD;
	g_SearchStrings.push_back(std::move(NewString));

	char szTemp[MAX_STRING] = { 0 };
	if (GetPrivateProfileString("Settings", "Verbose", NULL, szTemp, MAX_STRING, INIFileName)) {
		if (!_stricmp(szTemp, "yes") || !_stricmp(szTemp, "on"))
			gVerbose = true;
		else
			gVerbose = false;
	}
	else {
		gVerbose = false;
		WritePrivateProfileString("Settings", "Verbose", gVerbose ? "yes" : "no", INIFileName);
	}
	if (bVerbose || gVerbose) {
		sprintf_s(szMsg, "Watching for \"%s\"", targetName);
		WriteToChat(szMsg, CONCOLOR_YELLOW);
	}

	bReturn = TRUE;

	return bReturn;
}

BOOL RemoveFromTargetList(PCHAR SpawnName, bool bNotify)
{
	DebugSpewAlways("MQ2Targets::RemoveFromTargetList(%s, %s)", SpawnName, bNotify ? "true" : "false");

	CHAR szMsg[MAX_STRING] = { 0 };
	list<SearchStringRecord>::iterator pSearchStrings = g_SearchStrings.begin();
	while (pSearchStrings != g_SearchStrings.end())
	{
		if (!strcmp(SpawnName, pSearchStrings->SearchString.c_str()))
		{
			sprintf_s(szMsg, "No longer watching for \"%s\"", SpawnName);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			g_SearchStrings.erase(pSearchStrings);
			return TRUE;
		}
		pSearchStrings++;
	}
	sprintf_s(szMsg, "Cannot delete \"%s\", not found on watch list", SpawnName);
	WriteToChat(szMsg, CONCOLOR_RED);

	return FALSE;
}

// Command Handlers
void WatchHandler(PSPAWNINFO pChar, PCHAR szLine)
{
	CHAR szTemp[MAX_STRING] = { 0 }, szZone[MAX_STRING] = { 0 }, szArg1[MAX_STRING] = { 0 }, szArg2[MAX_STRING] = { 0 }, szArg3[MAX_STRING] = { 0 }, szArg4[MAX_STRING] = { 0 };
	DWORD ZoneID;

	if (!InGame())
		return;
	ZoneID = pLocalPC->zoneId;
	if (ZoneID > MAX_ZONES)
		ZoneID &= 0x7FFF;
	if (ZoneID >= MAX_ZONES || ZoneID <= 0) {
		WriteChatf("\ar%s\aw::\ayBad zone ID: \ag%d", mqplugin::PluginName, ZoneID);
		return;
	}

	if (!*szLine) {
		WatchUsage();
		return;
	}

	GetArg(szArg1, szLine, 1);

	if (!szArg1[0]) {
		WatchUsage();
		return;
	}

	strcpy_s(szArg2, GetNextArg(szLine));
	if (pWorldData)
		strcpy_s(szZone, GetFullZone(pLocalPC->zoneId));
	string lcArg1 = szArg1;
	MakeLower(lcArg1);
	string lcArg2 = szArg2;
	bool UseTarget = false;
	if (!lcArg2.length()) {
		if (pTarget) {
			lcArg2 = pTarget->DisplayedName;
			UseTarget = true;
		}
	}
	MakeLower(lcArg2);

	if (lcArg1 == "list" || lcArg1 == "l")
		WatchList(szZone);
	else if (lcArg1 == "sound" && lcArg2.length() > 0) {
		char szTemp[MAX_STRING] = { 0 };
		strcpy_s(szTemp, lcArg2.c_str());
		WatchSound(szZone, szTemp);
	}
	else if (lcArg1 == "add" && lcArg2.length() > 0) {
		szArg2[0] = 0;
		if (!UseTarget) {
			GetArg(szArg2, szLine, 2);
			strcpy_s(szArg3, GetNextArg(szLine));
		}
		else {
			strcpy_s(szArg2, lcArg2.c_str());
			szArg3[0] = 0;
		}
		bool bNotify = false, bHUD = true;
		int Sound = 0, Priority = 0;
		char szBuffer[MAX_STRING] = { 0 }, seperator[] = " ", * token, * nt;
		strcpy_s(szBuffer, szArg3);
		token = strtok_s(szBuffer, seperator, &nt);
		while (token != NULL)
		{
			if (!_stricmp(token, "notify"))
				bNotify = true;
			else if (!_stricmp(token, "nohud"))
				bHUD = false;
			else if (!_stricmp(token, "sound")) {
				token = strtok_s(NULL, seperator, &nt);
				if (token && IsNumber(token))
					Sound = atoi(token);
			}
			else if (!_stricmp(token, "priority")) {
				token = strtok_s(NULL, seperator, &nt);
				if (token && IsNumber(token))
					Priority = atoi(token);
			}
			token = strtok_s(NULL, seperator, &nt);
		}
		if (strstr(szArg3, "/all"))
			WatchAdd("all", szArg2, Priority, bNotify, Sound, bHUD);
		else
			WatchAdd(szZone, szArg2, Priority, bNotify, Sound, bHUD);
	}
	else if ((lcArg1 == "delete" || lcArg1 == "del" || lcArg1 == "remove" || lcArg1 == "rem") && (lcArg2.length() > 0 || UseTarget)) {
		szArg2[0] = 0;
		GetArg(szArg2, szLine, 2);
		strcpy_s(szArg3, GetNextArg(szLine));
		if (!szArg2[0] && UseTarget)
			strcpy_s(szArg2, lcArg2.c_str());
		if (strstr(szArg3, "/all"))
			WatchRemove("all", szArg2);
		else
			WatchRemove(szZone, szArg2);
	}
	else if (lcArg1 == "limit" && lcArg2.length() > 0 && IsNumber((PCHAR)lcArg2.c_str()))
		WatchTargets((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "x" && lcArg2.length() > 0 && IsNumber((PCHAR)lcArg2.c_str()))
		WatchXStart((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "y" && lcArg2.length() > 0 && IsNumber((PCHAR)lcArg2.c_str()))
		WatchYStart((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "increment" && lcArg2.length() > 0 && IsNumber((PCHAR)lcArg2.c_str()))
		WatchYIncrement((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "popuptest") {
		char szTmp[MAX_STRING] = { 0 }, szColor[MAX_STRING] = { 0 };
		int TmpColorA, TmpColorB;
		/*
		for(int z=0; ColorOption[z] > -1; z++)
			if(!_stricmp(ColorName[z], (PCHAR)lcArg2.c_str())) {
				strcpy_s(szColor, lcArg2.c_str());
				TmpColorA = ColorOption[z];
			}
		TmpColorB = TmpColorA & 0xFF;
		*/
		TmpColorA = atoi(lcArg2.c_str());
		TmpColorB = TmpColorA;
		strcpy_s(szColor, lcArg2.c_str());
		sprintf_s(szTmp, "This is a test of color %d (%s), using %d", TmpColorA, szColor, TmpColorB);
		DisplayOverlayText(szTmp, TmpColorB, 100, 1000, 1000, 3000);
	}
	else if (lcArg1 == "color") {
		GetArg(szArg3, szArg2, 1);
		GetArg(szArg4, szArg2, 2);
		if (!_stricmp(szArg3, "hud"))
			WatchColor("", szArg4);
		else if (!_stricmp(szArg3, "dead"))
			WatchColor("Dead", szArg4);
		else if (!_stricmp(szArg3, "guild"))
			WatchColor("Guild", szArg4);
		else if (!_stricmp(szArg3, "target"))
			WatchColor("Target", szArg4);
		else if (!_stricmp(szArg3, "chatadd"))
			WatchColor("ChatColorAdd", szArg4);
		else if (!_stricmp(szArg3, "chatrem"))
			WatchColor("ChatColorRem", szArg4);
		else if (!_stricmp(szArg3, "popupadd")) {
			for (int x = 0; ColorOption[x] > -1; x++) {
				if (!_stricmp(szArg4, ColorName[x])) {
					g_PopupColorAdd = ColorOption[x];
					WritePrivateProfileString("Settings", "PopupColorAdd", ColorName[x], INIFileName);
					sprintf_s(szTemp, "\atPopup Add Color now set to %s\ax", ColorName[x]);
					WriteToChat(szTemp, USERCOLOR_DEFAULT);
					return;
				}
			}
			char szTmp[MAX_STRING] = { 0 };
			WriteChatf("\ar%s\aw::\ayInvalid color option.  Choices are:", mqplugin::PluginName);
			for (int z = 0; ColorOption[z] > -1; z++) {
				sprintf_s(szTmp, " --> %s", ColorName[z]);
				WriteChatColor(szTmp, ColorOption[z]);
			}
		}
		else if (!_stricmp(szArg3, "popuprem")) {
			for (int x = 0; ColorOption[x] > -1; x++) {
				if (!_stricmp(szArg4, ColorName[x])) {
					g_PopupColorRem = ColorOption[x];
					WritePrivateProfileString("Settings", "PopupColorRem", ColorName[x], INIFileName);
					sprintf_s(szTemp, "\atPopup Remove Color now set to %s\ax", ColorName[x]);
					WriteToChat(szTemp, USERCOLOR_DEFAULT);
					return;
				}
			}
			char szTmp[MAX_STRING] = { 0 };
			WriteChatf("\ar%s\aw::\ayInvalid color option.  Choices are:", mqplugin::PluginName);
			for (int z = 0; ColorOption[z] > -1; z++) {
				sprintf_s(szTmp, " --> %s", ColorName[z]);
				WriteChatColor(szTmp, ColorOption[z]);
			}
		}
		else
			WatchUsage();
	}
	else if (lcArg1 == "font" && lcArg2.length() > 0 && IsNumber((PCHAR)lcArg2.c_str()))
		WatchFont((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "show")
	{
		DisplayEnabled = true;
		sprintf_s(szTemp, "\atMQ2Targets HUD elements now %s\ax", DisplayEnabled ? "\agSHOWN" : "\arHIDDEN");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		WritePrivateProfileString("Settings", "DisplayHUDElements", DisplayEnabled ? "on" : "off", INIFileName);
	}
	else if (lcArg1 == "hide")
	{
		DisplayEnabled = false;
		sprintf_s(szTemp, "\atMQ2Targets HUD elements now %s\ax", DisplayEnabled ? "\agSHOWN" : "\arHIDDEN");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		WritePrivateProfileString("Settings", "DisplayHUDElements", DisplayEnabled ? "on" : "off", INIFileName);
	}
	else if (lcArg1 == "hudstring" && lcArg2.length() > 0)
		WatchHUD((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "notifyhudstring" && lcArg2.length() > 0)
		WatchNotifyHUD((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "notifychatstring" && lcArg2.length() > 0)
		WatchChatHUD((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "time" || lcArg1 == "timestamp")
	{
		g_useTimeStamp = !g_useTimeStamp;
		sprintf_s(szTemp, "\atTimeStamps now %s\ax", g_useTimeStamp ? "\agON" : "\arOFF");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
	}
	else if (lcArg1 == "bg")
	{
		bBGUpdate = !bBGUpdate;
		sprintf_s(szTemp, "\atBackground updates are now %s\ax", bBGUpdate ? "\agON" : "\arOFF");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		WritePrivateProfileString("Settings", "UpdateInBackground", bBGUpdate ? "on" : "off", INIFileName);
	}
	else if (lcArg1 == "timeformat")
		WatchTime((PCHAR)lcArg2.c_str());
	else if (lcArg1 == "chat")
	{
		g_useChatReport = !g_useChatReport;
		sprintf_s(szTemp, "\atReporting spawns to chat now %s\ax", g_useChatReport ? "\agON" : "\arOFF");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
	}
	else if (lcArg1 == "mq2chat")
	{
		g_UseMQ2Chat = !g_UseMQ2Chat;
		sprintf_s(szTemp, "\atChat output now goes to %s \atchat", g_UseMQ2Chat ? "\agMQ2" : "\arEQ");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		WritePrivateProfileString("Settings", "UseMQ2Chat", g_UseMQ2Chat ? "1" : "0", INIFileName);
	}
	else if (lcArg1 == "all")
	{
		g_useAllZone = !g_useAllZone;
		sprintf_s(szTemp, "\atAll zone watch options now %s\ax", g_useAllZone ? "\agON" : "\arOFF");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
	}
	else if (lcArg1 == "debug")
	{
		DEBUGGING = !DEBUGGING;
		sprintf_s(szTemp, "\atTarget Debugging to Debugspew.log is now %s\ax", DEBUGGING ? "\agON" : "\arOFF");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
	}
	else if (lcArg1 == "verbose")
	{
		gVerbose = !gVerbose;
		WritePrivateProfileString("Settings", "Verbose", gVerbose ? "yes" : "no", INIFileName);
		sprintf_s(szTemp, "\atVerbose mode is now %s\ax", gVerbose ? "\agON" : "\arOFF");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
	}
	else if (lcArg1 == "concolor")
	{
		g_UseConColor = !g_UseConColor;
		WritePrivateProfileString("Settings", "UseConColor", g_UseConColor ? "yes" : "no", INIFileName);
		sprintf_s(szTemp, "\atMob con color mode is now %s\ax", g_UseConColor ? "\agON" : "\arOFF");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
	}
	else if (lcArg1 == "sorttype")
	{
		int Index = 0;
		while (SortTypeNames[Index][0]) {
			if (!_stricmp(szArg2, SortTypeNames[Index])) {
				nsSortType = (SORTTYPE)Index;
				WritePrivateProfileString("Settings", "SortType", SortTypeNames[(int)nsSortType], INIFileName);
				sprintf_s(szTemp, "\atSpawn tracking array sort type set to:  \ag%s", SortTypeNames[(int)nsSortType]);
				WriteToChat(szTemp, USERCOLOR_DEFAULT);
				return;
			}
			Index++;
		}
		sprintf_s(szTemp, "\arUsage: /watch sorttype [type]");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		strcpy_s(szTemp, "    \arvalid types :");
		Index = 0;
		while (SortTypeNames[Index][0]) {
			strcat_s(szTemp, " ");
			strcat_s(szTemp, SortTypeNames[Index]);
			Index++;
		}
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		sprintf_s(szTemp, "\atCurrently set to:  \ag%s", SortTypeNames[(int)nsSortType]);
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		return;
	}
	else if (lcArg1 == "sortorder")
	{
		int Index = 0;
		while (SortOrderNames[Index][0]) {
			if (!_stricmp(szArg2, SortOrderNames[Index])) {
				nsSortOrder = (SORTORDER)Index;
				WritePrivateProfileString("Settings", "SortOrder", SortOrderNames[(int)nsSortOrder], INIFileName);
				sprintf_s(szTemp, "\atSpawn tracking array sort order set to:  \ag%s", SortOrderNames[(int)nsSortOrder]);
				WriteToChat(szTemp, USERCOLOR_DEFAULT);
				return;
			}
			Index++;
		}
		sprintf_s(szTemp, "\arUsage: /watch sortorder [order]");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		strcpy_s(szTemp, "    \arvalid orders :");
		Index = 0;
		while (SortOrderNames[Index][0]) {
			strcat_s(szTemp, " ");
			strcat_s(szTemp, SortOrderNames[Index]);
			Index++;
		}
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		sprintf_s(szTemp, "\atCurrently set to:  \ag%s", SortOrderNames[(int)nsSortOrder]);
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		return;
	}
	else if (lcArg1 == "hudsorttype")
	{
		int Index = 0;
		while (SortTypeNames[Index][0]) {
			if (!_stricmp(szArg2, SortTypeNames[Index])) {
				SortType = (SORTTYPE)Index;
				WritePrivateProfileString("Settings", "HUDSortType", SortTypeNames[(int)SortType], INIFileName);
				sprintf_s(szTemp, "\atHUD sort type set to:  \ag%s", SortTypeNames[(int)SortType]);
				WriteToChat(szTemp, USERCOLOR_DEFAULT);
				return;
			}
			Index++;
		}
		sprintf_s(szTemp, "\arUsage: /watch hudsorttype [type]");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		strcpy_s(szTemp, "    \arvalid types :");
		Index = 0;
		while (SortTypeNames[Index][0]) {
			strcat_s(szTemp, " ");
			strcat_s(szTemp, SortTypeNames[Index]);
			Index++;
		}
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		sprintf_s(szTemp, "\atCurrently set to:  \ag%s", SortTypeNames[(int)SortType]);
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		return;
	}
	else if (lcArg1 == "hudsortorder")
	{
		int Index = 0;
		while (SortOrderNames[Index][0]) {
			if (!_stricmp(szArg2, SortOrderNames[Index])) {
				SortOrder = (SORTORDER)Index;
				WritePrivateProfileString("Settings", "HUDSortOrder", SortOrderNames[(int)SortOrder], INIFileName);
				sprintf_s(szTemp, "\atHUD sort order set to:  \ag%s", SortOrderNames[(int)SortOrder]);
				WriteToChat(szTemp, USERCOLOR_DEFAULT);
				return;
			}
			Index++;
		}
		sprintf_s(szTemp, "\arUsage: /watch hudsortorder [order]");
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		strcpy_s(szTemp, "    \arvalid orders :");
		Index = 0;
		while (SortOrderNames[Index][0]) {
			strcat_s(szTemp, " ");
			strcat_s(szTemp, SortOrderNames[Index]);
			Index++;
		}
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		sprintf_s(szTemp, "\atCurrently set to:  \ag%s", SortOrderNames[(int)SortOrder]);
		WriteToChat(szTemp, USERCOLOR_DEFAULT);
		return;
	}
	else if (lcArg1 == "help")
		WatchUsage(true);
	else
		WatchUsage();
}

void WatchList(PCHAR zone)
{
	CHAR szMsg[MAX_STRING] = { 0 };

	if (g_SearchStrings.size() == 0)
	{
		if (!AlreadyShown) {
			sprintf_s(szMsg, "Not watching for targets in %s", zone);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
		}
	}
	else
	{
		if (!AlreadyShown) {
			sprintf_s(szMsg, "Loaded %d Target searches for %s", (int)g_SearchStrings.size(), zone);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
		}

		int nCount = 0;
		list<SearchStringRecord>::iterator pSearchStrings = g_SearchStrings.begin();
		while (pSearchStrings != g_SearchStrings.end())
		{
			pSearchStrings->nIndex = ++nCount;
			if (!AlreadyShown) {
				char PriorityString[MAX_STRING] = { 0 };
				if (pSearchStrings->Priority)
					sprintf_s(PriorityString, " (Priority %d)", pSearchStrings->Priority);
				sprintf_s(szMsg, "%d. \"%s\"%s%s%s%s", pSearchStrings->nIndex, pSearchStrings->SearchString.c_str(),
					pSearchStrings->bNotify ? " (Notify)" : "", pSearchStrings->nSound ? " (Sound)" : "", pSearchStrings->bHUD ? "" : " (NoHUD)", pSearchStrings->Priority ? PriorityString : "");
				WriteToChat(szMsg, CONCOLOR_YELLOW);
			}
			pSearchStrings++;
		}
	}
	AlreadyShown = false;
}

void WatchAdd(PCHAR zone, PCHAR targetName, int Priority, bool bNotify, int Sound, bool bHUD)
{
	char szTemp[MAX_STRING] = { 0 }, szTarget[MAX_STRING] = { 0 };
	if (!zone || !targetName) {
		WatchUsage();
		return;
	}
	strcpy_s(szTarget, targetName);
	WriteToChat(szTarget, CONCOLOR_YELLOW);
	// show the current watch list
	gVerbose = false;
	if (GetPrivateProfileString("Settings", "Verbose", NULL, szTemp, MAX_STRING, INIFileName)) {
		if (!_stricmp(szTemp, "yes") || !_stricmp(szTemp, "on"))
			gVerbose = true;
	}
	else
		WritePrivateProfileString("Settings", "Verbose", gVerbose ? "yes" : "no", INIFileName);
	// want to do this because we don't want a "hole" in our ini target list
	if (AddToTargetList(szTarget, bNotify, Sound, Priority, bHUD, gVerbose))
		DumpTargetsToINI(zone);
	if (gVerbose)
		WatchList(zone);
	CleanHUDTargets();
	CheckTargets();
}

void WatchRemove(PCHAR zone, PCHAR searchString, bool bNotify)
{
	if (0 == zone || 0 == searchString)
	{
		WatchUsage();
		return;
	}

	CHAR targetName[MAX_STRING] = { 0 };
	strcpy_s(targetName, searchString);
	if (IsNumber(searchString))
	{
		// now look for each searchstring and build a "closest n" list
		list<SearchStringRecord>::iterator pSearchStrings = g_SearchStrings.begin();

		while (pSearchStrings != g_SearchStrings.end())
		{
			if (pSearchStrings->nIndex == atoi(searchString))
			{
				strcpy_s(targetName, pSearchStrings->SearchString.c_str());
				break;
			}
			pSearchStrings++;
		}
	}

	if (RemoveFromTargetList(targetName, bNotify))
	{
		// want to do this because we don't want a "hole" in our ini target list
		DumpTargetsToINI(zone);
		// now try to remove any spawns in the notify list that match the targetname
		RemoveNotifySpawns(targetName);
	}
	// show the current watch list
	WatchList(zone);

	CleanHUDTargets();

	CheckTargets();
}

void WatchTargets(PCHAR pNumTargets)
{
	if (NULL != pNumTargets && IsNumber(pNumTargets))
	{
		CHAR szMsg[MAX_STRING] = { 0 };

		g_numWatchedTargets = atoi(pNumTargets);

		sprintf_s(szMsg, "Listing closest %d targets", (int)g_numWatchedTargets);
		WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

		SavePluginSettings();
	}
	else
		WatchUsage();
}

void WatchXStart(PCHAR pXStart)
{
	if (NULL != pXStart && IsNumber(pXStart))
	{
		CHAR szMsg[MAX_STRING] = { 0 };

		g_HUDXStart = atoi(pXStart);

		sprintf_s(szMsg, "X Position set to %d", g_HUDXStart);
		WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

		SavePluginSettings();
	}
	else
		WatchUsage();
}

void WatchYStart(PCHAR pYStart)
{
	if (NULL != pYStart && IsNumber(pYStart))
	{
		CHAR szMsg[MAX_STRING] = { 0 };

		g_HUDYStart = atoi(pYStart);

		sprintf_s(szMsg, "Y Position set to %d", g_HUDYStart);
		WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

		SavePluginSettings();
	}
	else
		WatchUsage();
}

void WatchYIncrement(PCHAR pYIncrement)
{
	if (NULL != pYIncrement && IsNumber(pYIncrement))
	{
		CHAR szMsg[MAX_STRING] = { 0 };

		g_HUDYIncrement = atoi(pYIncrement);

		sprintf_s(szMsg, "Increment set to %d pixels", g_HUDYIncrement);
		WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

		SavePluginSettings();
	}
	else
		WatchUsage();
}

void WatchFont(PCHAR nSize)
{
	if (NULL != nSize && IsNumber(nSize))
	{
		CHAR szMsg[MAX_STRING] = { 0 };

		nFontSize = atoi(nSize);

		sprintf_s(szMsg, "HUD font size set to %d", nFontSize);
		WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

		SavePluginSettings();
	}
	else
		WatchUsage();
}

void WatchColor(PCHAR HUDType, PCHAR HUDColor)
{
	char szColor[20], szTemp[MAX_STRING] = { 0 };
	if (!HUDType || !HUDColor)
		return;

	if (!HUDColor[0] || strlen(HUDColor) != 6) {
		WatchUsage();
		return;
	}
	_strupr_s(HUDColor, 20);
	strcpy_s(szColor, HUDColor);
	for (int x = 0; x < 6; x++)
		if (szColor[x] < 'A' || szColor[x] > 'F')
			if (szColor[x] < '0' || szColor[x] > '9') {
				WatchUsage();
				return;
			}

	if (!HUDType[0])
		g_HUDColor = GetColor(szColor);
	else if (!strcmp(HUDType, "Guild"))
		g_GuildHUDColor = GetColor(szColor);
	else if (!strcmp(HUDType, "Dead"))
		g_DeadHUDColor = GetColor(szColor);
	else if (!strcmp(HUDType, "Target"))
		g_TargetHUDColor = GetColor(szColor);
	else if (!strcmp(HUDType, "ChatColorAdd"))
		g_ChatColorAdd = GetColor(szColor, COLORCA);
	else if (!strcmp(HUDType, "ChatColorRem"))
		g_ChatColorRem = GetColor(szColor, COLORCA);
	else {
		WatchUsage();
		return;
	}
	if (strstr(HUDType, "Chat")) {
		sprintf_s(szTemp, "%s", HUDType);
		WritePrivateProfileString("Settings", szTemp, szColor, INIFileName);
		if (strstr(HUDType, "Add"))
			sprintf_s(szTemp, "\amChat Add \atColor set to \ag%s", szColor);
		else
			sprintf_s(szTemp, "\amChat Remove \atColor set to \ag%s", szColor);
	}
	else {
		sprintf_s(szTemp, "%sHUDColor", HUDType);
		WritePrivateProfileString("Settings", szTemp, szColor, INIFileName);
		sprintf_s(szTemp, "\am%s%s\atHUD Color set to \ag%s", HUDType, HUDType[0] ? " " : "", szColor);
	}
	WriteToChat(szTemp);
}

void WatchTime(PCHAR pLine)
{
	if (NULL != pLine)
	{
		if (0 == _stricmp("reload", pLine) || 0 == _stricmp("refresh", pLine))
		{
			GetPrivateProfileString("Settings", "TimeStampFormat", "[%H:%M:%S]", g_TimeStampFormat, MAX_STRING, INIFileName);
			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "Time Format String set to %s", g_TimeStampFormat);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
		}
		else if (0 == _stricmp("show", pLine))
		{
			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "TimeStampFormat=%s", g_TimeStampFormat);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
		}
		else
		{
			strcpy_s(g_TimeStampFormat, pLine);
			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "Time Format String set to %s", g_TimeStampFormat);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			WritePrivateProfileString("Settings", "TimeStampFormat", g_TimeStampFormat, INIFileName);
		}
	}
	else
		WatchUsage(true);
}

void WatchHUD(PCHAR pLine)
{
	if (NULL != pLine)
	{
		if (0 == _stricmp("reload", pLine) || 0 == _stricmp("refresh", pLine))
		{
			LoadHUDString();
			LoadChatString();

			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "HUD String set to %s", g_HUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			sprintf_s(szMsg, "Notify HUD String set to %s", g_NotifyHUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			sprintf_s(szMsg, "Notify Chat String set to %s", g_NotifyChatString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
		}
		else if (0 == _stricmp("show", pLine))
		{
			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "HUDString=%s", g_HUDString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
			sprintf_s(szMsg, "NotifyHUDString=%s", g_NotifyHUDString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
			sprintf_s(szMsg, "NotifyChatString=%s", g_NotifyChatString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
		}
		else
		{
			string result = "";
			// remove all the % chars so it doesn't crash below
			if (find_substr(pLine, "%") != -1)
			{
				result = replace(pLine, "%", "");
			}
			strcpy_s(g_HUDString, result.c_str());

			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "HUD String set to %s", g_HUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

			SavePluginSettings();
		}
	}
	else
		WatchUsage(true);
}

void WatchNotifyHUD(PCHAR pLine)
{
	if (NULL != pLine)
	{
		if (0 == _stricmp("reload", pLine) || 0 == _stricmp("refresh", pLine))
		{
			LoadHUDString();
			LoadChatString();

			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "Notify HUD String set to %s", g_HUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			sprintf_s(szMsg, "Notify HUD String set to %s", g_NotifyHUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			sprintf_s(szMsg, "Notify Chat String set to %s", g_NotifyChatString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
		}
		else if (0 == _stricmp("show", pLine))
		{
			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "HUDString=%s", g_HUDString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
			sprintf_s(szMsg, "NotifyHUDString=%s", g_NotifyHUDString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
			sprintf_s(szMsg, "NotifyChatString=%s", g_NotifyChatString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
		}
		else
		{
			string result = "";
			// remove all the % chars so it doesn't crash below
			if (find_substr(pLine, "%") != -1)
			{
				result = replace(pLine, "%", "");
			}
			strcpy_s(g_NotifyHUDString, result.c_str());

			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "Notify HUD String set to %s", g_NotifyHUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

			SavePluginSettings();
		}
	}
	else
		WatchUsage(true);
}

void WatchChatHUD(PCHAR pLine)
{
	if (NULL != pLine)
	{
		if (0 == _stricmp("reload", pLine) || 0 == _stricmp("refresh", pLine))
		{
			LoadHUDString();
			LoadChatString();

			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "HUD String set to %s", g_HUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			sprintf_s(szMsg, "Notify HUD String set to %s", g_NotifyHUDString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
			sprintf_s(szMsg, "Notify Chat String set to %s", g_NotifyChatString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);
		}
		else if (0 == _stricmp("show", pLine))
		{
			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "HUDString=%s", g_HUDString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
			sprintf_s(szMsg, "NotifyHUDString=%s", g_NotifyHUDString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
			sprintf_s(szMsg, "NotifyChatString=%s", g_NotifyChatString);
			WriteToChat(szMsg, CONCOLOR_YELLOW);
		}
		else
		{
			string result = "";
			// remove all the % chars so it doesn't crash below
			if (find_substr(pLine, "%") != -1)
			{
				result = replace(pLine, "%", "");
			}
			strcpy_s(g_NotifyChatString, result.c_str());

			CHAR szMsg[MAX_STRING] = { 0 };
			sprintf_s(szMsg, "Notify Chat String set to %s", g_NotifyChatString);
			WriteToChat(szMsg, USERCOLOR_CHAT_CHANNEL);

			SavePluginSettings();
		}
	}
	else
		WatchUsage(true);
}

void WatchSound(PCHAR pZone, PCHAR pLine)
{
	CHAR szCommand[MAX_STRING] = { 0 };
	GetArg(szCommand, pLine, 1);
	if (!_stricmp(szCommand, "id")) {   // assign sound ID
		CHAR szID[MAX_STRING] = { 0 };
		GetArg(szID, pLine, 2);
		if (TRUE == IsNumber(szID)) {
			char szTemp[MAX_STRING] = { 0 };
			strcpy_s(szTemp, GetNextArg(pLine));
			AssignSound(szTemp);
		}
		else
			WatchUsage();
	}
	else if (0 == _stricmp(szCommand, "list"))
		ListSounds();
	else if (0 == _stricmp(szCommand, "stop"))
		StopNotifySound();
	else
		WatchUsage();
}

void AssignSound(PCHAR pLine)
{
	bool bError = true;
	CHAR szSoundID[MAX_STRING] = { 0 };
	GetArg(szSoundID, pLine, 1);
	if (TRUE == IsNumber(szSoundID))
	{
		int nSoundID = atoi(szSoundID);
		CHAR szFileName[MAX_STRING] = { 0 };
		strcpy_s(szFileName, GetNextArg(pLine));

		if (szFileName && szFileName[0])
		{
			bError = false;
			// write any items up to the ID assigned (makes it easier to iterate to load them)
			for (int idx = 1; idx < nSoundID; idx++)
			{
				CHAR szTempID[MAX_STRING] = { 0 };
				CHAR szTempFile[MAX_STRING] = { 0 };
				sprintf_s(szTempID, "%d", idx);
				GetPrivateProfileString("Sounds", szTempID, "nosound", szTempFile, MAX_STRING, INIFileName);
				WritePrivateProfileString("Sounds", szTempID, szTempFile, INIFileName);
			}
			// save Sound ID/filename
			WritePrivateProfileString("Sounds", szSoundID, szFileName, INIFileName);
			WriteChatf("\ar%s\aw::\agAdded alert sound id \am%s\ag = \at%s", mqplugin::PluginName, szSoundID, szFileName);
		}
	}

	if (true == bError)
		WatchUsage();
}

void WatchUsage(bool bHelp)
{
	WriteChatf("\ao--------------------------------------------------------------------------------------------------------");
	WriteChatf("%s %.2f", mqplugin::PluginName, MQ2Version);
	if (g_useAllZone) {
		WriteChatf("\ay/watch add \"spawnsearch string\" [notify] [sound #] [nohud] [priority #] [/all]");
		WriteChatf("\ay/watch add : by itself, if you have a target, will add the target to watch list");
		WriteChatf("\ay/watch del(ete)|rem(ove) \"spawnsearch string\" [/all]");
		WriteChatf("\ay/watch del(ete)|rem(ove) [index #] [/all]");
		WriteChatf("\ay/watch del|rem : by itself, if you have a target, will remove the target from watch list");
	}
	else {
		WriteChatf("\ay/watch add \"spawnsearch string\" [notify] [sound #] [nohud] [priority #]");
		WriteChatf("\ay/watch add : by itself, if you have a target, will add the target to watch list");
		WriteChatf("\ay/watch del(ete)|rem(ove) \"spawnsearch string\"");
		WriteChatf("\ay/watch del(ete)|rem(ove) [index #]");
		WriteChatf("\ay/watch del|rem : by itself, if you have a target, will remove the target from watch list");
	}
	WriteChatf("\ay/watch [help] [(l)ist] [(time)stamp] [chat] [all] [debug] [limit n] [x #] [y #] [increment #] [verbose] [concolor] [mq2chat] [font #] [bg] [show] [hide]\ax");
	WriteChatf("\ay/watch [[color] [hud|guild|dead|target|chatadd|chatrem] [RGB] | [popupadd|popuprem] [color]]\ax");
	WriteChatf("\ay/watch [[sorttype|sortorder|hudsorttype|hudsortorder [type|order]]\ax");
	WriteChatf("\ay/watch sound [list|stop|id filename]\ax");
	WriteChatf("\arExample\ax \ay/watch sound id 5 shamanspawn.mp3\ax");
	WriteChatf("\agAssigns sound#5 to the file shamanspawn.mp3\ax");
	WriteChatf("\arExample\ax \ay/watch add \"shaman pc\" notify sound 5\ax");
	WriteChatf("\agPlays sound#5 (shamanspawn.mp3) and gives an overlay popup (notify) when player character shamans spawn\ax");
	if (g_useAllZone)
		WriteChatf("\ay \"/all\" after [add|delete|remove] adds or removes entry for all zones\ax");
	if (true == bHelp)
	{
		WriteChatf("\ax");
		WriteChatf("\ax List of search spawn arguments:\ax");
		WriteChatf("\at [pc|npc|mount|pet|nopet|familiar|nofamiliar|invis|noradius|corpse|npccorpse|pccorpse]\ax");
		WriteChatf("\at [trigger|untargetable|trap|chest|timer|aura|object|banner|campfire|mercenary|flyer|any]\ax");
		WriteChatf("\at [next|prev|lfg|gm|group|raid|noguild|trader|named|merchant|tribute|knight]\ax");
		WriteChatf("\at [tank|healer|dps|slower|los|targetable|range|loc|id|radius|body|class|race|light|GUILD]\ax");
		WriteChatf("\at [guild|alert|noalert|notnearalert|nearalert|zradius|notid|nopcnear]\ax");

		WriteChatf("\ax");
		WriteChatf("\ay/watch [timeformat [reload|show|String]]\ax");
		WriteChatf("\ay/watch [hudstring [reload|show|String|&clr|&dst|&arr]]\ax");
		WriteChatf("\ay/watch [notifyhudstring [reload|show|String|&clr|&dst|&arr]]\ax");
		WriteChatf("\ay/watch [notifychatstring [reload|show|String|&clr|&dst|&arr]]\ax");
		WriteChatf("\arExample\ax \ay/watch hudstring ${Target.CleanName} ${Target.Level}${Target.Class.ShortName} ${Target.Distance}&arr\ax");
		WriteChatf("\agdisplays \ayCleric01 75CLR 30.23-->\ax");
		WriteChatf("\amHint\ax Use MQ2HUD syntax strings in MQ2Targets.ini, Keys: HUDString, NotifyHUDString, NotifyChatString\ax");
		WriteChatf("\am(MQ2Targets replaces \ayTarget.\ax with \aySpawn[<spawnid>].\ax at runtime)");
	}
	WriteChatf("\ao--------------------------------------------------------------------------------------------------------");
}

void WriteToChat(PCHAR szText, DWORD Color)
{
	CHAR szOutput[MAX_STRING] = { 0 };
	sprintf_s(szOutput, "%s: %s", mqplugin::PluginName, szText);
	if (g_UseMQ2Chat)
		WriteChatColor(szOutput, Color);
	else
		dsp_chat_no_events(szOutput, Color, false);
}

// return true if im ingame and have access to all information.
BOOL InGame()
{
	return(!gZoning && gGameState == GAMESTATE_INGAME && GetPcProfile() && pLocalPC && pLocalPlayer);
}

// starts searching for targets in current zone
void StartSearchingForTargets()
{
	CheckForTargets(true);

	LoadZoneTargets();

	CheckTargets();
	// reset "timer"
	g_timerSeconds = time(NULL);
	// reset "timer"
	g_timerOneSecond = time(NULL);
}

// target display
void CheckForTargets(bool bState)
{
	g_CheckForTargets = bState;

	DebugSpewAlways("MQ2Targets::CheckForTargets(%d)", bState);
}

// returns number of new targets found
int CheckTargets()
{
	CHAR szTemp[MAX_STRING] = { 0 };

	int nReturn = 0;

	if (!InGame())
		return nReturn;

	if (true == g_CheckForTargets)
	{
		// don't bother if nothing to search for in this zone
		if (g_SearchStrings.size() > 0)
		{
			CHAR szMsg[MAX_STRING] = { 0 };
			CHAR szArg[MAX_STRING] = { 0 };
			PCHAR szRest = NULL;

			PSPAWNINFO pOrigin = pLocalPlayer;
			// now look for each searchstring and build a "closest n" list
			list<SearchStringRecord>::iterator pSearchStrings = g_SearchStrings.begin();

			while (pSearchStrings != g_SearchStrings.end())
			{
				MQSpawnSearch SearchSpawn;
				ClearSearchSpawn(&SearchSpawn);

				strcpy_s(szArg, pSearchStrings->SearchString.c_str());
				strcpy_s(szTemp, szArg);
				_strlwr_s(szTemp);
				if (strstr(szArg, "guild") && pLocalPC->GuildID == -1) {
					pSearchStrings++;
					continue;
				}

				PCHAR szLine = szArg;

				CHAR szName[MAX_STRING] = { 0 };
				CHAR szLLine[MAX_STRING] = { 0 };
				const char* szFilter = szLLine;
				BOOL bArg = TRUE;

				strcpy_s(szLLine, szLine);
				_strlwr_s(szLLine);

				while (bArg)
				{
					GetArg(szArg, szFilter, 1);
					szFilter = GetNextArg(szFilter, 1);
					if (szArg[0] == 0)
					{
						bArg = FALSE;
					}
					else
					{
						szFilter = ParseSearchSpawnArgs(szArg, szFilter, &SearchSpawn);
					}
				}

				nReturn += FindMatchingSpawns(&SearchSpawn, pOrigin, pSearchStrings->bNotify, pSearchStrings->nSound, pSearchStrings->Priority, pSearchStrings->bHUD);

				pSearchStrings++;
			}
		}
		else
		{
			// clean up targets on HUD
			CleanHUDTargets();
		}
	}
	// if we found new matches, set the timer so it'll fire right away
	if (0 != nReturn)
		g_timerSeconds -= POPUPINTERVAL;

	return nReturn;
}

int FindMatchingSpawns(MQSpawnSearch* pSearchSpawn, SPAWNINFO* pOrigin, bool bNotify, int nSound, int Priority, bool bHUD)
{
	int nReturn = 0;

	//DebugSpewAlways("MQ2Targets::FindMatchingSpawns()");

	if (!pSearchSpawn || !pOrigin)
		return nReturn;

	PSPAWNINFO pSpawn = (PSPAWNINFO)pSpawnList;

	CHAR szTemp[MAX_STRING] = { 0 };

	while (pSpawn)
	{
		if (pSpawn != pOrigin && SpawnMatchesSearch(pSearchSpawn, pOrigin, pSpawn))
		{
			bool bAlreadyInThere = false;
			// see if it's already in our list.  If so, don't add it again
			for (unsigned long i = 0; i < g_Targets.size(); i++)
			{
				PSPAWNINFO theTarget = g_Targets[i].pSpawn;
				if (theTarget && pSpawn->SpawnID == theTarget->SpawnID)
				{
					bAlreadyInThere = true;
				}
			}
			// not already in our list, add it
			if (false == bAlreadyInThere && NULL == strstr(pSpawn->DisplayedName, " Mount"))
			{
				// matches search, add to our set
				if (bHUD) {
					float dist = GetDistance(pOrigin->X, pOrigin->Y, pSpawn->X, pSpawn->Y);
					g_Targets.push_back(TargetEntryFloat{pSpawn, dist});
				}
				g_nsTargets.push_back(TargetEntryInt{pSpawn, Priority});

				if (bNotify)
				{
					nReturn += AddToNotify(pSpawn, nSound, Priority);
				}
			}
		}
		pSpawn = pSpawn->pNext;
	}
	return nReturn;
}

void CleanHUDTargets()
{

	g_Targets.clear();
	g_nsTargets.clear();
}

static int pMQRankCompare_(const TargetEntryFloat& A, const TargetEntryFloat& B)
{
	if (A.Float == B.Float) {
		if (SortType == SORT_NAME) {
			if (SortOrder == SORT_REVERSE)
				return _stricmp(A.pSpawn->DisplayedName, B.pSpawn->DisplayedName) * -1;
			return _stricmp(B.pSpawn->DisplayedName, A.pSpawn->DisplayedName) * -1;
		}
		return _stricmp(B.pSpawn->DisplayedName, A.pSpawn->DisplayedName) * -1;
	}
	if (SortOrder == SORT_REVERSE) {
		if (SortType == SORT_LEVEL || SortType == SORT_PRIORITY) {
			if (A.Float > B.Float)
				return 1;
			return -1;
		}
		if (A.Float > B.Float)
			return -1;
		return 1;
	}
	if (SortType == SORT_LEVEL || SortType == SORT_PRIORITY) {
		if (A.Float > B.Float)
			return -1;
		return 1;
	}
	if (A.Float > B.Float)
		return 1;
	return -1;
}
static bool pMQRankCompare(const TargetEntryFloat& A, const TargetEntryFloat& B)
{
	return pMQRankCompare_(A, B) < 0;
}

static int pMQRankCompareNS_(const TargetEntryInt& A, const TargetEntryInt& B)
{
	if (nsSortType == SORT_PRIORITY) {
		if (A.Int == B.Int)
			return _stricmp(B.pSpawn->DisplayedName, A.pSpawn->DisplayedName) * -1;
		if (nsSortOrder == SORT_NORMAL) {
			if (A.Int > B.Int)
				return -1;
			return 1;
		}
		if (A.Int > B.Int)
			return 1;
		return -1;
	}

	if (nsSortType == SORT_LEVEL) {
		if (A.pSpawn->Level == B.pSpawn->Level)
			return _stricmp(B.pSpawn->DisplayedName, A.pSpawn->DisplayedName) * -1;
		if (nsSortOrder == SORT_NORMAL) {
			if (A.pSpawn->Level > B.pSpawn->Level)
				return -1;
			return 1;
		}
		if (A.pSpawn->Level > B.pSpawn->Level)
			return 1;
		return -1;
	}

	if (nsSortType == SORT_DISTANCE) {
		if (pLocalPlayer) {
			float DistA = GetDistanceSquared(pLocalPlayer, A.pSpawn);
			float DistB = GetDistanceSquared(pLocalPlayer, B.pSpawn);
			if (DistA == DistB)
				return _stricmp(B.pSpawn->DisplayedName, A.pSpawn->DisplayedName) * -1;
			if (nsSortOrder == SORT_NORMAL) {
				if (DistA > DistB)
					return 1;
				return -1;
			}
			if (DistA > DistB)
				return -1;
			return 1;
		}
		return _stricmp(B.pSpawn->DisplayedName, A.pSpawn->DisplayedName) * -1;
	}

	if (nsSortOrder == SORT_REVERSE)
		return _stricmp(A.pSpawn->DisplayedName, B.pSpawn->DisplayedName) * -1;
	return _stricmp(B.pSpawn->DisplayedName, A.pSpawn->DisplayedName) * -1;
}
static bool pMQRankCompareNS(const TargetEntryInt& A, const TargetEntryInt& B)
{
	return pMQRankCompareNS_(A, B) < 0;
}

int AddToNotify(PSPAWNINFO pSpawn, int nSound, int Priority)
{
	int nReturn = 0;

	if (!pSpawn)
		return 0;
	if (!pSpawn->SpawnID)
		return 0;

	bool bFound = false;
	// see if it's already in our list.  If so, don't add it again
	for (NotifyRecord& Item : g_NotifySpawns)
	{
		if (pSpawn->SpawnID == Item.SpawnID)
		{
			bFound = true;
			if (DEBUGGING)
				DebugSpewAlways("MQ2Targets::AddToNotify((pSpawn), %d): '%s' already in list", nSound, pSpawn->DisplayedName);

			break;
		}
	}

	// only add to notify list if it's not already in there
	if (!bFound)
	{
		if (DEBUGGING)
			DebugSpewAlways("MQ2Targets::AddToNotify((pSpawn), %d): Adding '%s' to list", nSound, pSpawn->DisplayedName);

		NotifyRecord notifyStruct;
		notifyStruct.bNotified = false;
		notifyStruct.nSound = nSound;
		notifyStruct.Priority = Priority;
		notifyStruct.SpawnID = pSpawn->SpawnID;
		notifyStruct.DisplayedName = pSpawn->DisplayedName;
		g_NotifySpawns.push_back(notifyStruct);
		nReturn = 1;
	}

	return nReturn;
}

void RemoveFromNotify(uint32_t SpawnID, const char* DisplayedName, bool bPopup)
{
	__time32_t long_time;
	struct tm currentTime;
	CHAR strTimeBuffer[MAX_STRING] = { 0 };

	if (!SpawnID)
		return;

	// see if it's already in our list.  If so, don't add it again
	auto iter = g_NotifySpawns.begin();
	DebugChat("RemoveFromNotify: pSpawn->SpawnID=%d, pSpawn->Name='%s'", SpawnID, DisplayedName);
	while (g_NotifySpawns.size() > 0 && iter != g_NotifySpawns.end())
	{
		NotifyRecord& rec = *iter;
		if (rec.SpawnID <= 0) {
			iter++;
			continue;
		}
		DebugChat("Checking pItem->Spawn.SpawniD=%d vs pSpawn->SpawnID=%d, pSpawn->DisplayedName='%s', pItem->Spawn.DisplayedName = '%s'",
			rec.SpawnID, SpawnID, DisplayedName, rec.DisplayedName);

		if (rec.SpawnID == SpawnID && DisplayedName[0] && !rec.DisplayedName.empty() && strstr(DisplayedName, rec.DisplayedName.c_str()))
		{
			DebugChat("MQ2Targets::RemoveFromNotify((pSpawn), %d): Removing '%s' from list.", bPopup, rec.DisplayedName.c_str());

			if (bPopup)
			{
				CHAR szText[MAX_STRING];
				bool nFound = false;
				const char* notifyText = nullptr;
				size_t nIndex = 0;
				for (; nIndex < Notifications.size(); nIndex++) {
					NotificationsStruct& VectorRef = Notifications[nIndex];
					if (VectorRef.spawnID == rec.SpawnID) {
						notifyText = VectorRef.notifyText.c_str();
						nFound = true;
						break;
					}
				}
				if (g_useTimeStamp) {
					_time32(&long_time);
					_localtime32_s(&currentTime, &long_time);
					strftime(strTimeBuffer, MAX_STRING, g_TimeStampFormat, &currentTime);
					if (nFound) {
						sprintf_s(szText, "%s despawned at %s", notifyText, strTimeBuffer);
						Notifications.erase(Notifications.begin() + nIndex);
					} else {
						sprintf_s(szText, "%s despawned at %s", rec.DisplayedName.c_str(), strTimeBuffer);
					}
				} else {
					if (nFound) {
						sprintf_s(szText, "%s despawned", notifyText);
						Notifications.erase(Notifications.begin() + nIndex);
					} else {
						sprintf_s(szText, "%s despawned", rec.DisplayedName.c_str());
					}
				}

				DisplayOverlayText(szText, g_PopupColorRem, 100, 1000, 1000, 3000);

				if (g_useChatReport) {
					ARGBCOLOR RGB;
					RGB.ARGB = g_ChatColorRem;
					char szTmp[MAX_STRING] = { 0 };
					sprintf_s(szTmp, "\a#%02X%02X%02X%s", RGB.R, RGB.G, RGB.B, szText);
					WriteToChat(szTmp);
				}
			}
			// get rid of the one I just showed
			g_NotifySpawns.erase(iter);
			break;
		}
		iter++;
	}
}

// cleans up any spawns in the notify list based on the passed in search string
// used on "/watch remove ..."
void RemoveNotifySpawns(PCHAR searchString)
{
	if (FALSE == InGame())
		return;

	CHAR szArg[MAX_STRING] = { 0 };
	MQSpawnSearch SearchSpawn;
	ClearSearchSpawn(&SearchSpawn);

	strcpy_s(szArg, searchString);

	PCHAR szLine = szArg;

	CHAR szName[MAX_STRING] = { 0 };
	CHAR szLLine[MAX_STRING] = { 0 };
	const char* szFilter = szLLine;
	BOOL bArg = TRUE;

	strcpy_s(szLLine, szLine);
	_strlwr_s(szLLine);

	while (bArg)
	{
		GetArg(szArg, szFilter, 1);
		szFilter = GetNextArg(szFilter, 1);
		if (szArg[0] == 0)
		{
			bArg = FALSE;
		}
		else
		{
			szFilter = ParseSearchSpawnArgs(szArg, szFilter, &SearchSpawn);
		}
	}

	PSPAWNINFO pOrigin = pLocalPlayer;
	PSPAWNINFO pSpawn = pSpawnList;
	CHAR szTemp[MAX_STRING] = { 0 };
	// see if it's already in our list.  If so, don't add it again
	auto iter = g_NotifySpawns.begin();
	while (iter != g_NotifySpawns.end())
	{
		NotifyRecord& rec = *iter;
		SPAWNINFO* pSpawn = GetSpawnByID(rec.SpawnID);
		if (rec.SpawnID != pOrigin->SpawnID && pSpawn && SpawnMatchesSearch(&SearchSpawn, pOrigin,pSpawn))
		{
			RemoveFromNotify(rec.SpawnID, rec.DisplayedName.c_str());
			// start at the beginning of the list again (since it's changed)
			iter = g_NotifySpawns.begin();
		}
		else
			iter++;   // go to the next item
	}
}

void PopupNotifyTarget()
{
	int nSound, nShown;
	__time32_t long_time;
	struct tm currentTime;
	CHAR strTimeBuffer[MAX_STRING] = { 0 };
	CHAR outText[MAX_STRING] = { 0 };
	CHAR szSpawnID[20] = { 0 };
	NotificationsStruct tNotify;

	PSPAWNINFO me = pLocalPlayer;

	if (!g_NotifySpawns.empty())
	{
		nSound = 0;
		nShown = 0;
		CHAR szList[MAX_STRING] = { 0 };
		CHAR szChatList[MAX_STRING] = { 0 };
		// grab the first item to show (if it hasn't already been shown)
		auto iter = g_NotifySpawns.begin();
		while (iter != g_NotifySpawns.end() && nShown < 10)
		{
			NotifyRecord& rec = *iter;

			if (!rec.bNotified)
			{
				CHAR szText[MAX_STRING];
				CHAR szChatText[MAX_STRING];
				strcpy_s(outText, g_NotifyChatString);
				sprintf_s(szSpawnID, "${Spawn[%d].", rec.SpawnID);
				if (find_substr(outText, "${Target.") != -1)
				{
					string result = replace(outText, "${Target.", szSpawnID);
					strcpy_s(outText, result.c_str());
				}
				ParseMacroParameter(me, outText);
				if (g_useTimeStamp) {
					_time32(&long_time);
					_localtime32_s(&currentTime, &long_time);
					strftime(strTimeBuffer, MAX_STRING, g_TimeStampFormat, &currentTime);
					if (strlen(szList) > 0) {
						strcat_s(szList, "\n");
						strcat_s(szChatList, "\n\awMQ2Targets: ");
					}
					//					sprintf_s(szText, "%s spawned at %s", pItem->pSpawn->DisplayedName, strTimeBuffer);
					sprintf_s(szText, "%s spawned at %s", outText, strTimeBuffer);
					ARGBCOLOR RGB;
					RGB.ARGB = g_ChatColorAdd;
					sprintf_s(szChatText, "\a#%02X%02X%02X%s spawned at %s", RGB.R, RGB.G, RGB.B, outText, strTimeBuffer);
					tNotify.notifyText = outText;
					tNotify.spawnID = rec.SpawnID;
					Notifications.push_back(tNotify);
				}
				else {
					if (strlen(szList) > 0) {
						strcat_s(szList, "\n");
						strcat_s(szChatList, "\n\awMQ2Targets: ");
					}
					//					sprintf_s(szText, "%s spawned", pItem->pSpawn->DisplayedName);
					sprintf_s(szText, "%s spawned", outText);
					ARGBCOLOR RGB;
					RGB.ARGB = g_ChatColorAdd;
					sprintf_s(szChatText, "\a#%02X%02X%02X%s spawned", RGB.R, RGB.G, RGB.B, outText);
					tNotify.notifyText = outText;
					tNotify.spawnID = rec.SpawnID;
					Notifications.push_back(tNotify);
				}
				strcat_s(szList, szText);
				strcat_s(szChatList, szChatText);
				nShown++;
				if (rec.nSound != 0)
					nSound = rec.nSound;
			}
			// mark it as handled
			rec.bNotified = true;
			iter++;
		}
		if (nShown > 0)
		{
			if (0 != nSound)
				PlayNotifySound(nSound);
			DisplayOverlayText(szList, g_PopupColorAdd, 100, 1000, 1000, 3000);
			if (g_useChatReport) {
				if (outText[0] && strcmp(outText, "NULL"))
				{
					WriteToChat(szChatList);
				}
			}
		}
	}
}

void PlayNotifySound(PCHAR pFileName)
{
	StopNotifySound();

	CHAR lpszOpenCommand[MAX_STRING] = { 0 };
	CHAR lpszPlayCommand[MAX_STRING] = { 0 };
	CHAR szError[MAX_STRING] = { 0 };
	char szMsg[MAX_STRING] = { 0 };

	CHAR szFileName[_MAX_PATH] = { 0 };
	sprintf_s(szFileName, "%s\\%s", gPathResources, pFileName);

	CHAR drive[_MAX_DRIVE];
	CHAR dir[_MAX_DIR];
	CHAR fname[_MAX_FNAME];
	CHAR ext[_MAX_EXT];

	_splitpath_s(szFileName, drive, dir, fname, ext);
	sprintf_s(szFileName, "\"%s\\%s\"", gPathResources, pFileName);
	DebugSpewAlways("MQ2Targets::PlayNotifySound(%s): Fullpath=%s, Drive=%s, Dir=%s, File=%s, Ext=%s", pFileName, szFileName, drive, dir, fname, ext);
	bool bFound = false;
	if (!_stricmp(ext, ".mp3"))
	{
		sprintf_s(lpszOpenCommand, "Open %s type MPEGVideo Alias mySound", szFileName);
		if (g_mp3Length < 1)
			sprintf_s(lpszPlayCommand, "play mySound from 0 notify");
		else
			sprintf_s(lpszPlayCommand, "play mySound from 0 to %u notify", g_mp3Length);
		DebugSpewAlways("MQ2Targets:PlayNotifySound(%s): Playing .mp3 with \"%s\"", pFileName, lpszOpenCommand);
		bFound = true;
	}
	else if (!_stricmp(ext, ".wav"))
	{
		sprintf_s(lpszOpenCommand, "Open %s type waveaudio Alias mySound", szFileName);
		if (g_wavLength < 1)
			sprintf_s(lpszPlayCommand, "play mySound from 0 notify");
		else
			sprintf_s(lpszPlayCommand, "play mySound from 0 to %u notify", g_wavLength);
		DebugSpewAlways("MQ2Targets:PlayNotifySound(%s): Playing .wav with \"%s\"", pFileName, lpszOpenCommand);
		bFound = true;
	}

	if (bFound)
	{
		int error = mciSendString(lpszOpenCommand, NULL, 0, NULL);
		if (error)
		{
			mciGetErrorString(error, szError, MAX_STRING);
			DebugSpewAlways("MQ2Targets::PlayNotifySound(%s): Error %d (%s) on %s", pFileName, error, szError, lpszOpenCommand);
		}
		else
		{
			error = mciSendString("status mySound length", szMsg, MAX_STRING, NULL);
			if (error)
			{
				mciGetErrorString(error, szError, MAX_STRING);
				DebugSpewAlways("MQ2Targets::PlayNotifySound(%s): Error %d (%s) on status mySound length", pFileName, error, szError);
			}
			else
			{
				DebugSpewAlways("MQ2Targets::PlayNotifySound(%s): Audio length %u", pFileName, atoi(szMsg));
				if (!_stricmp(ext, ".mp3"))
				{
					if ((unsigned)atoi(szMsg) < g_mp3Length)
						sprintf_s(lpszPlayCommand, "play mySound from 0 notify");
				}
				else
				{
					if ((unsigned)atoi(szMsg) < g_wavLength)
						sprintf_s(lpszPlayCommand, "play mySound from 0 notify");
				}
				error = mciSendString(lpszPlayCommand, NULL, 0, NULL);
				if (error)
				{
					mciGetErrorString(error, szError, MAX_STRING);
					DebugSpewAlways("MQ2Targets::PlayNotifySound(%s): Error %d (%s) on %s", pFileName, error, szError, lpszPlayCommand);
				}
			}
		}
		if (error)
			bFound = false;
	}

	if (!bFound)
	{
		PlaySound("nosound", NULL, SND_ASYNC);   // default beep
		DebugSpewAlways("MQ2Targets::PlayNotifySound(%s): PlaySound(nosound)", pFileName);
	}
}

void PlayNotifySound(int nSoundID)
{
	// convert sound ID int to string
	CHAR szSoundID[20] = { 0 };
	sprintf_s(szSoundID, "%d", nSoundID);
	// get requested sound file based on ID (nosound is default - failure)
	CHAR szSoundFile[MAX_STRING] = { 0 };
	GetPrivateProfileString("Sounds", szSoundID, "nosound", szSoundFile, MAX_STRING, INIFileName);

	PlayNotifySound(szSoundFile);
	DebugSpewAlways("MQ2Targets::PlayNotifySound(%d)", nSoundID);
}

void StopNotifySound()
{
	// shut down any sound (in case it's still playing)
	mciSendString("Close mySound", NULL, 0, NULL);
}

void ListSounds()
{
	WriteToChat("-----Sounds-----", CONCOLOR_YELLOW);

	CHAR szTempID[MAX_STRING] = { 0 };
	CHAR szSoundFile[MAX_STRING] = { 0 };
	CHAR szBuffer[MAX_STRING] = { 0 };

	int i = 1;
	do
	{
		sprintf_s(szTempID, "%d", i);
		GetPrivateProfileString("Sounds", szTempID, "bleep", szSoundFile, MAX_STRING, INIFileName);
		if (0 == _stricmp(szSoundFile, "bleep"))
		{
			break;
		}
		else if (0 != _stricmp(szSoundFile, "nosound"))   // only show ones that have a sound
		{
			sprintf_s(szBuffer, "%s - %s", szTempID, szSoundFile);
			WriteToChat(szBuffer, CONCOLOR_YELLOW);
		}
	} while (++i);
}

DWORD GetColor(char* TColor, BYTE CA)
{
	char* Stop, szColor[6];
	ARGBCOLOR Color;
	Color.A = CA;
	Color.R = 0xFF;
	Color.G = 0xFF;
	Color.B = 0xFF;
	if (strlen(TColor) < 6)
		return(Color.ARGB);
	szColor[0] = TColor[0];
	szColor[1] = TColor[1];
	szColor[2] = 0;
	Color.R = (BYTE)strtoul(szColor, &Stop, 16);
	szColor[0] = TColor[2];
	szColor[1] = TColor[3];
	szColor[2] = 0;
	Color.G = (BYTE)strtoul(szColor, &Stop, 16);
	szColor[0] = TColor[4];
	szColor[1] = TColor[5];
	szColor[2] = 0;
	Color.B = (BYTE)strtoul(szColor, &Stop, 16);
	return(Color.ARGB);
}
