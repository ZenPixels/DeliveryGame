// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGAIVehiclePawn.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Traffic/DGPathFollowComponent.h"
#include "Traffic/DGTrafficLightActor.h"
#include "Traffic/DGTrafficSubsystem.h"

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

	// bReverseAsBrake stays ON, deliberately. These vehicles are authored with manual gearboxes
	// (TransmissionSetup.bUseAutomaticGears = false), and arcade mode is the only thing that engages
	// a forward gear from throttle input — disabling it left every vehicle revving in neutral, unable
	// to launch. Its downside (held brake at standstill = reverse throttle) is prevented instead by
	// ApplyHoldOutputs, which parks on the handbrake with the pedal released.

	// Kinematic traffic does not simulate: the transform is set directly each tick, so the body must
	// not fight the mover and the drivetrain has nothing to do. Collision stays on — the mesh acts as
	// a solid obstacle the player's physics vehicle can hit.
	if (PathFollow && PathFollow->MoveMode == EDGPathFollowMoveMode::Kinematic)
	{
		if (USkeletalMeshComponent* VehicleMesh = GetMesh())
		{
			VehicleMesh->SetSimulatePhysics(false);
		}
		if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
		{
			Movement->SetComponentTickEnabled(false);
		}
	}

	RouteCollider = FindBox(TEXT("routing"), TEXT("Route"));
	TrafficCollider = FindBox(NAME_None, TEXT("Traffic"));
	StopZone = FindBox(NAME_None, TEXT("Stop"));

	// Apply the detection geometry from code — see bOverrideTrafficColliderShape.
	if (bOverrideTrafficColliderShape && TrafficCollider)
	{
		TrafficCollider->SetRelativeLocation(TrafficColliderOffset);
		TrafficCollider->SetRelativeRotation(FRotator::ZeroRotator);

		// Scale must be normalised too: the placed vans carried a stray (14.8, 3.4, 1) scale from
		// the old partial struct writes, which multiplied the extent set below into a 185 m blade.
		// The 40 m sensor cull masked it — the author spotted the box in the editor (2026-08-10).
		TrafficCollider->SetRelativeScale3D(FVector::OneVector);
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

	// Any wheeled vehicle holds us — AI traffic *and the player's*. Restricting this to AI pawns
	// mattered little under physics (colliding with the player at least stopped the AI); under
	// kinematic movement nothing physical stops us, so the follow logic is the only thing standing
	// between a van and driving straight through the player's parked jeep.
	return OtherActor->IsA<AWheeledVehiclePawn>();
}

