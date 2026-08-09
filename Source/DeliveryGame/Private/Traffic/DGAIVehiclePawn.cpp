// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGAIVehiclePawn.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Traffic/DGPathFollowComponent.h"

ADGAIVehiclePawn::ADGAIVehiclePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	// No components created here — see the component block in the header. They are resolved from
	// the Blueprint's own components in BeginPlay.
}

UBoxComponent* ADGAIVehiclePawn::FindBox(const FName& Tag, const FString& NameSubstring) const
{
	TArray<UBoxComponent*> Boxes;
	GetComponents<UBoxComponent>(Boxes);

	// Tag wins over name: BP_AI_Vehicle_Base identifies its routing marker by the "routing" tag,
	// and a tag survives renaming the component.
	for (UBoxComponent* Box : Boxes)
	{
		if (Box && !Tag.IsNone() && Box->ComponentHasTag(Tag))
		{
			return Box;
		}
	}

	for (UBoxComponent* Box : Boxes)
	{
		if (Box && !NameSubstring.IsEmpty() && Box->GetName().Contains(NameSubstring))
		{
			return Box;
		}
	}

	return nullptr;
}

void ADGAIVehiclePawn::BindBlockingVolume(UBoxComponent* Volume)
{
	if (!Volume)
	{
		return;
	}

	// The authored volumes are not reliably set up for queries, so enforce it here rather than
	// depending on per-Blueprint collision settings.
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionResponseToAllChannels(ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	Volume->OnComponentBeginOverlap.AddDynamic(this, &ADGAIVehiclePawn::OnColliderBeginOverlap);
	Volume->OnComponentEndOverlap.AddDynamic(this, &ADGAIVehiclePawn::OnColliderEndOverlap);
}

void ADGAIVehiclePawn::ResolveComponents()
{
	// Finds the Blueprint's own path-follow component once BP_Path_Follow is reparented onto
	// UDGPathFollowComponent, so there is exactly one driving the vehicle.
	PathFollow = FindComponentByClass<UDGPathFollowComponent>();
	if (!PathFollow)
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("%s has no Path Follow component; it will not drive. Add one, or check that "
				 "BP_Path_Follow has been reparented onto UDGPathFollowComponent."), *GetName());
	}

	RouteCollider = FindBox(TEXT("routing"), TEXT("Route"));
	TrafficCollider = FindBox(NAME_None, TEXT("Traffic"));
	StopZone = FindBox(NAME_None, TEXT("Stop"));

	// Apply the detection geometry from code — see bOverrideTrafficColliderShape.
	if (bOverrideTrafficColliderShape && TrafficCollider)
	{
		TrafficCollider->SetRelativeLocation(TrafficColliderOffset);
		TrafficCollider->SetRelativeRotation(FRotator::ZeroRotator);
		TrafficCollider->SetBoxExtent(TrafficColliderExtent, /*bUpdateOverlaps=*/true);

		UE_LOG(LogDeliveryGame, Verbose,
			TEXT("%s traffic volume set to offset %s extent %s"),
			*GetName(), *TrafficColliderOffset.ToCompactString(), *TrafficColliderExtent.ToCompactString());
	}

	// StopZone is genuinely optional — BP_AI_Vehicle_Base never had one.
	BindBlockingVolume(TrafficCollider);
	BindBlockingVolume(StopZone);

	CrashAudio = FindComponentByClass<UAudioComponent>();
}

void ADGAIVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	ResolveComponents();

	if (USkeletalMeshComponent* VehicleMesh = GetMesh())
	{
		// Hit events on a simulating body require this to be set explicitly.
		VehicleMesh->SetNotifyRigidBodyCollision(true);
		VehicleMesh->OnComponentHit.AddDynamic(this, &ADGAIVehiclePawn::OnMeshHit);
	}

	if (CrashSound && CrashAudio)
	{
		CrashAudio->SetSound(CrashSound);
	}

	// Only ever turn debug drawing on from here. Mirroring the flag outright would clobber a
	// per-component setting made in a child Blueprint or on a placed instance.
	if (bDrawDebug && PathFollow)
	{
		PathFollow->bDrawDebug = true;
	}

	// Start the cooldown expired so the first genuine impact is audible.
	TimeSinceLastCrashSound = CrashSoundCooldown;
}

float ADGAIVehiclePawn::GetForwardSpeedMPH() const
{
	const UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	return Movement ? Movement->GetForwardSpeedMPH() : 0.f;
}

bool ADGAIVehiclePawn::IsBlocked() const
{
	return BlockerCount > 0 || BlockingActors.Num() > 0;
}

void ADGAIVehiclePawn::AddBlocker()
{
	++BlockerCount;
	SyncBlockedState();
}

void ADGAIVehiclePawn::RemoveBlocker()
{
	if (BlockerCount == 0)
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("%s: RemoveBlocker called with no blockers held; check for an unpaired AddBlocker."),
			*GetName());
		return;
	}

	--BlockerCount;
	SyncBlockedState();
}

bool ADGAIVehiclePawn::ShouldBlockFor_Implementation(AActor* OtherActor) const
{
	if (!OtherActor || OtherActor == this)
	{
		return false;
	}

	// Other traffic holds us; scenery and the player on foot do not.
	return OtherActor->IsA<ADGAIVehiclePawn>();
}

