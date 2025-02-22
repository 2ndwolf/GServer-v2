#include "IDebug.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

#include "TServer.h"
#include "main.h"
#include "TPlayer.h"
#include "TWeapon.h"
#include "IUtil.h"
#include "TNPC.h"
#include "TMap.h"
#include "TLevel.h"

static const char* const filesystemTypes[] =
{
	"all",
	"file",
	"level",
	"head",
	"body",
	"sword",
	"shield",
	0
};

extern std::atomic_bool shutdownProgram;

TServer::TServer(const CString& pName)
	: running(false), doRestart(false), name(pName), serverlist(this), wordFilter(this)
#ifdef V8NPCSERVER
	, mScriptEngine(this), mPmHandlerNpc(nullptr)
#endif
#ifdef UPNP
	, upnp(this)
#endif
{
	auto time_now = std::chrono::high_resolution_clock::now();
	lastTimer = lastNWTimer = last1mTimer = last5mTimer = last3mTimer = time_now;
	calculateServerTime();

	// This has the full path to the server directory.
	serverpath = CString() << getHomePath() << "servers/" << name << "/";
	CFileSystem::fixPathSeparators(serverpath);

	// Set up the log files.
	CString logpath = serverpath.remove(0, getHomePath().length());
	CString npcPath = CString() << logpath << "logs/npclog.txt";
	CString rcPath = CString() << logpath << "logs/rclog.txt";
	CString serverPath = CString() << logpath << "logs/serverlog.txt";
	CFileSystem::fixPathSeparators(npcPath);
	CFileSystem::fixPathSeparators(rcPath);
	CFileSystem::fixPathSeparators(serverPath);
	npclog.setFilename(npcPath);
	rclog.setFilename(rcPath);
	serverlog.setFilename(serverPath);

#ifdef V8NPCSERVER
	CString scriptPath = CString() << logpath << "logs/scriptlog.txt";
	CFileSystem::fixPathSeparators(scriptPath);
	scriptlog.setFilename(scriptPath);
#endif

	// Announce ourself to other classes.
	for (auto & fs : filesystem) {
		fs.setServer(this);
	}
	filesystem_accounts.setServer(this);
}

TServer::~TServer()
{
	cleanup();
}

int TServer::init(const CString& serverip, const CString& serverport, const CString& localip, const CString& serverinterface)
{
	// Player ids 0 and 1 break things.  NPC id 0 breaks things.
	// Don't allow anything to have one of those ids.
	// Player ids 16000 and up is used for players on other servers and "IRC"-channels.
	// The players from other servers should be unique lists for each player as they are fetched depending on
	// what the player chooses to see (buddies, "global guilds" tab, "other servers" tab)
	playerIds.resize(2);
	npcIds.resize(10001); // Starting npc ids at 10,000 for now on..

#ifdef V8NPCSERVER
	// Initialize the Script Engine
	if (!mScriptEngine.Initialize())
	{
		serverlog.out("[%s] ** [Error] Could not initialize script engine.\n", name.text());
		// TODO(joey): new error number? log is probably enough
		return ERR_SETTINGS;
	}
#endif

	// Load the config files.
	int ret = loadConfigFiles();
	if (ret) return ret;

	// If an override serverip and serverport were specified, fix the options now.
	if (!serverip.isEmpty())
		settings.addKey("serverip", serverip);
	if (!serverport.isEmpty())
		settings.addKey("serverport", serverport);
	if (!localip.isEmpty())
		settings.addKey("localip", localip);
	if (!serverinterface.isEmpty())
		settings.addKey("serverinterface", serverinterface);

	overrideIP = serverip;
	overridePort = serverport;
	overrideLocalIP = localip;
	overrideInterface = serverinterface;

	// Fix up the interface to work properly with CSocket.
	CString oInter = overrideInterface;
	if (overrideInterface.isEmpty())
		oInter = settings.getStr("serverinterface");
	if (oInter == "AUTO")
		oInter.clear();

	// Initialize the player socket.
	playerSock.setType(SOCKET_TYPE_SERVER);
	playerSock.setProtocol(SOCKET_PROTOCOL_TCP);
	playerSock.setDescription("playerSock");

	// Start listening on the player socket.
	serverlog.out("[%s]      Initializing player listen socket.\n", name.text());
	if (playerSock.init((oInter.isEmpty() ? 0 : oInter.text()), settings.getStr("serverport").text()))
	{
		serverlog.out("[%s] ** [Error] Could not initialize listening socket...\n", name.text());
		return ERR_LISTEN;
	}
	if (playerSock.connect())
	{
		serverlog.out("[%s] ** [Error] Could not connect listening socket...\n", name.text());
		return ERR_LISTEN;
	}

#ifdef UPNP
	// Start a UPNP thread.  It will try to set a UPNP port forward in the background.
	serverlog.out("[%s]      Starting UPnP discovery thread.\n", name.text());
	upnp.initialize((oInter.isEmpty() ? playerSock.getLocalIp() : oInter.text()), settings.getStr("serverport").text());
	upnp_thread = std::thread(std::ref(upnp));
#endif

#ifdef V8NPCSERVER
	// Setup NPC Control port
	mNCPort = strtoint(settings.getStr("serverport"));

	mNpcServer = new TPlayer(this, nullptr, 0);
	mNpcServer->setType(PLTYPE_NPCSERVER);
	mNpcServer->loadAccount("(npcserver)");
	mNpcServer->setHeadImage(settings.getStr("staffhead", "head25.png"));
	mNpcServer->setLoaded(true);	// can't guarantee this, so forcing it

	// TODO(joey): Update this when server options is changed?
	// Set nickname, and append (Server) - required!
	CString nickName = settings.getStr("nickname");
	if (nickName.isEmpty())
		nickName = "NPC-Server";
	nickName << " (Server)";
	mNpcServer->setNick(nickName, true);

	// Add npc-server to playerlist
	addPlayer(mNpcServer);
#endif

	// Register ourself with the socket manager.
	sockManager.registerSocket((CSocketStub*)this);

	return 0;
}

// Called when the TServer is put into its own thread.
void TServer::operator()()
{
	running = true;
	while (running)
	{
		// Do a server loop.
		doMain();

		// Clean up deleted players here.
		cleanupDeletedPlayers();

		// Check if we should do a restart.
		if (doRestart)
		{
			doRestart = false;
			cleanup();
			int ret = init(overrideIP, overridePort, overrideLocalIP, overrideInterface);
			if (ret != 0)
				break;
		}

		if (shutdownProgram)
			running = false;
	}
	cleanup();
}

