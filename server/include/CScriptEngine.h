#pragma once

#ifndef CSCRIPTENGINE_H
#define CSCRIPTENGINE_H

#include <cassert>
#include <string>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ScriptBindings.h"
#include "ScriptAction.h"
#include "ScriptFactory.h"

#ifdef V8NPCSERVER
#include "V8ScriptWrappers.h"
#endif

class IScriptEnv;
class IScriptFunction;

class TNPC;
class TServer;
class TWeapon;

class CScriptEngine
{
public:
	CScriptEngine(TServer *server);
	~CScriptEngine();

	bool Initialize();
	void Cleanup(bool shutDown = false);
	void RunScripts(const std::chrono::high_resolution_clock::time_point& time);

	void ScriptWatcher();
	void StartScriptExecution(const std::chrono::high_resolution_clock::time_point& startTime);
	bool StopScriptExecution();

	TServer * getServer() const;
	IScriptEnv * getScriptEnv() const;
	IScriptObject<TServer> * getServerObject() const;

	bool ExecuteNpc(TNPC *npc);
	bool ExecuteWeapon(TWeapon *weapon);

	void RegisterNpcTimer(TNPC *npc);
	void RegisterNpcUpdate(TNPC *npc);
	void RegisterWeaponUpdate(TWeapon *weapon);

	void UnregisterNpcTimer(TNPC *npc);
	void UnregisterNpcUpdate(TNPC *npc);
	void UnregisterWeaponUpdate(TWeapon *weapon);

	// callbacks
	IScriptFunction * getCallBack(const std::string& callback) const;
	void removeCallBack(const std::string& callback);
	void setCallBack(const std::string& callback, IScriptFunction *cbFunc);

	// Compile script into a ScriptFunction
	IScriptFunction * CompileCache(const std::string& code, bool referenceCount = true);

	// Clear cache for code
	bool ClearCache(const std::string& code);

	// Clear cache for code, with a WrapperScript of Type T
	template<typename T>
	bool ClearCache(const std::string& code);

	template<class... Args>
	ScriptAction CreateAction(const std::string& action, Args... An);

	template<class T>
	IScriptObject<T> * WrapObject(T *obj) const;

	const ScriptRunError& getScriptError() const;

private:
	void runTimers(const std::chrono::high_resolution_clock::time_point& time);

	IScriptEnv *_env;
	IScriptFunction *_bootstrapFunction;
	IScriptObject<TServer> *_environmentObject;
	IScriptObject<TServer> *_serverObject;
	TServer *_server;

	std::chrono::high_resolution_clock::time_point lastScriptTimer;
	std::chrono::nanoseconds accumulator;

	// Script watcher
	std::atomic<bool> _scriptIsRunning;
	std::atomic<bool> _scriptWatcherRunning;
	std::chrono::high_resolution_clock::time_point _scriptStartTime;
	std::mutex _scriptWatcherLock;
	std::thread _scriptWatcherThread;

	std::unordered_map<std::string, IScriptFunction *> _cachedScripts;
	std::unordered_map<std::string, IScriptFunction *> _callbacks;
	std::unordered_set<TNPC *> _updateNpcs;
	std::unordered_set<TNPC *> _updateNpcsTimer;
	std::unordered_set<TWeapon *> _updateWeapons;
	std::unordered_set<IScriptFunction *> _deletedCallbacks;
};

inline void CScriptEngine::StartScriptExecution(const std::chrono::high_resolution_clock::time_point& startTime)
{
	{
		std::lock_guard<std::mutex> guard(_scriptWatcherLock);
		_scriptStartTime = startTime;
	}
	_scriptIsRunning.store(true);
}

inline bool CScriptEngine::StopScriptExecution()
{
	bool res = _scriptIsRunning.load();
	if (res)
		_scriptIsRunning.store(false);
	return res;
}

// Getters

inline TServer * CScriptEngine::getServer() const {
	return _server;
}

inline IScriptEnv * CScriptEngine::getScriptEnv() const {
	return _env;
}

inline IScriptObject<TServer> * CScriptEngine::getServerObject() const {
	return _serverObject;
}

inline IScriptFunction * CScriptEngine::getCallBack(const std::string& callback) const {
	auto it = _callbacks.find(callback);
	if (it != _callbacks.end())
		return it->second;

	return nullptr;
}

inline const ScriptRunError& CScriptEngine::getScriptError() const {
	return _env->getScriptError();
}

// Register scripts for processing

inline void CScriptEngine::RegisterNpcTimer(TNPC *npc) {
	_updateNpcsTimer.insert(npc);
}

inline void CScriptEngine::RegisterNpcUpdate(TNPC *npc) {
	_updateNpcs.insert(npc);
}

inline void CScriptEngine::RegisterWeaponUpdate(TWeapon *weapon) {
	_updateWeapons.insert(weapon);
}

// Unregister scripts from processing

inline void CScriptEngine::UnregisterWeaponUpdate(TWeapon *weapon) {
	_updateWeapons.erase(weapon);
}

inline void CScriptEngine::UnregisterNpcUpdate(TNPC *npc) {
	_updateNpcs.erase(npc);
}

inline void CScriptEngine::UnregisterNpcTimer(TNPC *npc) {
	_updateNpcsTimer.erase(npc);
}

//

template<class... Args>
ScriptAction CScriptEngine::CreateAction(const std::string& action, Args... An)
{
	// TODO(joey): This just creates an action, and leaves it up to the user to do something with this. Most-likely will be renamed, or changed.
	constexpr size_t Argc = (sizeof...(Args));
	assert(Argc > 0);

	SCRIPTENV_D("Server_RegisterAction:\n");
	SCRIPTENV_D("\tAction: %s\n", action.c_str());
	SCRIPTENV_D("\tArguments: %zu\n", Argc);

	auto funcIt = _callbacks.find(action);
	if (funcIt == _callbacks.end())
	{
		SCRIPTENV_D("Global::Server_RegisterAction: Callback not registered for %s\n", action.c_str());
		return ScriptAction{};
	}

	// Create an arguments object, and pass it to ScriptAction
	IScriptArguments *args = ScriptFactory::CreateArguments(_env, std::forward<Args>(An)...);
	assert(args);

	return ScriptAction(funcIt->second, args, action);
	//ScriptAction *newScriptAction = new ScriptAction(funcIt->second, args, action);
	//return newScriptAction;
}

template<class T>
inline IScriptObject<T> * CScriptEngine::WrapObject(T *obj) const
{
	SCRIPTENV_D("Begin Global::WrapObject()\n");

	// Wrap the object, and set the new script object on the original object
	IScriptObject<T> *wrappedObject = ScriptFactory::WrapObject(_env, ScriptConstructorId<T>::result, obj);
	obj->setScriptObject(wrappedObject);

	SCRIPTENV_D("End Global::WrapObject()\n\n");
	return wrappedObject;
}

template<typename T>
inline bool CScriptEngine::ClearCache(const std::string& code)
{
	return ClearCache(WrapScript<T>(code));
}


#endif
