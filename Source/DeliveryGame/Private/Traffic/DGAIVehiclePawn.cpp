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

	PathFollow = CreateDefaultSubobject<UDGPathFollowComponent>(TEXT("PathFollow"));

	USkeletalMeshComponent* VehicleMesh = GetMesh();

	// Query-only volumes: they must never push the vehicle around, only report overlaps.
	RouteCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("RouteCollider"));
	RouteCollider->SetupAttachment(VehicleMesh);
	RouteCollider->SetBoxExtent(FVector(120.f, 90.f, 60.f));
	RouteCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RouteCollider->SetCollisionResponseToAllChannels(ECR_Overlap);
	RouteCollider->SetGenerateOverlapEvents(true);

	TrafficCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("TrafficCollider"));
	TrafficCollider->SetupAttachment(VehicleMesh);
	TrafficCollider->SetRelativeLocation(FVector(400.f, 0.f, 0.f));
	TrafficCollider->SetBoxExtent(FVector(250.f, 80.f, 60.f));
	TrafficCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TrafficCollider->SetCollisionResponseToAllChannels(ECR_Overlap);
	TrafficCollider->SetGenerateOverlapEvents(true);

	StopZone = CreateDefaultSubobject<UBoxComponent>(TEXT("StopZone"));
	StopZone->SetupAttachment(VehicleMesh);
	StopZone->SetRelativeLocation(FVector(250.f, 0.f, 0.f));
	StopZone->SetBoxExtent(FVector(150.f, 100.f, 80.f));
	StopZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	StopZone->SetCollisionResponseToAllChannels(ECR_Overlap);
	StopZone->SetGenerateOverlapEvents(true);

	CrashAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("CrashAudio"));
	CrashAudio->SetupAttachment(VehicleMesh);
	CrashAudio->bAutoActivate = false;
}

void ADGAIVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	TrafficCollider->OnComponentBeginOverlap.AddDynamic(this, &ADGAIVehiclePawn::OnColliderBeginOverlap);
	TrafficCollider->OnComponentEndOverlap.AddDynamic(this, &ADGAIVehiclePawn::OnColliderEndOverlap);
	StopZone->OnComponentBeginOverlap.AddDynamic(this, &ADGAIVehiclePawn::OnColliderBeginOverlap);
	StopZone->OnComponentEndOverlap.AddDynamic(this, &ADGAIVehiclePawn::OnColliderEndOverlap);

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

	if (PathFollow)
	{
		PathFollow->bDrawDebug = bDrawDebug;
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

	SyncBlockedState();
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
		PathFollow->bBlockedAhead = IsBlocked();
	}
}

void ADGAIVehiclePawn::OnMeshHit(
	UPrimitiveComponent* /*HitComponent*/, AActor* /*OtherActor*/, UPrimitiveComponent* /*OtherComp*/,
	FVector NormalImpulse, const FHitResult& Hit)
{
	const float ImpulseSize = NormalImpulse.Size();
	if (ImpulseSize < CrashImpulseThreshold)
	{
		return;
	}

	if (TimeSinceLastCrashSound < CrashSoundCooldown)
	{
		return;
	}

	TimeSinceLastCrashSound = 0.f;

	if (!CrashAudio || !CrashSound)
	{
		return;
	}

	const float Intensity = FMath::GetMappedRangeValueClamped(
		FVector2f(CrashImpulseThreshold, CrashImpulseAtFullIntensity), FVector2f(0.f, 1.f), ImpulseSize);

	CrashAudio->SetWorldLocation(Hit.ImpactPoint);

	if (!CrashIntensityParameterName.IsNone())
	{
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

	UE_LOG(LogDeliveryGame, Verbose, TEXT("%s crash: impulse %.0f -> intensity %.2f"),
		*GetName(), ImpulseSize, Intensity);
}

void ADGAIVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TimeSinceLastCrashSound += DeltaSeconds;

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