void TServer::cleanupDeletedPlayers()
{
	if (deletedPlayers.empty()) return;
	for (auto i = deletedPlayers.begin(); i != deletedPlayers.end();)
	{
		TPlayer* player = *i;
		if (player == nullptr)
		{
			i = deletedPlayers.erase(i);
			continue;
		}

#ifdef V8NPCSERVER
		IScriptObject<TPlayer> *playerObject = player->getScriptObject();
		if (playerObject != 0)
		{
			// Process last script events for this player
			if (!player->isProcessed())
			{
				// Leave the level now while the player object is still alive
				if (player->getLevel() != 0)
					player->leaveLevel();

				// Send event to server that player is logging out
				if (player->isLoaded() && (player->getType() & PLTYPE_ANYPLAYER))
				{
					for (auto it = npcNameList.begin(); it != npcNameList.end(); ++it)
					{
						TNPC *npcObject = (*it).second;
						npcObject->queueNpcAction("npc.playerlogout", player);
					}
				}

				// Set processed
				player->setProcessed();
			}

			// If we just added events to the player, we will have to wait for them to run before removing player.
			if (playerObject->isReferenced())
			{
				SCRIPTENV_D("Reference count: %d\n", playerObject->getReferenceCount());
				++i;
				continue;
			}
		}
#endif

		// Get rid of the player now.
		playerIds[player->getId()] = nullptr;
		for ( auto j = playerList.begin(); j != playerList.end();)
		{
			TPlayer* p = *j;
			if (p == player)
			{
				// Unregister the player.
				sockManager.unregisterSocket(p);

				delete p;
				j = playerList.erase(j);
			}
			else ++j;
		}

		i = deletedPlayers.erase(i);
	}
	//deletedPlayers.clear();
}

void TServer::cleanup()
{
	// Close our UPNP port forward.
	// First, make sure the thread has completed already.
	// This can cause an issue if the server is about to be deleted.
#ifdef UPNP
	if (upnp_thread.joinable())
		upnp_thread.join();
	upnp.remove_all_forwarded_ports();
#endif

	// Save translations.
	this->TS_Save();

	// Save server flags.
	saveServerFlags();

#ifdef V8NPCSERVER
	// Save npcs
	saveNpcs();

	// npc-server will be cleared from playerlist, so lets invalidate the pointer here
	mNpcServer = nullptr;
#endif

	for (auto& player : playerList) {
		delete player;
	}
	playerIds.clear();
	playerList.clear();

	for (auto& level : levelList) {
		delete level;
	}
	levelList.clear();

	for (auto& map : mapList) {
		delete map;
	}
	mapList.clear();

	for (auto& npc : npcList) {
		delete npc;
	}
    npcList.clear();
	npcIds.clear();
	npcNameList.clear();

	saveWeapons();
	for (auto& weapon : weaponList) {
		delete weapon.second;
	}
	weaponList.clear();

#ifdef V8NPCSERVER
	// Clean up the script engine
	mScriptEngine.Cleanup();
#endif

	playerSock.disconnect();
	serverlist.getSocket()->disconnect();

	// Clean up the socket manager.  Pass false so we don't cause a crash.
	sockManager.cleanup(false);
}

void TServer::restart()
{
	doRestart = true;
}

bool TServer::doMain()
{
	// Update our socket manager.
	sockManager.update(0, 5000);		// 5ms

	// Current time
	auto currentTimer = std::chrono::high_resolution_clock::now();

#ifdef V8NPCSERVER
    mScriptEngine.RunScripts(currentTimer);
#endif

	// Every second, do some events.
	auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTimer - lastTimer);
	if (time_diff.count() >= 1000)
	{
		lastTimer = currentTimer;
		doTimedEvents();
	}

	return true;
}

bool TServer::doTimedEvents()
{
	// Do serverlist events.
	serverlist.doTimedEvents();

	// Do player events.
	{
		for (auto & player : playerList)
		{
			assert(player);
            if (!player->isNPCServer())
            {
                if (!player->doTimedEvents())
                    this->deletePlayer(player);
            }
		}
	}

	// Do level events.
	{
		for (auto level : levelList)
		{
            assert(level);
			level->doTimedEvents();
		}

		// Group levels.
		for (auto & groupLevel : groupLevels)
		{
			for (auto & j : groupLevel.second)
			{
				TLevel* level = j.second;
				assert(level);

				level->doTimedEvents();
			}
		}
	}

	// Send NW time.
	auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(lastTimer - lastNWTimer);
	if (time_diff.count() >= 5)
	{
		calculateServerTime();

		lastNWTimer = lastTimer;
        sendPacketToAll(CString() >> (char)PLO_NEWWORLDTIME << CString().writeGInt4(getNWTime()), nullptr);
	}

	// Stuff that happens every minute.
	time_diff = std::chrono::duration_cast<std::chrono::seconds>(lastTimer - last1mTimer);
	if (time_diff.count() >= 60)
	{
		last1mTimer = lastTimer;

		// Save server flags.
		this->saveServerFlags();
	}

	// Stuff that happens every 3 minutes.
	time_diff = std::chrono::duration_cast<std::chrono::seconds>(lastTimer - last3mTimer);
	if (time_diff.count() >= 180)
	{
		last3mTimer = lastTimer;

		// TODO(joey): probably a better way to do this..

		// Resynchronize the file systems.
		filesystem_accounts.resync();
		for (auto & i : filesystem)
			i.resync();
	}

	// Save stuff every 5 minutes.
	time_diff = std::chrono::duration_cast<std::chrono::seconds>(lastTimer - last5mTimer);
	if (time_diff.count() >= 300)
	{
		last5mTimer = lastTimer;

		// Reload some server settings.
		loadAllowedVersions();
		loadServerMessage();
		loadIPBans();

		// Save some stuff.
		// TODO(joey): Is this really needed? We save weapons to disk when it is updated or created anyway..
		saveWeapons();
#ifdef V8NPCSERVER
		saveNpcs();
#endif

		// Check all of the instanced maps to see if the players have left.
		if (!groupLevels.empty())
		{
			for (auto i = groupLevels.begin(); i != groupLevels.end();)
			{
				// Check if any players are found.
				bool playersFound = false;
				for (auto & j : (*i).second)
				{
					TLevel* level = j.second;
					if (!level->getPlayerList()->empty())
					{
						playersFound = true;
						break;
					}
				}

				// If no players are found, delete all the levels in this instance.
				if (!playersFound)
				{
					for (auto & j : (*i).second)
					{
						TLevel* level = j.second;
						delete level;
					}
					(*i).second.clear();
					groupLevels.erase(i++);
				}
				else ++i;
			}
		}
	}

	return true;
}

bool TServer::onRecv()
{
	// Create socket.
	CSocket *newSock = playerSock.accept();
	if (newSock == nullptr)
		return true;

	// Create the new player.
	auto *newPlayer = new TPlayer(this, newSock, 0);

	// Add the player to the server
	if (!addPlayer(newPlayer))
	{
		delete newPlayer;
		return false;
	}

	// Add them to the socket manager.
	sockManager.registerSocket((CSocketStub*)newPlayer);

	return true;
}

/////////////////////////////////////////////////////

void TServer::loadAllFolders()
{
	for (auto & fs : filesystem)
		fs.clear();

	filesystem[0].addDir("world");
	if (settings.getStr("sharefolder").length() > 0)
	{
		std::vector<CString> folders = settings.getStr("sharefolder").tokenize(",");
		for (auto & folder : folders)
			filesystem[0].addDir(folder.trim());
	}
}