void ADGAIVehiclePawn::RecomputeOverlapBlockers()
{
	BlockingActors.Reset();

	auto GatherFrom = [this](const UBoxComponent* Volume)
	{
		if (!Volume)
		{
			return;
		}

		TArray<AActor*> Overlapping;
		Volume->GetOverlappingActors(Overlapping);
		for (AActor* Other : Overlapping)
		{
			if (ShouldBlockFor(Other))
			{
				BlockingActors.Add(Other);
			}
		}
	};

	GatherFrom(TrafficCollider);
	GatherFrom(StopZone);

	UpdateTrafficClearance();
	SyncBlockedState();
}

void ADGAIVehiclePawn::UpdateTrafficClearance()
{
	if (!PathFollow)
	{
		return;
	}

	// Measured along our own forward axis, so a vehicle alongside in the next lane does not read as
	// "directly ahead" the way a raw centre-to-centre distance would.
	const FVector SelfLocation = GetActorLocation();
	const FVector Forward = GetActorForwardVector().GetSafeNormal();
	const FVector SelfVelocity = GetVelocity();

	float NearestAhead = TNumericLimits<float>::Max();
	float ClosingSpeed = 0.f;

	for (const TWeakObjectPtr<AActor>& Weak : BlockingActors)
	{
		const AActor* Other = Weak.Get();
		if (!Other)
		{
			continue;
		}

		const float AlongForward = FVector::DotProduct(Other->GetActorLocation() - SelfLocation, Forward);
		if (AlongForward > 0.f && AlongForward < NearestAhead)
		{
			NearestAhead = AlongForward;

			// Relative velocity along our heading: how fast this gap is actually shrinking. A parked
			// or braking vehicle yields a large closing speed; one matching our speed yields zero.
			ClosingSpeed = FVector::DotProduct(SelfVelocity - Other->GetVelocity(), Forward);
		}
	}

	PathFollow->SetTrafficAhead(NearestAhead, ClosingSpeed);
}

void ADGAIVehiclePawn::OnColliderBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* /*OtherActor*/, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	RecomputeOverlapBlockers();
}

void ADGAIVehiclePawn::OnColliderEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* /*OtherActor*/, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	RecomputeOverlapBlockers();
}

void ADGAIVehiclePawn::SyncBlockedState()
{
	if (PathFollow)
	{
		// Only manual holds hard-stop the vehicle. Overlaps feed TrafficClearance instead, so closing
		// on a slower vehicle eases off rather than slamming to a halt the moment the volumes touch.
		PathFollow->bBlockedAhead = (BlockerCount > 0);
	}
}

void ADGAIVehiclePawn::OnMeshHit(
	UPrimitiveComponent* /*HitComponent*/, AActor* /*OtherActor*/, UPrimitiveComponent* /*OtherComp*/,
	FVector NormalImpulse, const FHitResult& Hit)
{
	// Only CrashAudio is required. The Blueprint's audio component already has its MetaSound
	// assigned, so requiring the CrashSound override here would silence every existing vehicle.
	if (!CrashAudio || !CrashAudio->GetSound())
	{
		return;
	}

	const float ImpulseSize = NormalImpulse.Size();

	// A light impact silences any crash still playing, mirroring the original's else branch.
	if (ImpulseSize < CrashImpulseThreshold)
	{
		if (!CrashStopTriggerParameterName.IsNone())
		{
			CrashAudio->SetTriggerParameter(CrashStopTriggerParameterName);
		}
		return;
	}

	if (TimeSinceLastCrashSound < CrashSoundCooldown)
	{
		return;
	}

	TimeSinceLastCrashSound = 0.f;

	CrashAudio->SetWorldLocation(Hit.ImpactPoint);

	if (!CrashIntensityParameterName.IsNone())
	{
		const float Intensity = FMath::GetMappedRangeValueClamped(
			FVector2f(CrashImpulseThreshold, CrashImpulseAtFullIntensity), FVector2f(0.f, 1.f), ImpulseSize);
		CrashAudio->SetFloatParameter(CrashIntensityParameterName, Intensity);
	}

	if (!CrashAudio->IsPlaying())
	{
		CrashAudio->Play();
	}

	// Trigger after Play so the MetaSound graph is live to receive it.
	if (!CrashTriggerParameterName.IsNone())
	{
		CrashAudio->SetTriggerParameter(CrashTriggerParameterName);
	}

	UE_LOG(LogDeliveryGame, Verbose, TEXT("%s crash: impulse %.0f"), *GetName(), ImpulseSize);
}

void ADGAIVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TimeSinceLastCrashSound += DeltaSeconds;

	// The gap to a vehicle ahead changes every frame, so re-measure while anything is being tracked.
	if (!BlockingActors.IsEmpty())
	{
		UpdateTrafficClearance();
	}

	if (bDrawDebugColliders)
	{
		const UWorld* World = GetWorld();
		auto DrawVolume = [World](const UBoxComponent* Volume, const FColor& Color)
		{
			if (Volume)
			{
				DrawDebugBox(World, Volume->GetComponentLocation(), Volume->GetScaledBoxExtent(),
					Volume->GetComponentQuat(), Color, false, -1.f, 0, 2.f);
			}
		};

		DrawVolume(RouteCollider, FColor::Blue);
		DrawVolume(TrafficCollider, IsBlocked() ? FColor::Red : FColor::Green);
		DrawVolume(StopZone, FColor::Yellow);
	}
}
