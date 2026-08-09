// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGPathDeciderActor.h"

#include "Components/BoxComponent.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Traffic/DGAIVehiclePawn.h"
#include "Traffic/DGPathActor.h"
#include "Traffic/DGPathFollowComponent.h"

ADGPathDeciderActor::ADGPathDeciderActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// No components created here — see the DecisionBox comment. It is resolved in BeginPlay from
	// whatever box the Blueprint already owns.
}

void ADGPathDeciderActor::BeginPlay()
{
	Super::BeginPlay();

	DecisionBox = FindComponentByClass<UBoxComponent>();
	if (DecisionBox)
	{
		// The Blueprint's box may not have been set up for queries, so make sure it can report
		// overlaps rather than silently never firing.
		DecisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		DecisionBox->SetCollisionResponseToAllChannels(ECR_Overlap);
		DecisionBox->SetGenerateOverlapEvents(true);
		DecisionBox->OnComponentBeginOverlap.AddDynamic(this, &ADGPathDeciderActor::OnDecisionBoxBeginOverlap);
	}
	else
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("%s has no Box Component; it cannot reroute anything. Add one to this actor."),
			*GetName());
	}

	// An empty TargetPaths is normal — routes are then discovered from overlaps at the moment a
	// vehicle arrives, which is too early to check here.

	SetActorTickEnabled(bDrawDebug);
}

TArray<ADGPathActor*> ADGPathDeciderActor::GatherValidTargets() const
{
	TArray<ADGPathActor*> Valid;
	Valid.Reserve(TargetPaths.Num());
	for (const TObjectPtr<ADGPathActor>& Path : TargetPaths)
	{
		if (Path)
		{
			Valid.Add(Path);
		}
	}

	if (!Valid.IsEmpty())
	{
		return Valid;
	}

	// Fall back to whatever routes physically pass through the volume. BP_Path_Decider worked this
	// way, so deciders already placed in a level need no authored TargetPaths to keep functioning.
	if (DecisionBox)
	{
		TArray<AActor*> Overlapping;
		DecisionBox->GetOverlappingActors(Overlapping, ADGPathActor::StaticClass());
		for (AActor* Overlap : Overlapping)
		{
			if (ADGPathActor* Path = Cast<ADGPathActor>(Overlap))
			{
				Valid.Add(Path);
			}
		}
	}

	return Valid;
}

ADGPathActor* ADGPathDeciderActor::PeekNextPath() const
{
	const TArray<ADGPathActor*> Valid = GatherValidTargets();
	if (Valid.IsEmpty())
	{
		return nullptr;
	}

	if (ChoiceMode == EDGPathChoiceMode::RoundRobin)
	{
		return Valid[RoundRobinIndex % Valid.Num()];
	}

	return Valid[0];
}

ADGPathActor* ADGPathDeciderActor::ChoosePathFor_Implementation(ADGAIVehiclePawn* Vehicle)
{
	TArray<ADGPathActor*> Valid = GatherValidTargets();

	// Never hand a vehicle the road it is already on, matching GetNextTargetSpline's
	// RemoveItem(TargetSplines, TargetSpline). Without this, an overlap-discovered candidate set
	// nearly always includes the current route and vehicles would keep re-picking it.
	if (Vehicle && Vehicle->PathFollow)
	{
		Valid.Remove(Vehicle->PathFollow->GetCurrentPath());

		// Drop only routes the vehicle genuinely cannot follow. Do NOT filter on alignment here: the
		// splines are two-way road centre lines, so a route running against its spline direction is
		// perfectly valid and is simply travelled in reverse. U-turns, however, are banned (author
		// rule) — a decider must never send a vehicle back the way it came.
		const FVector VehicleLocation = Vehicle->GetActorLocation();
		const FVector VehicleForward = Vehicle->GetActorForwardVector();
		Valid.RemoveAll([Vehicle, &VehicleLocation, &VehicleForward](const ADGPathActor* Path)
		{
			return !Vehicle->PathFollow->IsPathUsable(Path)
				|| Vehicle->PathFollow->WouldEnterBackwards(Path, VehicleLocation, VehicleForward);
		});
	}

	if (Valid.IsEmpty())
	{
		return nullptr;
	}

	if (ChoiceMode == EDGPathChoiceMode::RoundRobin)
	{
		// Modulo against the live count so removing a target at runtime cannot index out of range.
		ADGPathActor* Chosen = Valid[RoundRobinIndex % Valid.Num()];
		RoundRobinIndex = (RoundRobinIndex + 1) % Valid.Num();
		return Chosen;
	}

	return Valid[FMath::RandHelper(Valid.Num())];
}

void ADGPathDeciderActor::OnDecisionBoxBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	ADGAIVehiclePawn* Vehicle = Cast<ADGAIVehiclePawn>(OtherActor);
	if (!Vehicle || !Vehicle->PathFollow)
	{
		return;
	}

	// A vehicle that committed to a route moments ago — its planned handoff just fired — keeps it.
	// Handing out a second decision mid-crossing yanks the goal sideways at the worst moment.
	if (Vehicle->PathFollow->GetTimeSinceLastPathChange() < 4.f)
	{
		return;
	}

	ADGPathActor* NewPath = ChoosePathFor(Vehicle);
	if (!NewPath)
	{
		return;
	}

	// Leave the vehicle alone if it is already on the chosen route, so overlapping volumes do not
	// reset its progress and stutter the steering.
	if (Vehicle->PathFollow->GetCurrentPath() == NewPath)
	{
		return;
	}

	Vehicle->PathFollow->SetPath(NewPath, bSnapToClosestPoint);

	UE_LOG(LogDeliveryGame, Log, TEXT("%s routed %s onto %s"),
		*GetName(), *Vehicle->GetName(), *NewPath->GetName());
}

void ADGPathDeciderActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bDrawDebug || !CVarDGTrafficDebugDraw.GetValueOnGameThread() || !DecisionBox)
	{
		return;
	}

	const UWorld* World = GetWorld();
	DrawDebugBox(World, DecisionBox->GetComponentLocation(), DecisionBox->GetScaledBoxExtent(),
		DecisionBox->GetComponentQuat(), FColor::Magenta, false, -1.f, 0, 3.f);

	for (ADGPathActor* Path : GatherValidTargets())
	{
		DrawDebugDirectionalArrow(World, GetActorLocation(), Path->GetActorLocation(),
			150.f, FColor::Magenta, false, -1.f, 0, 4.f);
	}
}
