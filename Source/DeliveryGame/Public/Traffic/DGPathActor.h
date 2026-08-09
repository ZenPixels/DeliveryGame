// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DGPathActor.generated.h"

class USplineComponent;

/**
 * A single spline route that AI vehicles follow. Native replacement for BP_Path.
 *
 * Paths register themselves with UDGTrafficSubsystem on BeginPlay so vehicles can
 * find their nearest route without an O(actors-in-level) search per vehicle.
 */
UCLASS(Blueprintable, ClassGroup = (Traffic), meta = (DisplayName = "Traffic Path"))
class DELIVERYGAME_API ADGPathActor : public AActor
{
	GENERATED_BODY()

public:
	ADGPathActor();

	/**
	 * The route. **Resolved from whichever USplineComponent this actor owns, not created here.**
	 *
	 * BP_Path carries its own "Spline Path" component, and the authored spline points of every path
	 * placed in a level live on that per-instance component. Creating a native spline instead would
	 * leave each reparented actor with an empty native spline and the real route orphaned, losing
	 * every road in the map. Resolving keeps the existing component authoritative.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path")
	mutable TObjectPtr<USplineComponent> RouteSpline;

	/**
	 * Routes a vehicle may continue onto after reaching the end of this one.
	 * Leave empty for a dead end; a closed-loop spline never consults this.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Path")
	TArray<TObjectPtr<ADGPathActor>> NextPaths;

	/** Per-path throttle cap for vehicles on this route. 0 means "use the vehicle's own MaxThrottle". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThrottleOverride = 0.f;

	/**
	 * Speed limit for this road, in mph. 0 means "use the vehicle's own CruiseSpeedMPH".
	 *
	 * This is the per-road control: set it low on tight residential streets and high on straight runs.
	 * Vehicles also slow for corners automatically, so this is the limit on the straight, not a
	 * cornering speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (ClampMin = "0.0", Units = "mph"))
	float SpeedLimitMPH = 0.f;

	/** Draw this path's spline and its links to NextPaths in PIE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Debug")
	bool bDrawDebug = false;

	/** The route, resolving it on first access. Always prefer this over touching RouteSpline directly. */
	UFUNCTION(BlueprintPure, Category = "Path")
	USplineComponent* GetRouteSpline() const;

	UFUNCTION(BlueprintPure, Category = "Path")
	float GetSplineLength() const;

	UFUNCTION(BlueprintPure, Category = "Path")
	bool IsClosedLoop() const;

	/**
	 * Closest point on this path to a world location.
	 * @param OutLocation             World-space closest point on the spline.
	 * @param OutDistanceAlongSpline  Input key converted to distance along the spline.
	 */
	void GetClosestPoint(const FVector& WorldLocation, FVector& OutLocation, float& OutDistanceAlongSpline) const;

	/** Squared distance from a world location to the closest point on this path. */
	double GetDistanceSquaredTo(const FVector& WorldLocation) const;

	/**
	 * Dot product of this path's direction near WorldLocation against Forward, flattened to 2D.
	 * Greater than zero means the route runs the same way the vehicle is facing.
	 *
	 * Used to keep vehicles off oncoming routes: an intersection typically has splines for both
	 * directions overlapping it, and without this check a vehicle can be handed the opposing one and
	 * will U-turn to follow it.
	 */
	float GetAlignmentWith(const FVector& WorldLocation, const FVector& Forward) const;

	/** World location at the very end of this route (or the start, for a closed loop). */
	FVector GetEndLocation() const;

	/** Pick a continuation route. Returns null at a dead end. */
	UFUNCTION(BlueprintCallable, Category = "Path")
	ADGPathActor* ChooseNextPath() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Cache the spline this actor owns into RouteSpline. Safe to call repeatedly, and deliberately
	 * not an OnRegister override — adding a virtual would change the vtable and block Live Coding.
	 */
	void ResolveRouteSpline() const;
};
