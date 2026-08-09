// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGTrafficLightActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Traffic/DGTrafficSubsystem.h"

namespace
{
	/** First component whose name contains any of the given fragments, case-insensitively. */
	USceneComponent* FindComponentByNameFragments(const AActor& Actor, const TArray<FString>& Fragments)
	{
		TArray<USceneComponent*> Components;
		Actor.GetComponents<USceneComponent>(Components);

		for (USceneComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const FString Name = Component->GetName();
			for (const FString& Fragment : Fragments)
			{
				if (Name.Contains(Fragment, ESearchCase::IgnoreCase))
				{
					return Component;
				}
			}
		}

		return nullptr;
	}
}

ADGTrafficLightActor::ADGTrafficLightActor()
{
	// Ticks for the cycle timer, so unlike the other traffic actors this is not debug-only.
	PrimaryActorTick.bCanEverTick = true;

	// No components created here: the Blueprint owns them. See the class comment.
}

void ADGTrafficLightActor::ResolveComponents()
{
	SignalBox = FindComponentByClass<UBoxComponent>();
	if (!SignalBox)
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("%s has no Box Component, so it cannot stop anything. Add one to this Blueprint."),
			*GetName());
	}

	// "Yelllow" is spelled with three Ls on BP_Prop_Traffic_Light_Sm; match both spellings so the
	// lamp is still found if that typo is ever corrected.
	RedLight = FindComponentByNameFragments(*this, {TEXT("RedLight"), TEXT("Red Light"), TEXT("Red")});
	YellowLight = FindComponentByNameFragments(*this, {TEXT("Yelllow"), TEXT("Yellow")});
	GreenLight = FindComponentByNameFragments(*this, {TEXT("GreenLight"), TEXT("Green Light"), TEXT("Green")});
}

void ADGTrafficLightActor::BeginPlay()
{
	Super::BeginPlay();

	ResolveComponents();

	SignalState = StartingState;
	TimeInState = 0.f;
	TimeUntilStart = InitDelay;

	UpdateLampVisibility();

	if (SignalBox)
	{
		// Queries only. No overlap delegates are bound: vehicles poll IsActorInZone instead, so there
		// is no enter/exit event whose loss could strand a vehicle.
		SignalBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SignalBox->SetCollisionResponseToAllChannels(ECR_Overlap);
		SignalBox->SetGenerateOverlapEvents(true);
	}

	if (UDGTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UDGTrafficSubsystem>() : nullptr)
	{
		Traffic->RegisterLight(this);
	}
}

void ADGTrafficLightActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDGTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UDGTrafficSubsystem>() : nullptr)
	{
		Traffic->UnregisterLight(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool ADGTrafficLightActor::IsActorInZone(const AActor* Actor) const
{
	if (!Actor || !SignalBox)
	{
		return false;
	}

	// Horizontal footprint test in the volume's own space; the vertical axis is checked separately
	// and generously. The authored zones float ~2 m above the road, so a strict point-in-box test
	// misses every vehicle — their origins sit at axle height, below the box.
	const FVector Local = SignalBox->GetComponentTransform().InverseTransformPosition(Actor->GetActorLocation());
	const FVector Extent = SignalBox->GetUnscaledBoxExtent();

	if (FMath::Abs(Local.X) > Extent.X || FMath::Abs(Local.Y) > Extent.Y)
	{
		return false;
	}

	// Same road level? Generous enough for kerbs and suspension, tight enough that a future overpass
	// on the city map is not governed by the signal underneath it.
	const float VerticalGap = FMath::Abs(Actor->GetActorLocation().Z - SignalBox->GetComponentLocation().Z);
	return VerticalGap <= SignalBox->GetScaledBoxExtent().Z + 300.f;
}

float ADGTrafficLightActor::GetStopLineDistance(const FVector& From, const FVector& Forward) const
{
	if (!SignalBox)
	{
		return -1.f;
	}

	// Vertical gate first — a signal on another road level must not brake us.
	if (FMath::Abs(From.Z - SignalBox->GetComponentLocation().Z) > SignalBox->GetScaledBoxExtent().Z + 300.f)
	{
		return -1.f;
	}

	// 2D ray-vs-box in the volume's local space: does the vehicle's forward line actually enter this
	// zone? This is the lane filter — the opposite lane's pad and the cross street's pads sit off our
	// line and return a miss, so only the signal governing our own approach ever brakes us.
	const FTransform& BoxTransform = SignalBox->GetComponentTransform();
	const FVector LocalFrom = BoxTransform.InverseTransformPosition(From);
	FVector LocalDir = BoxTransform.InverseTransformVector(Forward);
	LocalDir.Z = 0.f;
	if (!LocalDir.Normalize())
	{
		return -1.f;
	}

	const FVector Extent = SignalBox->GetUnscaledBoxExtent();
	float TMin = 0.f;
	float TMax = TNumericLimits<float>::Max();

	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const float Origin = (Axis == 0) ? LocalFrom.X : LocalFrom.Y;
		const float Dir = (Axis == 0) ? LocalDir.X : LocalDir.Y;
		const float E = (Axis == 0) ? Extent.X : Extent.Y;

		if (FMath::Abs(Dir) < KINDA_SMALL_NUMBER)
		{
			if (FMath::Abs(Origin) > E)
			{
				return -1.f; // travelling parallel to this slab, outside it
			}
			continue;
		}

		float T0 = (-E - Origin) / Dir;
		float T1 = (E - Origin) / Dir;
		if (T0 > T1)
		{
			Swap(T0, T1);
		}

		TMin = FMath::Max(TMin, T0);
		TMax = FMath::Min(TMax, T1);
		if (TMin > TMax)
		{
			return -1.f;
		}
	}

	// Local distances are distorted by the box's non-uniform scale, so convert through world space.
	const FVector WorldHit = BoxTransform.TransformPosition(LocalFrom + LocalDir * TMin);
	return FVector::DotProduct(WorldHit - From, Forward);
}

void ADGTrafficLightActor::UpdateLampVisibility()
{
	if (RedLight)
	{
		RedLight->SetVisibility(SignalState == EDGSignalState::Red, /*bPropagateToChildren=*/true);
	}
	if (YellowLight)
	{
		YellowLight->SetVisibility(SignalState == EDGSignalState::Yellow, /*bPropagateToChildren=*/true);
	}
	if (GreenLight)
	{
		GreenLight->SetVisibility(SignalState == EDGSignalState::Green, /*bPropagateToChildren=*/true);
	}
}

float ADGTrafficLightActor::GetCurrentStateDuration() const
{
	switch (SignalState)
	{
	case EDGSignalState::Green:  return GreenDuration;
	case EDGSignalState::Yellow: return YellowDuration;
	default:                     return RedDuration;
	}
}

void ADGTrafficLightActor::SetSignalState(EDGSignalState NewState)
{
	if (SignalState == NewState)
	{
		return;
	}

	SignalState = NewState;
	TimeInState = 0.f;

	// Nothing to hold or release: vehicles read the aspect themselves on their next update.
	UpdateLampVisibility();
}

void ADGTrafficLightActor::AdvanceSignalState()
{
	switch (SignalState)
	{
	case EDGSignalState::Green:  SetSignalState(EDGSignalState::Yellow); break;
	case EDGSignalState::Yellow: SetSignalState(EDGSignalState::Red);    break;
	default:                     SetSignalState(EDGSignalState::Green);  break;
	}
}

void ADGTrafficLightActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bAutoCycle)
	{
		if (TimeUntilStart > 0.f)
		{
			TimeUntilStart -= DeltaSeconds;
		}
		else
		{
			TimeInState += DeltaSeconds;
			if (TimeInState >= GetCurrentStateDuration())
			{
				AdvanceSignalState();
			}
		}
	}

	if (bDrawDebug && SignalBox && CVarDGTrafficDebugDraw.GetValueOnGameThread())
	{
		const FColor Color = (SignalState == EDGSignalState::Green) ? FColor::Green
			: (SignalState == EDGSignalState::Yellow) ? FColor::Yellow
			: FColor::Red;

		DrawDebugBox(GetWorld(), SignalBox->GetComponentLocation(), SignalBox->GetScaledBoxExtent(),
			SignalBox->GetComponentQuat(), Color, false, -1.f, 0, 3.f);

		const TCHAR* StateName = (SignalState == EDGSignalState::Green) ? TEXT("GREEN")
			: (SignalState == EDGSignalState::Yellow) ? TEXT("YELLOW")
			: TEXT("RED");

		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 250.f),
			FString::Printf(TEXT("%s %.1fs"), StateName, GetCurrentStateDuration() - TimeInState),
			nullptr, Color, 0.f, true, 1.2f);
	}
}
