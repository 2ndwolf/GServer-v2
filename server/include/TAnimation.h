//
// Created by marlon on 9/22/19.
//

#ifndef GS2EMU_TANIMATION_H
#define GS2EMU_TANIMATION_H

#include "TImage.h"
#include "TPlayer.h"
#include "TServer.h"

class TPlayer;

class TAnimationSprite
{
	public:
		TAnimationSprite(int pSprite, std::string pImage, int pX, int pY, int pW, int pH);
		~TAnimationSprite();

		inline void render(TPlayer * player, TServer * server, int pX, int pY);

	private:
		std::string img;
		int sprite, x, y, w, h;
};

class TAnimationAni
{
	public:
		TAnimationAni(TAnimationSprite *pImg, int pX, int pY);

		int x, y;

		TAnimationSprite *img;

		inline void render(TPlayer * player, TServer * server, int pX, int pY);
};

class TAnimation
{
	public:
		explicit TAnimation(CString pName, TServer * theServer);
		~TAnimation();

		bool loaded;
		CString name, real;

		bool load();
		void render(TPlayer* player, TServer * server, int pX, int pY, int pDir, int *pStep, float time);

		static TAnimation *find(const char *pName, TServer * theServer);
		TImage *findImage(char *pName, TServer * theServer);
	private:
		bool isLoop = false, isContinuous = false, isSingleDir = false;
		CString setBackTo;
		std::unordered_map<std::string, TImage *> imageList;
		std::unordered_map<int, TAnimationSprite *> animationSpriteList;
		std::map<int, std::map<int, TAnimationAni *>> animationAniList;
		TServer *server;
		float currentWait;
		float wait = 0.05f;
		int max;
		SDL_Thread *thread;
};



#include <SDL.h>
#include <SDL_image.h>
#include <CString.h>
#include "TImage.h"
#include "TServer.h"

#endif //GS2EMU_TANIMATION_H
