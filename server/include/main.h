#ifndef MAIN_H
#define MAIN_H

#include <SDL.h>
#include "CString.h"
#include "TImage.h"
#include "TAnimation.h"
#include <unordered_map>

bool parseArgs(int argc, char* argv[]);
void printHelp(const char* pname);
const CString getHomePath();
void shutdownServer(int sig);
static std::unordered_map<std::string, TImage *> imageList;
static std::unordered_map<std::string, TAnimation *> animations;


#endif // MAIN_H
