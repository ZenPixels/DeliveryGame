// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGTrafficLightActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Traffic/DGAIVehiclePawn.h"
#include "Traffic/DGPathFollowComponent.h"

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
			TEXT("%s has no Box Component, so it cannot hold anything. Add one to this Blueprint."),
			*GetName());
	}

	// "Yelllow" is spelled with three Ls on BP_Prop_Traffic_Light_Sm; match both spellings so the
	// lamp is still found if that typo is ever corrected.
	RedLight = FindComponentByNameFragments(*this, {TEXT("RedLight"), TEXT("Red Light"), TEXT("Red")});
	YellowLight = FindComponentByNameFragments(*this, {TEXT("Yelllow"), TEXT("Yellow")});
	GreenLight = FindComponentByNameFragments(*this, {TEXT("GreenLight"), TEXT("Green Light"), TEXT("Green")});

	if (!RedLight && !YellowLight && !GreenLight)
	{
		UE_LOG(LogDeliveryGame, Verbose,
			TEXT("%s found no lamp components; the signal will still gate traffic but show nothing."),
			*GetName());
	}
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
		// The Blueprint toggled bGenerateOverlapEvents to gate the zone. Leave events on permanently
		// and decide in the handler instead: toggling them off loses the end-overlap that would
		// release a waiting vehicle.
		SignalBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SignalBox->SetCollisionResponseToAllChannels(ECR_Overlap);
		SignalBox->SetGenerateOverlapEvents(true);
		SignalBox->OnComponentBeginOverlap.AddDynamic(this, &ADGTrafficLightActor::OnSignalBoxBeginOverlap);
		SignalBox->OnComponentEndOverlap.AddDynamic(this, &ADGTrafficLightActor::OnSignalBoxEndOverlap);

		// Catch vehicles already inside the volume at level start, which fire no begin-overlap.
		if (IsStopAspect())
		{
			HoldAllInVolume();
		}
	}
	else
	{
		SetActorTickEnabled(false);
	}
}

void ADGTrafficLightActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Never leave a vehicle holding for a signal that no longer exists.
	ReleaseAll();

	Super::EndPlay(EndPlayReason);
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

void ADGTrafficLightActor::ApplyHold(ADGAIVehiclePawn* Vehicle, bool bHold)
{
	if (!Vehicle || !Vehicle->PathFollow)
	{
		return;
	}

	Vehicle->PathFollow->SetSignalHold(bHold);

	if (bHold)
	{
		HeldVehicles.Add(Vehicle);
	}
	else
	{
		HeldVehicles.Remove(Vehicle);
	}
}

void ADGTrafficLightActor::HoldAllInVolume()
{
	if (!SignalBox)
	{
		return;
	}

	TArray<AActor*> Overlapping;
	SignalBox->GetOverlappingActors(Overlapping, ADGAIVehiclePawn::StaticClass());
	for (AActor* Other : Overlapping)
	{
		// Any AI vehicle, not just vans. The Blueprint cast to BP_AI_Van specifically, so the school
		// bus drove straight through every red light.
		ApplyHold(Cast<ADGAIVehiclePawn>(Other), true);
	}
}

void ADGTrafficLightActor::ReleaseAll()
{
	for (const TWeakObjectPtr<ADGAIVehiclePawn>& Weak : HeldVehicles)
	{
		if (ADGAIVehiclePawn* Vehicle = Weak.Get())
		{
			if (Vehicle->PathFollow)
			{
				Vehicle->PathFollow->SetSignalHold(false);
			}
		}
	}

	HeldVehicles.Reset();
}

void ADGTrafficLightActor::SetSignalState(EDGSignalState NewState)
{
	if (SignalState == NewState)
	{
		return;
	}

	SignalState = NewState;
	TimeInState = 0.f;

	UpdateLampVisibility();

	if (IsStopAspect())
	{
		HoldAllInVolume();
	}
	else
	{
		ReleaseAll();
	}
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

void ADGTrafficLightActor::OnSignalBoxBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!IsStopAspect())
	{
		return;
	}

	ApplyHold(Cast<ADGAIVehiclePawn>(OtherActor), true);
}

void ADGTrafficLightActor::OnSignalBoxEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	// Release on exit whatever the aspect: a vehicle outside the volume is no longer this signal's
	// concern, and leaving the flag set would strand it permanently.
	ApplyHold(Cast<ADGAIVehiclePawn>(OtherActor), false);
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

	if (bDrawDebug && SignalBox)
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
			FString::Printf(TEXT("%s %.1fs | holding %d"), StateName,
				GetCurrentStateDuration() - TimeInState, HeldVehicles.Num()),
			nullptr, Color, 0.f, true, 1.2f);
	}
}