void TServer::loadFolderConfig()
{
	for (auto & i : filesystem)
		i.clear();

	foldersConfig = CString::loadToken(CString() << serverpath << "config/foldersconfig.txt", "\n", true);
	for (auto & configLine : foldersConfig)
	{
		// No comments.
		int cLoc = -1;
		if ((cLoc = configLine.find("#")) != -1)
			configLine.removeI(cLoc);
		configLine.trimI();
		if ( configLine.length() == 0) continue;

		// Parse the line.
		CString type = configLine.readString(" ");
		CString config = configLine.readString("");
		type.trimI();
		config.trimI();
		CFileSystem::fixPathSeparators(config);

		// Get the directory.
		CString dirNoWild;
		int pos = -1;
		if ((pos = config.findl(CFileSystem::getPathSeparator())) != -1)
			dirNoWild = config.remove(pos + 1);
		CString dir = CString("world/") << dirNoWild;
		CString wildcard = config.remove(0, dirNoWild.length());

		// Find out which file system to add it to.
		CFileSystem* fs = getFileSystemByType(type);

		// Add it to the appropriate file system.
		if (fs != nullptr)
		{
			fs->addDir(dir, wildcard);
			serverlog.out("[%s]        adding %s [%s] to %s\n", name.text(), dir.text(), wildcard.text(), type.text());
		}
		filesystem[0].addDir(dir, wildcard);
	}
}

int TServer::loadConfigFiles()
{
	// TODO(joey): /reloadconfig reloads this, but things like server flags, weapons and npcs probably shouldn't be reloaded.
	//	Move them out of here?
	serverlog.out("[%s] :: Loading server configuration...\n", name.text());

	// Load Settings
	serverlog.out("[%s]      Loading settings...\n", name.text());
	loadSettings();

	// Load Admin Settings
	serverlog.out("[%s]      Loading admin settings...\n", name.text());
	loadAdminSettings();

	// Load allowed versions.
	serverlog.out("[%s]      Loading allowed client versions...\n", name.text());
	loadAllowedVersions();

	// Load folders config and file system.
	serverlog.out("[%s]      Folder config: ", name.text());
	if ( !settings.getBool("nofoldersconfig", false))
	{
		serverlog.append("ENABLED\n");
	} else serverlog.append("disabled\n");
	serverlog.out("[%s]      Loading file system...\n", name.text());
	loadFileSystem();

	// Load server flags.
	serverlog.out("[%s]      Loading serverflags.txt...\n", name.text());
	loadServerFlags();

	// Load server message.
	serverlog.out("[%s]      Loading config/servermessage.html...\n", name.text());
	loadServerMessage();

	// Load IP bans.
	serverlog.out("[%s]      Loading config/ipbans.txt...\n", name.text());
	loadIPBans();

	// Load weapons.
	serverlog.out("[%s]      Loading weapons...\n", name.text());
	loadWeapons(true);

	// Load classes.
	serverlog.out("[%s]      Loading classes...\n", name.text());
	loadClasses(true);

	// Load maps.
	serverlog.out("[%s]      Loading maps...\n", name.text());
	loadMaps(true);

#ifdef V8NPCSERVER
	// Load database npcs.
	serverlog.out("[%s]      Loading npcs...\n", name.text());
	loadNpcs(true);
#endif

	// Load translations.
	serverlog.out("[%s]      Loading translations...\n", name.text());
	loadTranslations();

	// Load word filter.
	serverlog.out("[%s]      Loading word filter...\n", name.text());
	loadWordFilter();

	return 0;
}

void TServer::loadSettings()
{
	if (!settings.isOpened())
	{
		settings.setSeparator("=");
		settings.loadFile(CString() << serverpath << "config/serveroptions.txt");
		if ( !settings.isOpened())
			serverlog.out("[%s] ** [Error] Could not open config/serveroptions.txt.  Will use default config.\n", name.text());
	}

	// Load status list.
	statusList = settings.getStr("playerlisticons", "Online,Away,DND,Eating,Hiding,No PMs,RPing,Sparring,PKing").tokenize(",");

	// Load staff list
	staffList = settings.getStr("staff").tokenize(",");

	// Send our ServerHQ info in case we got changed the staffonly setting.
	getServerList()->sendServerHQ();
}

void TServer::loadAdminSettings()
{
	adminsettings.setSeparator("=");
	adminsettings.loadFile(CString() << serverpath << "config/adminconfig.txt");
	if (!adminsettings.isOpened())
		serverlog.out("[%s] ** [Error] Could not open config/adminconfig.txt.  Will use default config.\n", name.text());
	else getServerList()->sendServerHQ();
}

void TServer::loadAllowedVersions()
{
	CString versions;
	versions.load(CString() << serverpath << "config/allowedversions.txt");
	versions = removeComments(versions);
	versions.removeAllI("\r");
	versions.removeAllI("\t");
	versions.removeAllI(" ");
	allowedVersions = versions.tokenize("\n");
	allowedVersionString.clear();
	for (auto & allowedVersion : allowedVersions)
	{
		if (!allowedVersionString.isEmpty())
			allowedVersionString << ", ";

		int loc = allowedVersion.find(":");
		if (loc == -1)
			allowedVersionString << getVersionString(allowedVersion, PLTYPE_ANYCLIENT);
		else
		{
			CString s = allowedVersion.subString(0, loc);
			CString f = allowedVersion.subString(loc + 1);
			int vid = getVersionID(s);
			int vid2 = getVersionID(f);
			if (vid != -1 && vid2 != -1)
				allowedVersionString << getVersionString(s, PLTYPE_ANYCLIENT) << " - " << getVersionString(f, PLTYPE_ANYCLIENT);
		}
	}
}

void TServer::loadFileSystem()
{
	for (auto & i : filesystem)
		i.clear();
	filesystem_accounts.clear();
	filesystem_accounts.addDir("accounts");
	if ( settings.getBool("nofoldersconfig", false))
		loadAllFolders();
	else
		loadFolderConfig();
}

void TServer::loadServerFlags()
{
	std::vector<CString> lines = CString::loadToken(CString() << serverpath << "serverflags.txt", "\n", true);
	for (auto & line : lines)
		this->setFlag(line, false);
}

void TServer::loadServerMessage()
{
	servermessage.load(CString() << serverpath << "config/servermessage.html");
	servermessage.removeAllI("\r");
	servermessage.replaceAllI("\n", " ");
}

void TServer::loadIPBans()
{
	ipBans = CString::loadToken(CString() << serverpath << "config/ipbans.txt", "\n", true);
}

void TServer::loadClasses(bool print)
{
	CFileSystem scriptFS(this);
	scriptFS.addDir("scripts", "*.txt");
	const std::map<CString, CString> &scriptFileList = scriptFS.getFileList();
	for (auto & scriptFile : scriptFileList)
	{
		std::string className = scriptFile.first.subString(0, scriptFile.first.length() - 4).text();

		CString scriptData;
		scriptData.load(scriptFile.second);
		classList[className] = std::make_unique<TScriptClass>(this, className, scriptData.text());
	}
}

