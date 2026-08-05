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

	DecisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DecisionBox"));
	DecisionBox->SetBoxExtent(FVector(300.f, 300.f, 200.f));
	DecisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DecisionBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	DecisionBox->SetGenerateOverlapEvents(true);
	SetRootComponent(DecisionBox);
}

void ADGPathDeciderActor::BeginPlay()
{
	Super::BeginPlay();

	DecisionBox->OnComponentBeginOverlap.AddDynamic(this, &ADGPathDeciderActor::OnDecisionBoxBeginOverlap);

	if (GatherValidTargets().IsEmpty())
	{
		UE_LOG(LogDeliveryGame, Warning, TEXT("%s has no valid TargetPaths; vehicles will pass through unchanged."),
			*GetName());
	}

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

ADGPathActor* ADGPathDeciderActor::ChoosePathFor_Implementation(ADGAIVehiclePawn* /*Vehicle*/)
{
	const TArray<ADGPathActor*> Valid = GatherValidTargets();
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

	UE_LOG(LogDeliveryGame, Verbose, TEXT("%s routed %s onto %s"),
		*GetName(), *Vehicle->GetName(), *NewPath->GetName());
}

void ADGPathDeciderActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bDrawDebug || !DecisionBox)
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
