// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGPathActor.h"

#include "Components/SplineComponent.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Traffic/DGTrafficSubsystem.h"

ADGPathActor::ADGPathActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Deliberately no root component. BP_Path's own "Spline Path" component is its root, and
	// defining a native root here displaces it, which breaks the Blueprint's component binding and
	// leaves the construction script reading None.
}

namespace
{
	/**
	 * Rebuild Spline from actor-local Points. Shared by OnConstruction and BeginPlay: a spline
	 * rebuilt only at construction time does NOT survive into PIE or a saved map unless the
	 * component is also marked spline-edited — USplineComponent's instance-data restore reverts
	 * unmarked curves to the Blueprint template's default 100 cm stub. That reversion is exactly
	 * how the whole W-corner traffic pile-up happened (2026-08-09): every MCP-spawned road ran
	 * fine in editor and became a 1 m path in PIE.
	 */
	void RebuildSplineFromPoints(USplineComponent& Spline, const TArray<FVector>& Points, bool bClosedLoop)
	{
		Spline.ClearSplinePoints(/*bUpdateSpline=*/false);
		for (const FVector& Point : Points)
		{
			Spline.AddSplinePoint(Point, ESplineCoordinateSpace::Local, /*bUpdateSpline=*/false);
		}
		Spline.SetClosedLoop(bClosedLoop, /*bUpdateSpline=*/false);
		Spline.UpdateSpline();

		// Mark the per-instance curves authoritative so duplication (PIE) and save keep them.
		Spline.bSplineHasBeenEdited = true;
	}
}

void ADGPathActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Only an authored RoutePoints array rebuilds the spline; hand-edited splines stay untouched.
	if (RoutePoints.Num() < 2)
	{
		return;
	}

	ResolveRouteSpline();
	USplineComponent* Spline = RouteSpline;
	if (!Spline)
	{
		// A bare native ADGPathActor owns no spline — the component comes from the BP_Path Blueprint.
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("%s has RoutePoints but no spline component to rebuild. Spawn BP_Path rather than "
				 "the native class."), *GetName());
		return;
	}

	RebuildSplineFromPoints(*Spline, RoutePoints, bClosedLoopRoute);
}

USplineComponent* ADGPathActor::GetRouteSpline() const
{
	ResolveRouteSpline();
	return RouteSpline;
}

void ADGPathActor::ResolveRouteSpline() const
{
	if (RouteSpline)
	{
		return;
	}

	// Prefer a spline that actually has a route authored on it over an empty one, so an incidental
	// empty spline component cannot shadow the real path.
	TArray<USplineComponent*> Splines;
	GetComponents<USplineComponent>(Splines);

	USplineComponent* Fallback = nullptr;
	for (USplineComponent* Candidate : Splines)
	{
		if (!Candidate)
		{
			continue;
		}

		if (Candidate->GetNumberOfSplinePoints() > 1)
		{
			RouteSpline = Candidate;
			return;
		}

		if (!Fallback)
		{
			Fallback = Candidate;
		}
	}

	RouteSpline = Fallback;
}

void ADGPathActor::BeginPlay()
{
	Super::BeginPlay();

	ResolveRouteSpline();

	if (!RouteSpline)
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("%s has no spline component; it will not be registered as a route. Add a Spline "
				 "Component to this actor."), *GetName());
		return;
	}

	// Runtime authority: an authored RoutePoints array always wins over whatever spline state was
	// serialized or restored, so a route can never begin play as the template's default stub.
	if (RoutePoints.Num() >= 2)
	{
		RebuildSplineFromPoints(*RouteSpline, RoutePoints, bClosedLoopRoute);
	}

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
	const USplineComponent* Spline = GetRouteSpline();
	return Spline ? Spline->GetSplineLength() : 0.f;
}

bool ADGPathActor::IsClosedLoop() const
{
	const USplineComponent* Spline = GetRouteSpline();
	return Spline && Spline->IsClosedLoop();
}

void ADGPathActor::GetClosestPoint(const FVector& WorldLocation, FVector& OutLocation, float& OutDistanceAlongSpline) const
{
	const USplineComponent* Spline = GetRouteSpline();
	if (!Spline)
	{
		OutLocation = WorldLocation;
		OutDistanceAlongSpline = 0.f;
		return;
	}

	const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	OutDistanceAlongSpline = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
	OutLocation = Spline->FindLocationClosestToWorldLocation(WorldLocation, ESplineCoordinateSpace::World);
}

double ADGPathActor::GetDistanceSquaredTo(const FVector& WorldLocation) const
{
	const USplineComponent* Spline = GetRouteSpline();
	if (!Spline)
	{
		return TNumericLimits<double>::Max();
	}

	const FVector Closest = Spline->FindLocationClosestToWorldLocation(WorldLocation, ESplineCoordinateSpace::World);
	return FVector::DistSquared(Closest, WorldLocation);
}

float ADGPathActor::GetAlignmentWith(const FVector& WorldLocation, const FVector& Forward) const
{
	const USplineComponent* Spline = GetRouteSpline();
	if (!Spline)
	{
		return 0.f;
	}

	const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	const float Distance = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
	const FVector Direction = Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

	// Flattened to 2D so a sloped road does not weaken the comparison.
	return FVector::DotProduct(Direction.GetSafeNormal2D(), Forward.GetSafeNormal2D());
}

FVector ADGPathActor::GetEndLocation() const
{
	const USplineComponent* Spline = GetRouteSpline();
	if (!Spline)
	{
		return GetActorLocation();
	}

	const float Distance = Spline->IsClosedLoop() ? 0.f : Spline->GetSplineLength();
	return Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
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

	const USplineComponent* Spline = GetRouteSpline();
	if (!bDrawDebug || !CVarDGTrafficDebugDraw.GetValueOnGameThread() || !Spline)
	{
		return;
	}

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	const float Length = Spline->GetSplineLength();
	const int32 NumSegments = FMath::Max(NumPoints * 4, 8);

	FVector Previous = Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
	for (int32 i = 1; i <= NumSegments; ++i)
	{
		const float Distance = (Length * i) / NumSegments;
		const FVector Current = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		DrawDebugLine(GetWorld(), Previous, Current, FColor::Green, false, -1.f, 0, 4.f);
		Previous = Current;
	}

	// Link lines to continuation routes, so junction wiring is visible at a glance.
	const FVector End = Spline->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World);
	for (const TObjectPtr<ADGPathActor>& Next : NextPaths)
	{
		if (const USplineComponent* NextSpline = Next ? Next->GetRouteSpline() : nullptr)
		{
			const FVector NextStart = NextSpline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
			DrawDebugDirectionalArrow(GetWorld(), End, NextStart, 120.f, FColor::Yellow, false, -1.f, 0, 6.f);
		}
	}
}