void TServer::loadWeapons(bool print)
{
	CFileSystem weaponFS(this);
	weaponFS.addDir("weapons", "weapon*.txt");
	CFileSystem bcweaponFS(this);
	bcweaponFS.addDir("weapon_bytecode", "*");
	const std::map<CString, CString> &weaponFileList = weaponFS.getFileList();
	for (auto & weaponFile : weaponFileList)
	{
		TWeapon *weapon = TWeapon::loadWeapon(weaponFile.first, this);
		if (weapon == nullptr) continue;
		if (!weapon->hasBytecode())
			weapon->setModTime(weaponFS.getModTime(weaponFile.first));
		else
			weapon->setModTime(bcweaponFS.getModTime(weapon->getByteCodeFile()));

		// Check if the weapon exists.
		if (weaponList.find(weapon->getName()) == weaponList.end())
		{
			weaponList[weapon->getName()] = weapon;
			if (print) serverlog.out("[%s]        %s\n", name.text(), weapon->getName().text());
		}
		else
		{
			// If the weapon exists, and the version on disk is newer, reload it.
			TWeapon* w = weaponList[weapon->getName()];
			if (w->getModTime() < weapon->getModTime())
			{
				delete w;
				weaponList[weapon->getName()] = weapon;
				updateWeaponForPlayers(weapon);
				if (print) {
					serverlog.out("[%s]        %s [updated]\n", name.text(), weapon->getName().text());

					TServer::sendPacketTo(PLTYPE_ANYRC, CString() >> (char)PLO_RC_CHAT << "Server: Updated weapon " << weapon->getName().text() << " ");
				}
			}
			else
			{
				// TODO(joey): even though were deleting the weapon because its skipped, its still queuing its script action
				//	and attempting to execute it. Technically the code needs to be run again though, will fix soon.
				if (print) serverlog.out("[%s]        %s [skipped]\n", name.text(), weapon->getName().text());
				delete weapon;
			}
		}
	}

	// Add the default weapons.
	if (weaponList.find("bow") == weaponList.end())	weaponList["bow"] = new TWeapon(this, TLevelItem::getItemId("bow"));
	if (weaponList.find("bomb") == weaponList.end()) weaponList["bomb"] = new TWeapon(this, TLevelItem::getItemId("bomb"));
	if (weaponList.find("superbomb") == weaponList.end()) weaponList["superbomb"] = new TWeapon(this, TLevelItem::getItemId("superbomb"));
	if (weaponList.find("fireball") == weaponList.end()) weaponList["fireball"] = new TWeapon(this, TLevelItem::getItemId("fireball"));
	if (weaponList.find("fireblast") == weaponList.end()) weaponList["fireblast"] = new TWeapon(this, TLevelItem::getItemId("fireblast"));
	if (weaponList.find("nukeshot") == weaponList.end()) weaponList["nukeshot"] = new TWeapon(this, TLevelItem::getItemId("nukeshot"));
	if (weaponList.find("joltbomb") == weaponList.end()) weaponList["joltbomb"] = new TWeapon(this, TLevelItem::getItemId("joltbomb"));
}

void TServer::loadMaps(bool print)
{
	// Remove existing maps.
	for (auto i = mapList.begin(); i != mapList.end(); )
	{
		TMap* map = *i;
		for (auto & player : playerList)
		{
			if (player->getMap() == map)
				player->setMap(0);
		}
		delete map;
		i = mapList.erase(i);
	}

	// Load gmaps.
	std::vector<CString> gmaps = settings.getStr("gmaps").guntokenize().tokenize("\n");
	for (CString & gmapName : gmaps)
	{
		// Check for blank lines.
		if ( gmapName == "\r") continue;

		// Load the gmap.
		TMap* gmap = new TMap(MAPTYPE_GMAP);

		if (gmapName.right(5) != ".gmap") {
			gmapName << ".gmap";
		}

		if ( !gmap->load(CString() << gmapName, this))
		{
			if (print) serverlog.out(CString() << "[" << name << "] " << "** [Error] Could not load " << gmapName << ".gmap" << "\n");
			delete gmap;
			continue;
		}

		if (print) serverlog.out("[%s]        [gmap] %s\n", name.text(), gmapName.text());
		mapList.push_back(gmap);
	}

	// Load bigmaps.
	std::vector<CString> bigmaps = settings.getStr("maps").guntokenize().tokenize("\n");
	for (auto & i : bigmaps)
	{
		// Check for blank lines.
		if (i == "\r") continue;

		// Load the bigmap.
		TMap* bigmap = new TMap(MAPTYPE_BIGMAP);
		if ( !bigmap->load(i.trim(), this))
		{
			if (print) serverlog.out(CString() << "[" << name << "] " << "** [Error] Could not load " << i << "\n");
			delete bigmap;
			continue;
		}

		if (print) serverlog.out("[%s]        [bigmap] %s\n", name.text(), i.text());
		mapList.push_back(bigmap);
	}

	// Load group maps.
	std::vector<CString> groupmaps = settings.getStr("groupmaps").guntokenize().tokenize("\n");
	for (auto & groupmap : groupmaps)
	{
		// Check for blank lines.
		if (groupmap == "\r") continue;

		// Determine the type of map we are loading.
		CString ext(getExtension(groupmap));
		ext.toLowerI();

		// Create the new map based on the file extension.
		TMap* gmap = nullptr;
		if (ext == ".txt")
			gmap = new TMap(MAPTYPE_BIGMAP, true);
		else if (ext == ".gmap")
			gmap = new TMap(MAPTYPE_GMAP, true);
		else continue;

		// Load the map.
		if ( !gmap->load(CString() << groupmap, this))
		{
			if (print) serverlog.out(CString() << "[" << name << "] " << "** [Error] Could not load " << groupmap << "\n");
			delete gmap;
			continue;
		}

		if (print) serverlog.out("[%s]        [group map] %s\n", name.text(), groupmap.text());
		mapList.push_back(gmap);
	}
}

#ifdef V8NPCSERVER
void TServer::loadNpcs(bool print)
{
	CFileSystem npcFS(this);
	npcFS.addDir("npcs", "npc*.txt");
	const std::map<CString, CString> &npcFileList = npcFS.getFileList();
	for (auto it = npcFileList.begin(); it != npcFileList.end(); ++it)
	{
		bool loaded = false;

		// Create the npc
		TNPC *newNPC = new TNPC("", "", 30, 30.5, this, nullptr, false);
		if (newNPC->loadNPC((*it).second))
		{
			int npcId = newNPC->getId();
			if (npcId < 1000)
			{
				printf("Database npcs must be greater than 1000\n");
			}
			else if (npcId < npcIds.size() && npcIds[npcId] != 0)
			{
				printf("Error creating database npc: Id is in use!\n");
			}
			else
			{
				// Assign id to npc
				if (npcIds.size() <= npcId)
					npcIds.resize((size_t)npcId + 10);

				npcIds[npcId] = newNPC;
				npcList.push_back(newNPC);
				assignNPCName(newNPC, newNPC->getName());

				// Add npc to level
				TLevel *level = newNPC->getLevel();
				if (level)
					level->addNPC(newNPC);

				loaded = true;
			}
		}

		if (!loaded)
			delete newNPC;
	}
}
#endif

void TServer::loadTranslations()
{
	this->TS_Reload();
}

