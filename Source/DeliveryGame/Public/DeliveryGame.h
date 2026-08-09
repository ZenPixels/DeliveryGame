// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDeliveryGame, Log, All);

/**
 * Master switch for every piece of traffic-AI debug drawing (`dg.TrafficDebugDraw`).
 *
 * Per-actor bDrawDebug flags still choose *what* draws; this chooses whether anything does — one
 * console command silences the lot without touching instance flags. Shipping builds strip
 * DrawDebug* entirely regardless (ENABLE_DRAW_DEBUG=0), so this is a development convenience.
 */
extern TAutoConsoleVariable<bool> CVarDGTrafficDebugDraw;
