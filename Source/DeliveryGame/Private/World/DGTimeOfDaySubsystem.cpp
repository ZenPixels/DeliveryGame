// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/DGTimeOfDaySubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Delivery/DGDeliverySubsystem.h"
#include "DeliveryGame.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	/**
	 * Jump the clock from the console: `dg.SetHour 20`.
	 *
	 * A full cycle takes minutes of real time, so waiting for dusk to check something is a poor
	 * use of an evening. This is the fastest way to see any hour.
	 */
	static void SetHourCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogDeliveryGame, Warning, TEXT("dg.SetHour <0-24>"));
			return;
		}

		if (UDGTimeOfDaySubsystem* Time = World->GetSubsystem<UDGTimeOfDaySubsystem>())
		{
			Time->SetHour(FCString::Atof(*Args[0]));
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GSetHourCommand(
		TEXT("dg.SetHour"),
		TEXT("Set the island's time of day, 0-24. Example: dg.SetHour 20.5"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetHourCommand));
}

void UDGTimeOfDaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	CurrentHour = StartHour;
	ResolveLights(InWorld);
	UpdateSky();

	bWasNight = IsNight();
	bWasLateNight = IsLateNight();

	UE_LOG(LogDeliveryGame, Log, TEXT("Day %d begins at %s (a full cycle takes %.0f minutes)."),
		DayNumber, *GetTimeText().ToString(), DayLengthMinutes);
	OnDayStarted.Broadcast(DayNumber);
}

void UDGTimeOfDaySubsystem::ResolveLights(UWorld& InWorld)
{
	// Adopt whatever sun the level already has rather than spawning a competing one — its
	// authored intensity, colour and shadow settings are the artist's, and we only drive angle
	// and brightness.
	for (TActorIterator<ADirectionalLight> It(&InWorld); It; ++It)
	{
		ADirectionalLight* Light = *It;
		const UDirectionalLightComponent* Component =
			Light ? Cast<UDirectionalLightComponent>(Light->GetLightComponent()) : nullptr;
		if (!Component)
		{
			continue;
		}

		if (Component->IsUsedAsAtmosphereSunLight() || !SunLight.IsValid())
		{
			SunLight = Light;
			SunFullIntensity = Component->Intensity;
			if (Component->IsUsedAsAtmosphereSunLight())
			{
				break;
			}
		}
	}

	if (!SunLight.IsValid())
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("No directional light found; the sky will not follow the clock."));
		return;
	}

	// A moving sun needs a movable light; a Stationary one would keep its baked shadows.
	if (ADirectionalLight* Sun = SunLight.Get(); Sun->GetRootComponent() &&
		Sun->GetRootComponent()->Mobility != EComponentMobility::Movable)
	{
		Sun->SetMobility(EComponentMobility::Movable);
		UE_LOG(LogDeliveryGame, Warning, TEXT("%s was not Movable; switched so it can track the clock."),
			*Sun->GetName());
	}

	// Give the sun a disc you can actually watch rise. The physical 0.5 degrees is a pinprick;
	// widening it also softens shadows, which suits the low-poly look.
	if (UDirectionalLightComponent* SunComponent =
			Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
	{
		SunComponent->LightSourceAngle = SunDiscAngleDegrees;
		SunComponent->MarkRenderStateDirty();
	}

	if (!bSpawnMoonLight)
	{
		return;
	}

	// Spawned rather than placed so the level needs no edit. Registering it as the atmosphere's
	// *second* sun light is what lets the sky treat it as a moon.
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	ADirectionalLight* Moon = InWorld.SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), FTransform::Identity, SpawnParams);
	if (!Moon)
	{
		return;
	}

	Moon->SetMobility(EComponentMobility::Movable);
	if (UDirectionalLightComponent* MoonComponent = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
	{
		MoonComponent->SetIntensity(0.f);
		MoonComponent->SetLightColor(MoonColor);
		MoonComponent->bAtmosphereSunLight = true;
		MoonComponent->AtmosphereSunLightIndex = 1;

		// The disc is brightened independently of the illuminance: a light dim enough to feel
		// like moonlight would otherwise render a moon you can barely find.
		MoonComponent->LightSourceAngle = MoonDiscAngleDegrees;
		MoonComponent->AtmosphereSunDiskColorScale = MoonDiscBrightness;
		MoonComponent->MarkRenderStateDirty();
	}
	MoonLight = Moon;
}

void UDGTimeOfDaySubsystem::UpdateSky()
{
	ADirectionalLight* Sun = SunLight.Get();
	if (!Sun)
	{
		return;
	}

	// Pitch sweeps a full turn per day: 0 at 06:00 (light horizontal, sunrise), -90 at noon
	// (overhead), -180 at 18:00 (setting), -270 at midnight.
	const float SunPitch = -(((CurrentHour - 6.f) / 24.f) * 360.f);
	Sun->SetActorRotation(FRotator(SunPitch, SunYaw, 0.f));

	// Elevation from the light's own forward vector: sunlight points down while the sun is up.
	const float SunElevationDegrees =
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(-Sun->GetActorForwardVector().Z, -1.f, 1.f)));

	if (UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		// Fade out across the horizon instead of snapping off, so dusk has a hand-over rather
		// than a light switch.
		const float SunAlpha = FMath::Clamp(SunElevationDegrees / FMath::Max(SunFadeDegrees, 0.01f), 0.f, 1.f);
		SunComponent->SetIntensity(SunFullIntensity * SunAlpha);
	}

	if (ADirectionalLight* Moon = MoonLight.Get())
	{
		// Opposite the sun, and only worth anything once the sun has gone.
		Moon->SetActorRotation(FRotator(SunPitch + 180.f, SunYaw, 0.f));
		if (UDirectionalLightComponent* MoonComponent = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
		{
			const float MoonAlpha = FMath::Clamp(-SunElevationDegrees / FMath::Max(SunFadeDegrees, 0.01f), 0.f, 1.f);
			MoonComponent->SetIntensity(MoonIntensity * MoonAlpha);
		}
	}
}

void UDGTimeOfDaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bAdvanceTime && DayLengthMinutes > 0.f)
	{
		const float HoursPerSecond = 24.f / (DayLengthMinutes * 60.f);
		CurrentHour = FMath::Fmod(CurrentHour + DeltaTime * HoursPerSecond, 24.f);
	}

	UpdateSky();

	// Transitions fire once each, not every frame.
	const bool bNightNow = IsNight();
	if (bNightNow != bWasNight)
	{
		bWasNight = bNightNow;
		UE_LOG(LogDeliveryGame, Log, TEXT("%s at %s."),
			bNightNow ? TEXT("Nightfall") : TEXT("Sunrise"), *GetTimeText().ToString());
		(bNightNow ? OnNightfall : OnSunrise).Broadcast();
	}

	const bool bLateNow = IsLateNight();
	if (bLateNow && !bWasLateNight)
	{
		UE_LOG(LogDeliveryGame, Log, TEXT("It is %s — the player is out late."), *GetTimeText().ToString());
		OnLateNight.Broadcast();
	}
	bWasLateNight = bLateNow;

	DrawDebugHUD();
}

void UDGTimeOfDaySubsystem::DrawDebugHUD() const
{
#if !UE_BUILD_SHIPPING
	if (!GEngine || !CVarDGDebugHUD.GetValueOnGameThread())
	{
		return;
	}

	// Fixed keys so each line replaces itself instead of scrolling the screen.
	const FString Clock = FString::Printf(TEXT("Day %d   %s%s"),
		DayNumber, *GetTimeText().ToString(),
		IsLateNight() ? TEXT("   [LATE]") : (IsNight() ? TEXT("   [night]") : TEXT("")));
	GEngine->AddOnScreenDebugMessage(/*Key=*/9001, /*TimeToDisplay=*/0.f,
		IsNight() ? FColor(150, 170, 255) : FColor(255, 220, 130), Clock);

	if (const UWorld* World = GetWorld())
	{
		if (const UDGDeliverySubsystem* Delivery = World->GetSubsystem<UDGDeliverySubsystem>())
		{
			const FString Work = FString::Printf(TEXT("$%d   jobs %d/%d   offers %d%s"),
				Delivery->GetMoney(), Delivery->GetHeldJobCount(), Delivery->JobCapacity,
				Delivery->GetOffers().Num(),
				Delivery->IsOfferQueueBlocked() ? TEXT("   [queue blocked]") : TEXT(""));
			GEngine->AddOnScreenDebugMessage(9002, 0.f, FColor(180, 255, 180), Work);
		}
	}
#endif
}

FText UDGTimeOfDaySubsystem::GetTimeText() const
{
	const int32 Hours = FMath::Clamp(FMath::FloorToInt32(CurrentHour), 0, 23);
	const int32 Minutes = FMath::Clamp(FMath::FloorToInt32((CurrentHour - Hours) * 60.f), 0, 59);
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Hours, Minutes));
}

bool UDGTimeOfDaySubsystem::IsNight() const
{
	const ADirectionalLight* Sun = SunLight.Get();
	if (!Sun)
	{
		// No sun to ask: fall back to the clock.
		return CurrentHour < 6.f || CurrentHour >= 18.f;
	}
	return -Sun->GetActorForwardVector().Z <= 0.f;
}

bool UDGTimeOfDaySubsystem::IsLateNight() const
{
	return CurrentHour >= LateNightHour || CurrentHour < 4.f;
}

void UDGTimeOfDaySubsystem::SetHour(float NewHour)
{
	CurrentHour = FMath::Fmod(FMath::Max(NewHour, 0.f), 24.f);
	UpdateSky();
	bWasNight = IsNight();
	bWasLateNight = IsLateNight();
	UE_LOG(LogDeliveryGame, Log, TEXT("Time set to %s."), *GetTimeText().ToString());
}

void UDGTimeOfDaySubsystem::EndDay()
{
	const int32 Finished = DayNumber;
	OnDayEnded.Broadcast(Finished);

	++DayNumber;
	SetHour(StartHour);

	UE_LOG(LogDeliveryGame, Log, TEXT("Day %d ended; day %d begins at %s."),
		Finished, DayNumber, *GetTimeText().ToString());
	OnDayStarted.Broadcast(DayNumber);
}

TStatId UDGTimeOfDaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDGTimeOfDaySubsystem, STATGROUP_Tickables);
}

bool UDGTimeOfDaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
