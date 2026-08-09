// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeliveryGame.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogDeliveryGame);

TAutoConsoleVariable<bool> CVarDGTrafficDebugDraw(
	TEXT("dg.TrafficDebugDraw"),
	true,
	TEXT("Master switch for traffic AI debug drawing (splines, aim lines, status text, volumes, signals). ")
	TEXT("0 silences everything regardless of per-actor bDrawDebug flags."),
	ECVF_Default);

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, DeliveryGame, "DeliveryGame");