void ADGAIVehiclePawn::RecomputeOverlapBlockers()
{
	// Speed-scaled forward sensor. The full 25 m box is the braking-plus-headway distance at
	// cruise and is correct on straights — but a slow vehicle turning through a junction with a
	// 25 m blade reaches across the whole intersection and gets held by vehicles on the other
	// legs (author observation, 2026-08-10: sensor ~5x the van). Length follows speed: ~5 m at a
	// standstill up to the authored extent at cruise, anchored 50 cm ahead of the actor origin
	// like the static shape was.
	if (bOverrideTrafficColliderShape && TrafficCollider && PathFollow)
	{
		const float Speed = FMath::Abs(PathFollow->GetVehicleSpeed());
		const float HalfLength = FMath::Clamp(250.f + Speed * 0.85f, 250.f, TrafficColliderExtent.X);
		TrafficCollider->SetBoxExtent(
			FVector(HalfLength, TrafficColliderExtent.Y, TrafficColliderExtent.Z), /*bUpdateOverlaps=*/false);
		TrafficCollider->SetRelativeLocation(
			FVector(HalfLength + 50.f, TrafficColliderOffset.Y, TrafficColliderOffset.Z));
	}

	BlockingActors.Reset();

	auto GatherFrom = [this](UBoxComponent* Volume)
	{
		if (!Volume)
		{
			return;
		}

		// The overlap list is a cache that only refreshes when the component moves. A stationary
		// vehicle's cache can hold a phantom overlap indefinitely (a missed end event), which read as
		// "something at my bumper" with the nearest vehicle 4.7 km away — frozen forever. Refresh it
		// explicitly; this also fires any end-overlap notifies that were missed.
		Volume->UpdateOverlaps();

		// Count only vehicle BODIES. GetOverlappingActors matches any component — including the
		// other vehicle's own 23 m detection volume, so two vehicles "touched" at 50 m and crossing
		// sensors froze both drivers mid-junction (the diagnostic log caught the bus tracking a van
		// at "0 cm" that was 82 m away).
		TArray<UPrimitiveComponent*> OverlappingComponents;
		Volume->GetOverlappingComponents(OverlappingComponents);
		for (UPrimitiveComponent* Component : OverlappingComponents)
		{
			if (Component && Component->IsA<USkeletalMeshComponent>() && ShouldBlockFor(Component->GetOwner()))
			{
				BlockingActors.Add(Component->GetOwner());
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

	// Velocity of any vehicle, whichever way it moves: a kinematically-driven pawn reports zero
	// physics velocity, which would read as "parked" and break closing-speed braking entirely.
	auto VelocityOf = [](const AActor* Actor) -> FVector
	{
		if (const ADGAIVehiclePawn* AIPawn = Cast<ADGAIVehiclePawn>(Actor))
		{
			if (AIPawn->PathFollow && AIPawn->PathFollow->MoveMode == EDGPathFollowMoveMode::Kinematic)
			{
				return Actor->GetActorForwardVector() * AIPawn->PathFollow->GetVehicleSpeed();
			}
		}
		return Actor->GetVelocity();
	};

	const FVector SelfVelocity = VelocityOf(this);

	// Half-length of a mesh along Direction, using its ORIENTED local bounds. Gaps must be
	// bumper-to-bumper: centre-to-centre distances hide half of each vehicle's length as phantom
	// cushion. World-AABB projection was close but inflated diagonally — a vehicle mid-turn at 45
	// degrees read ~40% longer than it is, so "gap zero" fired at absurd real distances. Mesh bounds
	// specifically, never actor bounds: those include the 25 m detection volume.
	auto HalfLengthAlong = [](const USkeletalMeshComponent* BodyMesh, const FVector& Direction) -> float
	{
		if (!BodyMesh)
		{
			return 250.f; // roughly half a van, if there is no mesh to measure
		}
		const FVector LocalDir = BodyMesh->GetComponentTransform().InverseTransformVectorNoScale(Direction);
		const FVector E = BodyMesh->GetLocalBounds().BoxExtent * BodyMesh->GetComponentTransform().GetScale3D();
		return FMath::Abs(E.X * LocalDir.X) + FMath::Abs(E.Y * LocalDir.Y) + FMath::Abs(E.Z * LocalDir.Z);
	};

	const float SelfHalf = HalfLengthAlong(GetMesh(), Forward);

	float NearestAhead = TNumericLimits<float>::Max();
	float ClosingSpeed = 0.f;

	for (const TWeakObjectPtr<AActor>& Weak : BlockingActors)
	{
		const AActor* Other = Weak.Get();
		if (!Other)
		{
			continue;
		}

		const FVector ToOther = Other->GetActorLocation() - SelfLocation;

		// Sanity cull: nothing farther than the detection volume can reach is a real overlap. Belt
		// to the UpdateOverlaps braces above — a phantom must never freeze a vehicle again.
		if (ToOther.SizeSquared2D() > FMath::Square(4000.f))
		{
			continue;
		}

		const float AlongForward = FVector::DotProduct(ToOther, Forward);
		if (AlongForward <= 0.f)
		{
			continue;
		}

		const ADGAIVehiclePawn* OtherPawn = Cast<ADGAIVehiclePawn>(Other);
		const float OtherHalf = HalfLengthAlong(OtherPawn ? OtherPawn->GetMesh() : nullptr, Forward);
		const float BumperGap = FMath::Max(0.f, AlongForward - SelfHalf - OtherHalf);

		// Oncoming traffic in the adjacent lane is not an obstacle. Mid-turn, the long detection box
		// sweeps across the oncoming queue, and treating those vehicles as "ahead" froze the turner
		// mid-junction facing across the road (author's diagnosis, confirmed by mechanism). Oncoming
		// only counts when it is genuinely head-on in our lane and close.
		const float HeadingDot = FVector::DotProduct(
			Forward, Other->GetActorForwardVector().GetSafeNormal2D());
		if (HeadingDot < -0.4f)
		{
			const float Lateral = FMath::Abs(ToOther.X * Forward.Y - ToOther.Y * Forward.X);
			if (Lateral > 150.f || BumperGap > 450.f)
			{
				continue;
			}
		}

		if (BumperGap < NearestAhead)
		{
			NearestAhead = BumperGap;

			// Relative velocity along our heading: how fast this gap is actually shrinking. A parked
			// or braking vehicle yields a large closing speed; one matching our speed yields zero.
			ClosingSpeed = FVector::DotProduct(SelfVelocity - VelocityOf(Other), Forward);
		}
	}

	PathFollow->SetTrafficAhead(NearestAhead, ClosingSpeed);

	// Diagnostic: vehicles have been observed frozen on obstacles that do not exist. When anything
	// reads as close, name it — the log answers "blocked by WHAT" definitively.
	if (NearestAhead < 500.f)
	{
		static float LastPhantomLogTime = -100.f;
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		if (Now - LastPhantomLogTime > 2.f)
		{
			LastPhantomLogTime = Now;
			for (const TWeakObjectPtr<AActor>& Weak : BlockingActors)
			{
				if (const AActor* Other = Weak.Get())
				{
					UE_LOG(LogDeliveryGame, Log, TEXT("%s tracking %s at %.0f cm (2D dist %.0f)"),
						*GetName(), *Other->GetName(), NearestAhead,
						FVector::Dist2D(GetActorLocation(), Other->GetActorLocation()));
				}
			}
		}
	}
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

void ADGAIVehiclePawn::UpdateSignalAwareness()
{
	if (!PathFollow)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const UDGTrafficSubsystem* Traffic = World ? World->GetSubsystem<UDGTrafficSubsystem>() : nullptr;
	if (!Traffic)
	{
		return;
	}

	const FVector Location = GetActorLocation();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();

	// Mode-aware speed: kinematic pawns report zero physics velocity, which would make every in-zone
	// vehicle read as "queueing" (frozen mid-junction on a flip) and disable the amber dilemma.
	const float Speed = PathFollow->GetVehicleSpeed();

	// Recomputed from scratch every tick. Because the answer is derived rather than remembered, a
	// hold cannot outlive the condition that caused it. (The one piece of memory, bSignalCommitted,
	// is cleared by ground truth: being outside every zone.)
	bool bShouldHold = false;
	bool bInAnyZone = false;
	float NearestStopLine = 1000000.f;

	for (const ADGTrafficLightActor* Light : Traffic->GetRegisteredLights())
	{
		if (!Light)
		{
			continue;
		}

		if (Light->IsActorInZone(this))
		{
			// Track zone membership for every aspect, so a vehicle that launches through on green is
			// already latched as committed before the light can flip on it.
			bInAnyZone = true;

			// Only an uncommitted vehicle at queueing speed holds. A committed one — it entered
			// moving, or slowed for its turn mid-crossing, or clipped the oncoming approach's pad —
			// completes the manoeuvre.
			if (Light->IsStopAspect() && !bSignalCommitted && Speed <= SignalCommitSpeed)
			{
				bShouldHold = true;
			}
			continue;
		}

		if (!Light->IsStopAspect())
		{
			continue;
		}

		const float Distance = Light->GetStopLineDistance(Location, Forward);
		if (Distance < 0.f || Distance > SignalDetectionRange)
		{
			continue;
		}

		// Amber dilemma: if stopping from here would take more than comfortable braking, carry on —
		// commitment covers the crossing. A red is braked for regardless.
		if (Light->SignalState == EDGSignalState::Yellow && PathFollow->ComfortableDeceleration > 0.f)
		{
			const float UsableDistance = FMath::Max(Distance - 150.f, 50.f);
			const float RequiredDeceleration = (Speed * Speed) / (2.f * UsableDistance);
			if (RequiredDeceleration > PathFollow->ComfortableDeceleration)
			{
				continue;
			}
		}

		NearestStopLine = FMath::Min(NearestStopLine, Distance);
	}

	// Latch: moving through a zone un-held means crossing. Exit clears it.
	if (!bInAnyZone)
	{
		bSignalCommitted = false;
	}
	else if (!bShouldHold && Speed > SignalCommitSpeed)
	{
		bSignalCommitted = true;
	}

	PathFollow->SetSignalHold(bShouldHold);
	PathFollow->SetSignalStopAhead(NearestStopLine);
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

	// Rebuild the tracked set from ground truth every tick, not just on overlap events. A missed
	// end-overlap once left a vehicle believing something was at its bumper forever — frozen 10 m
	// off-road with nothing near it. Four vehicles querying two volumes each is trivial; a stale
	// clearance is not.
	RecomputeOverlapBlockers();

	UpdateSignalAwareness();

	if (bDrawDebugColliders && CVarDGTrafficDebugDraw.GetValueOnGameThread())
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
