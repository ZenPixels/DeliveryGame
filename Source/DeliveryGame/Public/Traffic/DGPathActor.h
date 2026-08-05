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

	/** The route itself. Named to match the old BP_Path "Spline Path" variable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path")
	TObjectPtr<USplineComponent> SplinePath;

	/**
	 * Routes a vehicle may continue onto after reaching the end of this one.
	 * Leave empty for a dead end; a closed-loop spline never consults this.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Path")
	TArray<TObjectPtr<ADGPathActor>> NextPaths;

	/** Per-path throttle cap for vehicles on this route. 0 means "use the vehicle's own MaxThrottle". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThrottleOverride = 0.f;

	/** Draw this path's spline and its links to NextPaths in PIE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Debug")
	bool bDrawDebug = false;

	UFUNCTION(BlueprintPure, Category = "Path")
	USplineComponent* GetSplinePath() const { return SplinePath; }

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

	/** Pick a continuation route. Returns null at a dead end. */
	UFUNCTION(BlueprintCallable, Category = "Path")
	ADGPathActor* ChooseNextPath() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
};
