// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DGPathFollowComponent.generated.h"

class ADGPathActor;
class UChaosWheeledVehicleMovementComponent;

/**
 * Drives a Chaos wheeled vehicle along an ADGPathActor spline.
 *
 * Native replacement for the BP_Path_Follow component. Behaviour is deliberately the same
 * shape as the Blueprint — look ahead along the spline, steer at the aim point, ease off the
 * throttle in corners — with two changes worth knowing about:
 *
 *  - Progress is re-derived from the vehicle's actual position each update rather than
 *    integrated, so being knocked off the route self-corrects instead of drifting.
 *  - The nearest route comes from UDGTrafficSubsystem instead of an actor-wide search.
 */
UCLASS(Blueprintable, ClassGroup = (Traffic), meta = (BlueprintSpawnableComponent, DisplayName = "Path Follow"))
class DELIVERYGAME_API UDGPathFollowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDGPathFollowComponent();

	// ---------------------------------------------------------------- Path

	/** Claim the nearest path on BeginPlay. Mirrors the old "Auto Find Spline" flag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Path")
	bool bAutoFindSpline = true;

	/** Explicit route. Ignored when bAutoFindSpline wins at BeginPlay and this is unset. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Path Follow|Path")
	TObjectPtr<ADGPathActor> SplinePath;

	/** Start driving as soon as the component begins play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Path")
	bool bStartMovingOnBeginPlay = true;

	// ------------------------------------------------------------ Steering

	/** How far ahead along the route the vehicle aims. Larger values cut corners but wobble less. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "1.0", Units = "cm"))
	float ForwardAimDistance = 600.f;

	/** Yaw error at which steering reaches full lock. Smaller values steer more aggressively. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float SteeringSaturationAngle = 45.f;

	/** Steering slew rate. 0 applies the target steering immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0"))
	float SteeringInterpSpeed = 6.f;

	// ------------------------------------------------------------ Throttle

	/** Upper bound on throttle. An ADGPathActor with ThrottleOverride set takes precedence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxThrottle = 0.5f;

	/** Fraction of throttle retained at full steering lock. 1 disables cornering slowdown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CorneringThrottleScale = 0.6f;

	/** Cruise target. Throttle tapers to zero as speed approaches this. 0 disables the cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", Units = "mph"))
	float CruiseSpeedMPH = 25.f;

	/** Brake applied while blocked or stopped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StoppingBrakeForce = 1.f;

	// --------------------------------------------------------- Performance

	/**
	 * Seconds between destination re-evaluations; steering and throttle are still applied every
	 * frame. Replaces the hand-rolled "Time Since Last Update" accumulator in the Blueprint.
	 * 0 re-evaluates every frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Performance", meta = (ClampMin = "0.0", Units = "s"))
	float DestinationUpdateInterval = 0.1f;

	/** Distance from the end of an open route at which the next path is claimed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Performance", meta = (ClampMin = "1.0", Units = "cm"))
	float PathEndTolerance = 300.f;

	// --------------------------------------------------------------- State

	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	float DistanceAlongSpline = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	float PercentageAlongSpline = 0.f;

	/** Current look-ahead aim point in world space. */
	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	bool bIsMoving = false;

	/**
	 * Set by the owning pawn's stop zone / traffic collider. While true the throttle is cut and
	 * the brake is held, but the route and progress are retained.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Path Follow|State")
	bool bBlockedAhead = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Debug")
	bool bDrawDebug = false;

	// ----------------------------------------------------------------- API

	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void StartMoving();

	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void StopMoving();

	/**
	 * Assign a route.
	 * @param bSnapToClosestPoint  Re-derive progress from the vehicle's current position.
	 *                             Pass false when entering a route at its start.
	 */
	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void SetPath(ADGPathActor* NewPath, bool bSnapToClosestPoint = true);

	/** Re-derive progress and the look-ahead aim point from the vehicle's current position. */
	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void UpdateDestination();

	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetSteeringInput() const { return CurrentSteering; }

	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetThrottleInput() const { return CurrentThrottle; }

	UFUNCTION(BlueprintPure, Category = "Path Follow")
	ADGPathActor* GetCurrentPath() const { return SplinePath; }

	/** Multi-line status string for on-screen debugging. Replaces the BP_Debug_Text plumbing. */
	UFUNCTION(BlueprintPure, Category = "Path Follow|Debug")
	FString GetDebugStatus() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Apply steering / throttle / brake to the movement component for this frame. */
	void ProceedToDestination(float DeltaTime);

	/** Claim a continuation route at the end of an open spline. Stops the vehicle at a dead end. */
	void AdvanceToNextPath();

	void DrawDebugVisuals() const;

	UChaosWheeledVehicleMovementComponent* GetMovement() const;

private:
	float TimeSinceLastUpdate = 0.f;
	float CurrentSteering = 0.f;
	float CurrentThrottle = 0.f;

	mutable TWeakObjectPtr<UChaosWheeledVehicleMovementComponent> CachedMovement;
};