void TServer::loadWordFilter()
{
	wordFilter.load(CString() << serverpath << "config/rules.txt");
}

void TServer::saveServerFlags()
{
	CString out;
	for (auto & mServerFlag : mServerFlags)
		out << mServerFlag.first << "=" << mServerFlag.second << "\r\n";
	out.save(CString() << serverpath << "serverflags.txt");
}

void TServer::saveWeapons()
{
	CFileSystem weaponFS(this);
	weaponFS.addDir("weapons", "weapon*.txt");
	const std::map<CString, CString>& weaponFileList = weaponFS.getFileList();

	for (auto & weapon : weaponList)
	{
		TWeapon *weaponObject = weapon.second;
		if (weaponObject->isDefault())
			continue;

		// TODO(joey): add a function to weapon to get the filename?
		CString weaponFile = CString("weapon") << weapon.first << ".txt";
		time_t mod = weaponFS.getModTime(weaponFile);
		if (weaponObject->getModTime() > mod)
		{
			// The weapon in memory is newer than the weapon on disk.  Save it.
			weaponObject->saveWeapon();
			weaponFS.setModTime(weaponFS.find(weaponFile), weaponObject->getModTime());
		}
	}
}

#ifdef V8NPCSERVER

void TServer::saveNpcs()
{
	for (auto it = npcList.begin(); it != npcList.end(); ++it)
	{
		TNPC *npc = *it;
		if (npc->getPersist())
			npc->saveNPC();
	}
}

std::vector<std::pair<double, std::string>> TServer::calculateNpcStats()
{
	std::vector<std::pair<double, std::string>> script_profiles;

	// Iterate npcs
	for (auto it = npcList.begin(); it != npcList.end(); ++it)
	{
		TNPC *npc = *it;
		ScriptExecutionContext& context = npc->getExecutionContext();
		std::pair<unsigned int, double> executionData = context.getExecutionData();
		if (executionData.second > 0.0)
		{
			std::string npcName = npc->getName();
			if (npcName.empty())
				npcName = "Level npc " + std::to_string(npc->getId());

			TLevel *npcLevel = npc->getLevel();
			if (npcLevel != nullptr) {
				npcName.append(" (in level ").append(npcLevel->getLevelName().text()).
					append(" at pos (").append(CString((float)npc->getPixelX() / 16.0f).text()).
					append(", ").append(CString((float)npc->getPixelY() / 16.0f).text()).append(")");
			}

			script_profiles.push_back(std::make_pair(executionData.second, npcName));
		}
	}

	// Iterate weapons
	for (auto it = weaponList.begin(); it != weaponList.end(); ++it)
	{
		TWeapon *weapon = (*it).second;
		ScriptExecutionContext& context = weapon->getExecutionContext();
		std::pair<unsigned int, double> executionData = context.getExecutionData();

		if (executionData.second > 0.0)
		{
			std::string weaponName("Weapon ");
			weaponName.append((*it).first.text());
			script_profiles.push_back(std::make_pair(executionData.second, weaponName));
		}
	}

	std::sort(script_profiles.rbegin(), script_profiles.rend());
	return script_profiles;
}

void TServer::reportScriptException(const ScriptRunError& error)
{
	std::string error_message = error.getErrorString();
	sendToNC(error_message);
	getScriptLog().out(error_message + "\n");
}

void TServer::reportScriptException(const std::string& error_message)
{
	sendToNC(error_message);
	getScriptLog().out(error_message + "\n");
}

#endif

/////////////////////////////////////////////////////

TPlayer * TServer::getPlayer(const unsigned short id) const
{
	if (id >= (unsigned short)playerIds.size())
		return nullptr;

	return playerIds[id];
}

TPlayer* TServer::getPlayer(const unsigned short id, int type) const
{
	if (id >= (unsigned short)playerIds.size())
		return nullptr;

	if (playerIds[id] == nullptr)
		return nullptr;

	// Check if its the type of player we are looking for
	if (!(playerIds[id]->getType() & type))
		return nullptr;

	return playerIds[id];
}

TPlayer* TServer::getPlayer(const CString& account, int type) const
{
	for (auto i : playerList)
	{
		TPlayer *player = (TPlayer*)i;
		if (player == 0)
			continue;

		// Check if its the type of player we are looking for
		if (!(player->getType() & type))
			continue;

		// Compare account names.
		if (player->getAccountName().toLower() == account.toLower())
			return player;
	}

	return nullptr;
}

/*
TPlayer* TServer::getPlayer(const unsigned short id, bool includeRC) const
{
	if (id >= (unsigned short)playerIds.size()) return 0;
	if (playerIds[id] == nullptr) return 0;
	if (!includeRC && playerIds[id]->isControlClient()) return 0;
	if (playerIds[id]->isHiddenClient()) return 0;
	return playerIds[id];
}

TPlayer* TServer::getPlayer(const CString& account, bool includeRC) const
{
	for (std::vector<TPlayer *>::const_iterator i = playerList.begin(); i != playerList.end(); ++i)
	{
		TPlayer *player = (TPlayer*)*i;
		if (player == 0 || player->isHiddenClient())
			continue;

		if (!includeRC && player->isRC())
			continue;

		// Compare account names.
		if (player->getAccountName().toLower() == account.toLower())
			return player;
	}
	return 0;
}

TPlayer* TServer::getRC(const unsigned short id) const
{
	if (id >= (unsigned short)playerIds.size()) return 0;
	if (playerIds[id] == nullptr) return 0;
	if (!playerIds[id]->isRC()) return 0;
	return playerIds[id];
}

TPlayer* TServer::getRC(const CString& account) const
{
	for (std::vector<TPlayer *>::const_iterator i = playerList.begin(); i != playerList.end(); ++i)
	{
		TPlayer *player = (TPlayer*)*i;
		if (player == 0 || !player->isRC())
			continue;

		// Compare account names.
		if (player->getAccountName().toLower() == account.toLower())
			return player;
	}
	return 0;
}
*/

TLevel* TServer::getLevel(const CString& pLevel)
{
	return TLevel::findLevel(pLevel, this);
}

TMap* TServer::getMap(const CString& name) const
{
	for (auto map : mapList)
	{
			if (map->getMapName() == name)
			return map;
	}
	return nullptr;
}

TMap* TServer::getMap(const TLevel* pLevel) const
{
	if (pLevel == 0) return 0;

	for (auto pMap : mapList)
	{
			if (pMap->isLevelOnMap(pLevel->getLevelName()))
			return pMap;
	}
	return nullptr;
}

TWeapon* TServer::getWeapon(const CString& name)
{
	return (weaponList.find(name) != weaponList.end() ? weaponList[name] : 0);
}

CString TServer::getFlag(const std::string& pFlagName)
{
#ifdef V8NPCSERVER
	if (mServerFlags.find(pFlagName) != mServerFlags.end())
		return mServerFlags[pFlagName];
	return "";
#else
	return mServerFlags[pFlagName];
#endif
}

CFileSystem* TServer::getFileSystemByType(CString& type)
{
	// Find out the filesystem.
	int fs = -1;
	int j = 0;
	while (filesystemTypes[j] != 0)
	{
		if (type.comparei(CString(filesystemTypes[j])))
		{
			fs = j;
			break;
		}
		++j;
	}

	if (fs == -1) return 0;
	return &filesystem[fs];
}

