// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGPathActor.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Traffic/DGTrafficSubsystem.h"

ADGPathActor::ADGPathActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SplinePath = CreateDefaultSubobject<USplineComponent>(TEXT("SplinePath"));
	SetRootComponent(SplinePath);
}

void ADGPathActor::BeginPlay()
{
	Super::BeginPlay();

	if (UDGTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UDGTrafficSubsystem>() : nullptr)
	{
		Traffic->RegisterPath(this);
	}

	// Only pay for ticking when there is debug drawing to do.
	SetActorTickEnabled(bDrawDebug);
}

void ADGPathActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDGTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UDGTrafficSubsystem>() : nullptr)
	{
		Traffic->UnregisterPath(this);
	}

	Super::EndPlay(EndPlayReason);
}

float ADGPathActor::GetSplineLength() const
{
	return SplinePath ? SplinePath->GetSplineLength() : 0.f;
}

bool ADGPathActor::IsClosedLoop() const
{
	return SplinePath && SplinePath->IsClosedLoop();
}

void ADGPathActor::GetClosestPoint(const FVector& WorldLocation, FVector& OutLocation, float& OutDistanceAlongSpline) const
{
	if (!SplinePath)
	{
		OutLocation = WorldLocation;
		OutDistanceAlongSpline = 0.f;
		return;
	}

	const float InputKey = SplinePath->FindInputKeyClosestToWorldLocation(WorldLocation);
	OutDistanceAlongSpline = SplinePath->GetDistanceAlongSplineAtSplineInputKey(InputKey);
	OutLocation = SplinePath->FindLocationClosestToWorldLocation(WorldLocation, ESplineCoordinateSpace::World);
}

double ADGPathActor::GetDistanceSquaredTo(const FVector& WorldLocation) const
{
	if (!SplinePath)
	{
		return TNumericLimits<double>::Max();
	}

	const FVector Closest = SplinePath->FindLocationClosestToWorldLocation(WorldLocation, ESplineCoordinateSpace::World);
	return FVector::DistSquared(Closest, WorldLocation);
}

ADGPathActor* ADGPathActor::ChooseNextPath() const
{
	// Gather valid candidates first so a null entry in NextPaths cannot be "chosen".
	TArray<ADGPathActor*> Candidates;
	Candidates.Reserve(NextPaths.Num());
	for (const TObjectPtr<ADGPathActor>& Next : NextPaths)
	{
		if (Next)
		{
			Candidates.Add(Next);
		}
	}

	if (Candidates.IsEmpty())
	{
		return nullptr;
	}

	return Candidates[FMath::RandHelper(Candidates.Num())];
}

void ADGPathActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bDrawDebug || !SplinePath)
	{
		return;
	}

	const int32 NumPoints = SplinePath->GetNumberOfSplinePoints();
	const float Length = SplinePath->GetSplineLength();
	const int32 NumSegments = FMath::Max(NumPoints * 4, 8);

	FVector Previous = SplinePath->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
	for (int32 i = 1; i <= NumSegments; ++i)
	{
		const float Distance = (Length * i) / NumSegments;
		const FVector Current = SplinePath->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		DrawDebugLine(GetWorld(), Previous, Current, FColor::Green, false, -1.f, 0, 4.f);
		Previous = Current;
	}

	// Link lines to continuation routes, so junction wiring is visible at a glance.
	const FVector End = SplinePath->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World);
	for (const TObjectPtr<ADGPathActor>& Next : NextPaths)
	{
		if (Next && Next->SplinePath)
		{
			const FVector NextStart = Next->SplinePath->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
			DrawDebugDirectionalArrow(GetWorld(), End, NextStart, 120.f, FColor::Yellow, false, -1.f, 0, 6.f);
		}
	}
}
