// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DGTimeOfDaySubsystem.generated.h"

class ADirectionalLight;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDGDayEvent, int32, DayNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDGTimeEvent);

/**
 * The island's clock, and the sun that follows it.
 *
 * One authority for time: the sky reads from here, and so will the delivery day ("how much can
 * you pack into a day"), rent, shop hours and anything else that cares. Keeping the visual cycle
 * and the gameplay day on separate clocks is how you end up with a sunset at 2pm.
 *
 * The sky needs no assets to do this. SkyAtmosphere computes the sky from the sun's direction, so
 * rotating the light gives sunrise, golden hour, dusk and darkness for free, and the volumetric
 * clouds light themselves. Only stars and a moon disc would need art (see docs/ASSET_TODO.md).
 */
UCLASS()
class DELIVERYGAME_API UDGTimeOfDaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------ Clock

	/** Real-world minutes for a full 24 hour cycle. */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	float DayLengthMinutes = 5.f;

	/** Hour the player wakes up on each new day. Early enough to catch dawn. */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	float StartHour = 5.f;

	/** Past this hour the player is out late — the hook for the staying-up-too-late penalty. */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	float LateNightHour = 22.f;

	/** Stop the clock without stopping the world (the pause menu, cutscenes, debugging). */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	bool bAdvanceTime = true;

	UFUNCTION(BlueprintPure, Category = "Time")
	float GetCurrentHour() const { return CurrentHour; }

	UFUNCTION(BlueprintPure, Category = "Time")
	int32 GetDayNumber() const { return DayNumber; }

	/** 0 at midnight, 0.5 at midday — for driving curves and materials. */
	UFUNCTION(BlueprintPure, Category = "Time")
	float GetNormalizedTime() const { return CurrentHour / 24.f; }

	/** "08:32", for the phone's clock. */
	UFUNCTION(BlueprintPure, Category = "Time")
	FText GetTimeText() const;

	/** True while the sun is below the horizon. */
	UFUNCTION(BlueprintPure, Category = "Time")
	bool IsNight() const;

	/** True once past LateNightHour (or before dawn). */
	UFUNCTION(BlueprintPure, Category = "Time")
	bool IsLateNight() const;

	/** Jump the clock. Used by the console command and by sleeping/day transitions. */
	UFUNCTION(BlueprintCallable, Category = "Time")
	void SetHour(float NewHour);

	/** End the day: advances the day counter and wakes the player at StartHour. */
	UFUNCTION(BlueprintCallable, Category = "Time")
	void EndDay();

	// ----------------------------------------------------------------- Events

	/** A new day began (day number is the new one). */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FDGDayEvent OnDayStarted;

	/** The day was wrapped up (day number is the one just finished). */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FDGDayEvent OnDayEnded;

	/** The sun just went below the horizon. */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FDGTimeEvent OnNightfall;

	/** The sun just came up. */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FDGTimeEvent OnSunrise;

	/** Just crossed LateNightHour — you should be home by now. */
	UPROPERTY(BlueprintAssignable, Category = "Time")
	FDGTimeEvent OnLateNight;

	// -------------------------------------------------------------------- Sky

	/** Compass direction the sun travels along. Rotate for a different-feeling island. */
	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	float SunYaw = -30.f;

	/** Spawn a dim second directional light to serve as moonlight when the sun is down. */
	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	bool bSpawnMoonLight = true;

	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	float MoonIntensity = 0.35f;

	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.68f, 1.f, 1.f);

	/** How far above the horizon the sun reaches full strength, in degrees. */
	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	float SunFadeDegrees = 6.f;

	/**
	 * Angular size of the sun's disc in the sky, in degrees. The real sun is about 0.5, which is
	 * a barely-there dot; a low-poly island wants something you can actually see rising.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	float SunDiscAngleDegrees = 2.f;

	/** Angular size of the moon's disc. */
	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	float MoonDiscAngleDegrees = 3.f;

	/**
	 * Brightness of the moon's disc, separate from how much it lights the world. Values well
	 * above 1 make the moon clearly visible while moonlight stays dim enough to feel like night.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Time|Sky")
	FLinearColor MoonDiscBrightness = FLinearColor(14.f, 15.f, 20.f, 1.f);

	// ------------------------------------------------- UTickableWorldSubsystem

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	float CurrentHour = 8.f;
	int32 DayNumber = 1;

	/** Remembered so transitions fire once rather than every frame. */
	bool bWasNight = false;
	bool bWasLateNight = false;

	/** The scene's existing atmosphere sun. Its authored intensity is treated as full daylight. */
	TWeakObjectPtr<ADirectionalLight> SunLight;
	float SunFullIntensity = 3.f;

	TWeakObjectPtr<ADirectionalLight> MoonLight;

	void ResolveLights(UWorld& InWorld);

	/** Point the sun (and moon) at the current hour and fade them across the horizon. */
	void UpdateSky();

	/** Developer readout while the phone cannot show this yet. Stripped from shipping. */
	void DrawDebugHUD() const;
};