#ifdef V8NPCSERVER
void TServer::assignNPCName(TNPC *npc, const std::string& name)
{
	std::string newName = name;
	int num = 0;
	while (npcNameList.find(newName) != npcNameList.end())
		newName = name + std::to_string(++num);

	npc->setName(newName);
	npcNameList[newName] = npc;
}

void TServer::removeNPCName(TNPC *npc)
{
	auto npcIter = npcNameList.find(npc->getName());
	if (npcIter != npcNameList.end())
		npcNameList.erase(npcIter);
}

TNPC* TServer::addServerNpc(int npcId, float pX, float pY, TLevel *pLevel, bool sendToPlayers)
{
	// Force database npc ids to be >= 1000
	if (npcId < 1000)
	{
		printf("Database npcs need to be greater than 1000\n");
		return nullptr;
	}

	// Make sure the npc id isn't in use
	if (npcId < npcIds.size() && npcIds[npcId] != 0)
	{
		printf("Error creating database npc: Id is in use!\n");
		return nullptr;
	}

	// Create the npc
	TNPC* newNPC = new TNPC("", "", pX, pY, this, pLevel, false);
	newNPC->setId(npcId);
	npcList.push_back(newNPC);

	if (npcIds.size() <= npcId)
		npcIds.resize((size_t)npcId + 10);
	npcIds[npcId] = newNPC;

	// Add the npc to the level
	if (pLevel)
		pLevel->addNPC(newNPC);

	// Send the NPC's props to everybody in range.
	if (sendToPlayers)
	{
		CString packet = CString() >> (char)PLO_NPCPROPS >> (int)newNPC->getId() << newNPC->getProps(0);

		// Send to level.
		TMap* map = getMap(pLevel);
		sendPacketToLevel(packet, map, pLevel, 0, true);
	}

	return newNPC;
}

void TServer::handlePM(TPlayer * player, const CString & message)
{
	if (!mPmHandlerNpc)
	{
		CString npcServerMsg;
		npcServerMsg = "I am the npcserver for\nthis game server. Almost\nall npc actions are controlled\nby me.";
		player->sendPacket(CString() >> (char)PLO_PRIVATEMESSAGE >> (short)mNpcServer->getId() << "\"\"," << npcServerMsg.gtokenize());
		return;
	}

	// TODO(joey): This sets the first argument as the npc object, so we can't use it here for now.
	//mPmHandlerNpc->queueNpcEvent("npcserver.playerpm", true, player->getScriptObject(), std::string(message.text()));

	printf("Msg: %s\n", std::string(message.text()).c_str());

	mPmHandlerNpc->getExecutionContext().addAction(mScriptEngine.CreateAction("npcserver.playerpm", player->getScriptObject(), std::string(message.text())));
	mScriptEngine.RegisterNpcUpdate(mPmHandlerNpc);
}

void TServer::setPMFunction(TNPC *npc, IScriptFunction *function)
{
	if (npc == nullptr || function == nullptr)
	{
		mPmHandlerNpc = nullptr;
		mScriptEngine.removeCallBack("npcserver.playerpm");
		return;
	}

	mScriptEngine.setCallBack("npcserver.playerpm", function);
	mPmHandlerNpc = npc;
}

#endif

TNPC* TServer::addNPC(const CString& pImage, const CString& pScript, float pX, float pY, TLevel* pLevel, bool pLevelNPC, bool sendToPlayers)
{
	// New Npc
	TNPC* newNPC = new TNPC(pImage, pScript, pX, pY, this, pLevel, pLevelNPC);
	npcList.push_back(newNPC);

	// Assign NPC Id
	bool assignedId = false;
	for (unsigned int i = 10000; i < npcIds.size(); ++i)
	{
		if (npcIds[i] == 0)
		{
			npcIds[i] = newNPC;
			newNPC->setId(i);
			assignedId = true;
			break;
		}
	}

	// Assign NPC Id
	if ( !assignedId )
	{
		newNPC->setId(npcIds.size());
		npcIds.push_back(newNPC);
	}

	// Send the NPC's props to everybody in range.
	if (sendToPlayers)
	{
		CString packet = CString() >> (char)PLO_NPCPROPS >> (int)newNPC->getId() << newNPC->getProps(0);

		// Send to level.
		TMap* map = getMap(pLevel);
		sendPacketToLevel(packet, map, pLevel, 0, true);
	}

	return newNPC;
}

bool TServer::deleteNPC(const unsigned int pId, bool eraseFromLevel)
{
	// Grab the NPC.
	TNPC* npc = getNPC(pId);
	if (npc == nullptr) return false;

	return deleteNPC(npc, eraseFromLevel);
}

bool TServer::deleteNPC(TNPC* npc, bool eraseFromLevel)
{
	if (npc == nullptr) return false;
	if (npc->getId() >= npcIds.size()) return false;

	// Remove the NPC from all the lists.
	npcIds[npc->getId()] = nullptr;

	for ( auto npcL = npcList.begin(); npcL != npcList.end(); )
	{
		if ((*npcL) == npc)
			npcL = npcList.erase(npcL);
		else ++npcL;
	}

	TLevel *level = npc->getLevel();

	if (level)
	{
		// Remove the NPC from the level
		if (eraseFromLevel)
			level->removeNPC(npc);

		// Tell the clients to delete the NPC.
		bool isOnMap = level->getMap() != nullptr;
		CString tmpLvlName = (isOnMap ? level->getMap()->getMapName() : level->getLevelName());

		for (auto p : playerList)
		{
				if (p->isControlClient()) continue;

			if (isOnMap || p->getVersion() < CLVER_2_1)
				p->sendPacket(CString() >> (char)PLO_NPCDEL >> (int)npc->getId());
			else
				p->sendPacket(CString() >> (char)PLO_NPCDEL2 >> (char)tmpLvlName.length() << tmpLvlName >> (int)npc->getId());
		}
	}

#ifdef V8NPCSERVER
	// TODO(joey): Need to deal with illegal characters
	// If we persist this npc, delete the file  [ maybe should add a parameter if we should remove the npc from disk ]
	if (npc->getPersist())
	{
		CString filePath = getServerPath() << "npcs/npc" << npc->getName() << ".txt";
		CFileSystem::fixPathSeparators(filePath);
		remove(filePath.text());
	}

	if (!npc->isLevelNPC())
	{
		// Remove npc name assignment
		if (!npc->getName().empty())
			removeNPCName(npc);

		// If this is the npc that handles pms, clear it
		if (mPmHandlerNpc == npc)
			mPmHandlerNpc = nullptr;
	}
#endif

	// Delete the NPC from memory.
	delete npc;

	return true;
}

bool TServer::deleteClass(const std::string& className)
{
	auto classIter = classList.find(className);
	if (classIter == classList.end())
		return false;

	classList.erase(classIter);
	CString filePath = getServerPath() << "scripts/" << className << ".txt";
	CFileSystem::fixPathSeparators(filePath);
	remove(filePath.text());

	return true;
}

