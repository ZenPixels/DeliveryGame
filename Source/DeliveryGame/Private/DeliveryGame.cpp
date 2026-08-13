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

TAutoConsoleVariable<bool> CVarDGDebugHUD(
	TEXT("dg.DebugHUD"),
	true,
	TEXT("On-screen developer readout: day, time of day, money, held jobs and offers. ")
	TEXT("0 hides it. Stripped from shipping builds."),
	ECVF_Default);

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, DeliveryGame, "DeliveryGame");
