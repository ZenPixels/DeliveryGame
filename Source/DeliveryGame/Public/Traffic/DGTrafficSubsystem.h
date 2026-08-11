// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DGTrafficSubsystem.generated.h"

class ADGAIVehiclePawn;
class ADGPathActor;
class ADGTrafficLightActor;

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

	// ------------------------------------------------------------- Signals
	//
	// Lights register here so vehicles can *ask* which signals apply to them, rather than signals
	// pushing a hold onto vehicles. A pull model recomputes from ground truth every update, so a
	// stale hold — a vehicle stopped by a light it is nowhere near — cannot survive a single frame.

	void RegisterLight(ADGTrafficLightActor* Light);
	void UnregisterLight(ADGTrafficLightActor* Light);

	UFUNCTION(BlueprintPure, Category = "Traffic")
	TArray<ADGTrafficLightActor*> GetRegisteredLights() const;

	// ------------------------------------------------------------ Vehicles
	//
	// AI vehicles register so right-of-way can ask "who else is approaching this junction"
	// without an actor iterator. Each vehicle scanning every other is O(N^2) per update —
	// fine for a handful of vans, but pair this with a spatial bucket before city scale.

	void RegisterVehicle(ADGAIVehiclePawn* Vehicle);
	void UnregisterVehicle(ADGAIVehiclePawn* Vehicle);

	UFUNCTION(BlueprintPure, Category = "Traffic")
	TArray<ADGAIVehiclePawn*> GetRegisteredVehicles() const;

private:
	UPROPERTY()
	TArray<TObjectPtr<ADGPathActor>> RegisteredPaths;

	UPROPERTY()
	TArray<TObjectPtr<ADGTrafficLightActor>> RegisteredLights;

	UPROPERTY()
	TArray<TObjectPtr<ADGAIVehiclePawn>> RegisteredVehicles;
};