void TServer::updateClass(const std::string& className, const std::string& classCode)
{
	classList[className] = std::make_unique<TScriptClass>(this, className, classCode); 
	
	CString filePath = getServerPath() << "scripts/" << className << ".txt";
	CFileSystem::fixPathSeparators(filePath);

	CString fileData(classCode);
	fileData.save(filePath);
}

unsigned int TServer::getFreePlayerId()
{
	unsigned int newId = 0;
	for (unsigned int i = 2; i < playerIds.size(); ++i)
	{
		if (playerIds[i] == nullptr)
		{
			newId = i;
			i = playerIds.size();
		}
	}
	if (newId == 0)
	{
		newId = playerIds.size();
		playerIds.push_back(nullptr);
	}

	return newId;
}

bool TServer::addPlayer(TPlayer *player, unsigned int id)
{
    assert(player);

	// No id was passed, so we will fetch one
	if (id == UINT_MAX)
		id = getFreePlayerId();
	else if (playerIds.size() <= id)
		playerIds.resize((size_t)id + 10);
	else if (playerIds[id] != nullptr)
		return false;

	// Add them to the player list.
	player->setId(id);
	playerIds[id] = player;
	playerList.push_back(player);

#ifdef V8NPCSERVER
	// Create script object for player
	mScriptEngine.WrapObject(player);
#endif

	return true;
}

bool TServer::deletePlayer(TPlayer* player)
{
	if (player == nullptr)
		return true;

	// Add the player to the set of players to delete.
	if ( deletedPlayers.insert(player).second )
	{
		// Remove the player from the serverlist.
		getServerList()->deletePlayer(player);
	}

	return true;
}

void TServer::playerLoggedIn(TPlayer *player)
{
	// Tell the serverlist that the player connected.
	getServerList()->addPlayer(player);

#ifdef V8NPCSERVER
	// Send event to server that player is logging in
	for (auto it = npcNameList.begin(); it != npcNameList.end(); ++it)
	{
		TNPC *npcObject = (*it).second;
		npcObject->queueNpcAction("npc.playerlogin", player);
	}
#endif
}

void TServer::calculateServerTime()
{
	// timevar apparently subtracts 11078 days from time(0) then divides by 5.
	serverTime = ((unsigned int)time(nullptr) - 11078 * 24 * 60 * 60) / 5;
}

bool TServer::isIpBanned(const CString& ip)
{
	for (const auto& ipBan : ipBans)
	{
		if (ip.match(ipBan))
			return true;
	}

	return false;
}

bool TServer::isStaff(const CString& accountName)
{
	for (const auto& account : staffList)
	{
		if (accountName.toLower() == account.trim().toLower())
			return true;
	}

	return false;
}

void TServer::logToFile(const std::string & fileName, const std::string & message)
{
	CString fileNamePath = CString() << getServerPath().remove(0, getHomePath().length()) << "logs/";

	// Remove leading characters that may try to go up a directory
	int idx = 0;
	while (fileName[idx] == '.' || fileName[idx] == '/' || fileName[idx] == '\\')
		idx++;
	fileNamePath << fileName.substr(idx);

	CLog logFile(fileNamePath, true);
	logFile.open();
	logFile.out("\n%s\n", message.c_str());
}

/*
	TServer: Server Flag Management
*/
bool TServer::deleteFlag(const std::string& pFlagName, bool pSendToPlayers)
{
	if ( settings.getBool("dontaddserverflags", false))
		return false;

	std::unordered_map<std::string, CString>::iterator mServerFlag;
	if ((mServerFlag = mServerFlags.find(pFlagName)) != mServerFlags.end())
	{
		mServerFlags.erase(mServerFlag);
		if (pSendToPlayers)
            sendPacketToAll(CString() >> (char)PLO_FLAGDEL << pFlagName, nullptr);
		return true;
	}

	return false;
}

bool TServer::setFlag(CString pFlag, bool pSendToPlayers)
{
	std::string flagName = pFlag.readString("=").text();
	CString flagValue = pFlag.readString("");
	return this->setFlag(flagName, (flagValue.isEmpty() ? "1" : flagValue), pSendToPlayers);
}

bool TServer::setFlag(const std::string& pFlagName, const CString& pFlagValue, bool pSendToPlayers)
{
	if ( settings.getBool("dontaddserverflags", false))
		return false;

	// delete flag
	if (pFlagValue.isEmpty())
		return deleteFlag(pFlagName);

	// optimize
	if (mServerFlags[pFlagName] == pFlagValue)
		return true;

	// set flag
	if (settings.getBool("cropflags", true))
	{
		int fixedLength = 223 - 1 - (int)pFlagName.length();
		mServerFlags[pFlagName] = pFlagValue.subString(0, fixedLength);
	}
	else mServerFlags[pFlagName] = pFlagValue;

	if (pSendToPlayers)
        sendPacketToAll(CString() >> (char)PLO_FLAGSET << pFlagName << "=" << pFlagValue, nullptr);
	return true;
}

/*
	Packet-Sending Functions
*/
void TServer::sendPacketToAll(CString pPacket, TPlayer *pPlayer) const
{
	for (auto player : playerList)
	{
		if ( player == pPlayer || player->isNPCServer()) continue;

		player->sendPacket(pPacket);
	}
}

void TServer::sendPacketToLevel(CString pPacket, TMap* pMap, TLevel* pLevel, TPlayer* pPlayer, bool onlyGmap) const
{
	if (pMap == nullptr || (onlyGmap && pMap->getType() == MAPTYPE_BIGMAP))// || pLevel->isGroupLevel())
	{
		for (auto p : playerList)
		{
			if ( p == pPlayer || !p->isClient()) continue;
			if ( p->getLevel() == pLevel)
				p->sendPacket(pPacket);
		}
		return;
	}

	if (pLevel == 0) return;
	bool _groupMap = (pPlayer == 0 ? false : pPlayer->getMap()->isGroupMap());
	for (auto other : playerList)
	{
			if (!other->isClient() || other == pPlayer || other->getLevel() == 0) continue;
		if (_groupMap && pPlayer != 0 && pPlayer->getGroup() != other->getGroup()) continue;

		if (other->getMap() == pMap)
		{
			int sgmap[2] = {pMap->getLevelX(pLevel->getActualLevelName()), pMap->getLevelY(pLevel->getActualLevelName())};
			int ogmap[2];
			switch (pMap->getType())
			{
				case MAPTYPE_GMAP:
					ogmap[0] = other->getProp(PLPROP_GMAPLEVELX).readGUChar();
					ogmap[1] = other->getProp(PLPROP_GMAPLEVELY).readGUChar();
					break;

				default:
				case MAPTYPE_BIGMAP:
					ogmap[0] = pMap->getLevelX(other->getLevel()->getActualLevelName());
					ogmap[1] = pMap->getLevelY(other->getLevel()->getActualLevelName());
					break;
			}

			if (abs(ogmap[0] - sgmap[0]) < 2 && abs(ogmap[1] - sgmap[1]) < 2)
				other->sendPacket(pPacket);
		}
	}
}

