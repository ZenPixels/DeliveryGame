// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DGTrafficSubsystem.generated.h"

class ADGPathActor;

/**
 * Registry of traffic paths in the world.
 *
 * The Blueprint version had every vehicle call FindNearestActor each time it needed a
 * route, which walks every actor in the level. Paths now self-register here, so the
 * search is over a handful of splines instead.
 */
UCLASS()
class DELIVERYGAME_API UDGTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterPath(ADGPathActor* Path);
	void UnregisterPath(ADGPathActor* Path);

	/**
	 * Nearest registered path to a world location, measured to the closest point on each spline
	 * rather than to the path actor's origin — a long spline whose origin is far away still wins
	 * if the vehicle is sitting on top of it.
	 *
	 * @param OutDistanceAlongSpline  Distance along the winning path closest to WorldLocation.
	 * @return The nearest path, or null if none are registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic")
	ADGPathActor* FindNearestPath(const FVector& WorldLocation, float& OutDistanceAlongSpline) const;

	UFUNCTION(BlueprintPure, Category = "Traffic")
	TArray<ADGPathActor*> GetRegisteredPaths() const;

	UFUNCTION(BlueprintPure, Category = "Traffic")
	int32 GetNumRegisteredPaths() const { return RegisteredPaths.Num(); }

private:
	UPROPERTY()
	TArray<TObjectPtr<ADGPathActor>> RegisteredPaths;
};