void TServer::sendPacketToLevel(CString pPacket, TMap* pMap, TPlayer* pPlayer, bool sendToSelf, bool onlyGmap) const
{
	if (pPlayer->getLevel() == nullptr) return;

	if (pMap == nullptr || (onlyGmap && pMap->getType() == MAPTYPE_BIGMAP) || pPlayer->getLevel()->isSingleplayer())
	{
		TLevel* level = pPlayer->getLevel();
		if (level == nullptr) return;
		for (auto p : playerList)
		{
			if ((p == pPlayer && !sendToSelf) || !p->isClient()) continue;
			if ( p->getLevel() == level)
				p->sendPacket(pPacket);
		}
		return;
	}

	bool _groupMap = pPlayer->getMap()->isGroupMap();
	for (auto player : playerList)
	{
		if (!player->isClient()) continue;
		if ( player == pPlayer)
		{
			if (sendToSelf) pPlayer->sendPacket(pPacket);
			continue;
		}
		if ( player->getLevel() == nullptr) continue;
		if (_groupMap && pPlayer->getGroup() != player->getGroup()) continue;

		if ( player->getMap() == pMap)
		{
			int ogmap[2], sgmap[2];
			switch (pMap->getType())
			{
				case MAPTYPE_GMAP:
					ogmap[0] = player->getProp(PLPROP_GMAPLEVELX).readGUChar();
					ogmap[1] = player->getProp(PLPROP_GMAPLEVELY).readGUChar();
					sgmap[0] = pPlayer->getProp(PLPROP_GMAPLEVELX).readGUChar();
					sgmap[1] = pPlayer->getProp(PLPROP_GMAPLEVELY).readGUChar();
					break;

				default:
				case MAPTYPE_BIGMAP:
					ogmap[0] = pMap->getLevelX(player->getLevel()->getActualLevelName());
					ogmap[1] = pMap->getLevelY(player->getLevel()->getActualLevelName());
					sgmap[0] = pMap->getLevelX(pPlayer->getLevel()->getActualLevelName());
					sgmap[1] = pMap->getLevelY(pPlayer->getLevel()->getActualLevelName());
					break;
			}

			if (abs(ogmap[0] - sgmap[0]) < 2 && abs(ogmap[1] - sgmap[1]) < 2)
				player->sendPacket(pPacket);
		}
	}
}

void TServer::sendPacketTo(int who, CString pPacket, TPlayer* pPlayer) const
{
	for (auto player : playerList)
	{
		if ( player == pPlayer) continue;
		if ( player->getType() & who)
			player->sendPacket(pPacket);
	}
}

/*
	NPC-Server Functionality
*/
bool TServer::NC_AddWeapon(TWeapon *pWeaponObj)
{
	if (pWeaponObj == nullptr)
		return false;

	weaponList[pWeaponObj->getName()] = pWeaponObj;
	return true;
}

bool TServer::NC_DelWeapon(const CString& pWeaponName)
{
	// Definitions
	TWeapon *weaponObj = getWeapon(pWeaponName);
	if (weaponObj == 0 || weaponObj->isDefault())
		return false;

	// Delete from File Browser
	CString name = pWeaponName;
	name.replaceAllI("\\", "_");
	name.replaceAllI("/", "_");
	name.replaceAllI("*", "@");
	name.replaceAllI(":", ";");
	name.replaceAllI("?", "!");
	CString filePath = getServerPath() << "weapons/weapon" << name << ".txt";
	CFileSystem::fixPathSeparators(filePath);
	remove(filePath.text());

	// Delete from Memory
	mapRemove<CString, TWeapon *>(weaponList, weaponObj);
	delete weaponObj;

	// Delete from Players
	sendPacketTo(PLTYPE_ANYCLIENT, CString() >> (char)PLO_NPCWEAPONDEL << pWeaponName);
	return true;
}

void TServer::updateWeaponForPlayers(TWeapon *pWeapon)
{
	// Update Weapons
	for (auto player : playerList)
	{
		if (!player->isClient())
			continue;

		if (player->hasWeapon(pWeapon->getName()))
		{
			player->sendPacket(CString() >> (char)PLO_NPCWEAPONDEL << pWeapon->getName());
			player->sendPacket(CString() << pWeapon->getWeaponPacket());
		}
	}
}

/*
	Translation Functionality
*/
bool TServer::TS_Load(const CString& pLanguage, const CString& pFileName)
{
	// Load File
	std::vector<CString> fileData = CString::loadToken(pFileName, "\n", true);
	if (fileData.empty())
		return false;

	// Parse File
	std::vector<CString>::const_iterator cur, next;
	for (cur = fileData.begin(); cur != fileData.end(); ++cur)
	{
		if (cur->find("msgid") == 0)
		{
			CString msgId = cur->subString(7, cur->length() - 8);
			CString msgStr = "";
			bool isStr = false;

			++cur;
			while (cur != fileData.end())
			{
				// Make sure our string isn't empty.
				if (cur->isEmpty())
				{
					++cur;
					continue;
				}

				if ((*cur)[0] == '"' && (*cur)[cur->length() - 1] == '"')
				{
					CString str('\n');
					str.write(cur->subString(1, cur->length() - 2));
					(isStr ? msgStr.write(str) : msgId.write(str));
				}
				else if (cur->find("msgstr") == 0)
				{
					msgStr = cur->subString(8, cur->length() - 9);
					isStr = true;
				}
				else { --cur; break; }

				++cur;
			}

			mTranslationManager.add(pLanguage.text(), msgId.text(), msgStr.text());
		}

		if (cur == fileData.end())
			break;
	}

	return true;
}

CString TServer::TS_Translate(const CString& pLanguage, const CString& pKey)
{
	return mTranslationManager.translate(pLanguage.toLower().text(), pKey.text());
}

void TServer::TS_Reload()
{
	// Save Translations
	this->TS_Save();

	// Reset Translations
	mTranslationManager.reset();

	// Load Translation Folder
	CFileSystem translationFS(this);
	translationFS.addDir("translations", "*.po");

	// Load Each File
	const std::map<CString, CString> &temp = translationFS.getFileList();
	for (auto & i : temp)
		this->TS_Load(removeExtension(i.first), i.second);
}

void TServer::TS_Save()
{
	// Grab Translations
	std::map<std::string, STRMAP> *languages = mTranslationManager.getTranslationList();

	// Iterate each Language
	for (auto & language : *languages)
	{
		// Create Output
		CString output;

		// Iterate each Translation
		for (auto & lang : language.second)
		{
			output << "msgid ";
			std::vector<CString> sign = CString(lang.first.c_str()).removeAll("\r").tokenize("\n");
			for (auto & s : sign)
				output << "\"" << s << "\"\r\n";
			output << "msgstr ";
			if (!lang.second.empty())
			{
				std::vector<CString> lines = CString(lang.second.c_str()).removeAll("\r").tokenize("\n");
				for (auto & line : lines)
					output << "\"" << line << "\"\r\n";
			}
			else output << "\"\"\r\n";

			output << "\r\n";
		}

		// Save File
		output.trimRight().save(getServerPath() << "translations/" << language.first.c_str() << ".po");
	}
}
