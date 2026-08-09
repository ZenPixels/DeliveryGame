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
	TObjectPtr<ADGPathActor> TargetSpline;

	/** Start driving as soon as the component begins play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Path")
	bool bStartMovingOnBeginPlay = true;

	/**
	 * Keep trying to find a route after the vehicle has stopped for want of one.
	 *
	 * **Without this a stop is permanent.** Reaching the end of a route with no continuation within
	 * ContinuationSearchRadius calls StopMoving, and nothing ever calls StartMoving again — so one
	 * vehicle parking itself after a turn strands every vehicle queued behind it too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Path")
	bool bAutoResume = true;

	/** Seconds between attempts to find a route again while stopped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Path", meta = (ClampMin = "0.1", Units = "s"))
	float ResumeRetryInterval = 1.5f;

	// -------------------------------------------------------------- Recovery

	/**
	 * Place the vehicle back on its lane if it has been immobile off-route for StuckTimeout.
	 *
	 * Steering back is always attempted first and usually works. This is the last resort for a vehicle
	 * beached on terrain or jammed against geometry, where no steering input can free it. Ambient
	 * traffic silently rejoining is far less noticeable than a van parked in a field forever.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Recovery")
	bool bRecoverByTeleport = true;

	/** Speed below which the vehicle counts as immobile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Recovery", meta = (ClampMin = "0.0", Units = "cm/s"))
	float StuckSpeedThreshold = 40.f;

	/** Seconds of trying to move without moving before recovery fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Recovery", meta = (ClampMin = "0.5", Units = "s"))
	float StuckTimeout = 5.f;

	/** Seconds the vehicle has been immobile while trying to move. Diagnostic. */
	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|Recovery")
	float TimeStuck = 0.f;

	// ------------------------------------------------------------ Steering

	/**
	 * How far ahead along the route the vehicle aims. Larger values cut corners but wobble less.
	 *
	 * Deliberately NOT the 100 carried over from BP_AI_Vehicle_Base: there it was a probe offset in
	 * a cross-track formula, whereas here it is a pure-pursuit look-ahead, and roughly one car
	 * length of look-ahead oscillates. Re-tune from 600, not from the old number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "1.0", Units = "cm"))
	float ForwardAimDistance = 600.f;

	/**
	 * Shortest look-ahead, used at a standstill. A fixed long look-ahead makes junction turns swing
	 * wide, because the aim point sits well past the corner and the vehicle cuts straight for it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "50.0", Units = "cm"))
	float MinAimDistance = 220.f;

	/**
	 * Look-ahead expressed as travel time, added to MinAimDistance and capped by ForwardAimDistance.
	 * Long look-ahead at speed keeps straights smooth; short look-ahead when slow tightens turns.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0", Units = "s"))
	float AimTimeAhead = 0.55f;

	/**
	 * Sideways offset of the aim point from the spline, along the path's right vector. Negative
	 * aims left.
	 *
	 * The original steered from a marker ~255 cm to one side of the vehicle, which held the spline
	 * off-centre rather than centred. Pure pursuit aims at the spline itself, so set this if the
	 * splines mark a road edge or lane boundary rather than the driving line.
	 */
	/**
	 * 254.89 is the offset recovered from BP_AI_Vehicle_Base's old "Route Collider" marker, i.e. half
	 * a lane width on the Island roads.
	 *
	 * Set as the **C++ default deliberately**, not merely on the Blueprint component template: the
	 * template value is shadowed by archetype overrides on the child vehicle Blueprints and on placed
	 * instances, which silently pinned this to 0 and left traffic driving down the centre line. A C++
	 * default cannot be shadowed that way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (Units = "cm"))
	float LateralOffset = 254.89f;

	/**
	 * Extra lateral aim shift per cm of remaining lane error.
	 *
	 * Offsetting the aim point alone only converges on the lane asymptotically — the vehicle
	 * straightens up as the error shrinks, so it settles nearer the centre line than asked. This term
	 * over-steers in proportion to the error left, driving it to zero, and vanishes once the vehicle
	 * is in its lane. 0 disables lane correction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LaneCorrectionGain = 2.f;

	/**
	 * Damping on lane correction, in seconds: correction is reduced by the rate the vehicle is
	 * already closing on its lane, multiplied by this.
	 *
	 * Proportional-only correction overshoots — it keeps steering hard right until the error is gone,
	 * by which point the vehicle is crossing the centre line the other way. This term eases off as
	 * the gap closes, which is what stops the weave.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float LaneDampingGain = 0.7f;

	/** Cap on the lane-correction shift, so a badly displaced vehicle cannot aim absurdly wide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxLaneCorrection = 800.f;

	/** Signed lateral position relative to the route: positive is right of it. Diagnostic. */
	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	float CurrentLateralOffset = 0.f;

	/** Yaw error at which steering reaches full lock. Smaller values steer more aggressively. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float SteeringSaturationAngle = 45.f;

	/** Steering slew rate. 0 applies the target steering immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0"))
	float SteeringInterpSpeed = 6.f;

	// ------------------------------------------------------------ Throttle

	/**
	 * Upper bound on throttle. An ADGPathActor with ThrottleOverride set takes precedence.
	 * 0.4 is the value carried over from BP_AI_Vehicle_Base's path-follow template.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxThrottle = 0.4f;

	/** Fraction of throttle retained at full steering lock. 1 disables cornering slowdown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CorneringThrottleScale = 0.6f;

	/**
	 * Default cruise target. Overridden per road by ADGPathActor::SpeedLimitMPH, and reduced
	 * automatically for corners. 0 disables speed governing entirely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", Units = "mph"))
	float CruiseSpeedMPH = 25.f;

	/**
	 * Multiplier on the road's speed limit for this vehicle. 1 obeys it exactly.
	 *
	 * The hook for vehicles that are *meant* to break the rules — a speeding NPC at 1.3, a cautious
	 * one at 0.8, a pursuit vehicle higher still. Kept as a per-vehicle multiplier rather than letting
	 * such vehicles ignore limits outright, so a slow road still slows a speeder proportionally and
	 * corner slowdown continues to apply on top.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float SpeedLimitCompliance = 1.f;

	/**
	 * How far ahead along the route to measure curvature when deciding cornering speed.
	 *
	 * Slowing for a corner has to happen *before* entering it. Nothing did that previously, so
	 * vehicles arrived at full cruise, could not turn tightly enough, and understeered off the road.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "50.0", Units = "cm"))
	float CornerLookaheadDistance = 900.f;

	/** Heading change over the lookahead at which cornering slowdown is fully applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float CornerFullSlowAngle = 55.f;

	/** Fraction of the speed limit still permitted in the tightest corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinCornerSpeedScale = 0.35f;

	/** Brake applied while blocked or stopped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Throttle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StoppingBrakeForce = 1.f;

	/** Minimum clearance at which the vehicle runs at full throttle, regardless of speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Following", meta = (ClampMin = "0.0", Units = "cm"))
	float SafeFollowDistance = 800.f;

	/**
	 * Headway: seconds of travel to keep clear of whatever is ahead. The gap the vehicle aims to hold
	 * is `MinFollowDistance + Speed * this`, floored at SafeFollowDistance.
	 *
	 * A fixed distance is why vehicles rear-ended the stopped bus — 800 cm is ample at walking pace
	 * and nowhere near enough at 25 mph, which covers roughly 1100 cm every second.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Following", meta = (ClampMin = "0.0", Units = "s"))
	float FollowHeadwaySeconds = 1.6f;

	/**
	 * Deceleration treated as "comfortable", in cm/s^2. Brake demand reaches full when the manoeuvre
	 * needs more than this. 350 is roughly 0.36 g — firm but not an emergency stop.
	 *
	 * Lower values brake earlier and more gently; higher values leave braking later and harder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Following", meta = (ClampMin = "10.0"))
	float ComfortableDeceleration = 350.f;

	/** Clearance ahead at or below which the vehicle comes to a stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Following", meta = (ClampMin = "0.0", Units = "cm"))
	float MinFollowDistance = 300.f;

	// --------------------------------------------------------- Performance

	/**
	 * Seconds between destination re-evaluations; steering and throttle are still applied every
	 * frame. Replaces the hand-rolled "Time Since Last Update" accumulator in the Blueprint.
	 * 0 re-evaluates every frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Performance", meta = (ClampMin = "0.0", Units = "s"))
	float DestinationUpdateInterval = 0.1f;

	/**
	 * How far the vehicle may stray from its route before it is treated as lost and re-acquires the
	 * nearest path. 0 disables the check.
	 *
	 * Without this a vehicle that misses a corner keeps driving toward a stale aim point with no
	 * steering error, holds the throttle open, and leaves the map.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Performance", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistanceFromPath = 1500.f;

	/**
	 * How much better the opposite travel direction must look before the vehicle switches to it.
	 * Prevents the direction flip-flopping frame to frame near a perpendicular approach.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DirectionFlipHysteresis = 0.35f;

	/**
	 * Travel direction may only flip below this speed.
	 *
	 * A moving vehicle must never change which way it is going down the road. Without this guard a
	 * violent swerve momentarily makes the opposite direction score better, the direction flips, and
	 * because the lane offset is measured relative to travel direction the **oncoming lane instantly
	 * becomes "correct"** — so the vehicle settles there, error reading zero, and stops correcting.
	 * Flipping stays available at a standstill, which is when a genuinely mis-latched direction
	 * needs fixing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Steering", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DirectionFlipMaxSpeed = 200.f;

	/** Distance from the end of an open route at which the next path is claimed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Performance", meta = (ClampMin = "1.0", Units = "cm"))
	float PathEndTolerance = 300.f;

	/**
	 * How far from the end of a finished route to look for a continuation when the path has no
	 * NextPaths entries. Routes further away than this are not considered connected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Performance", meta = (ClampMin = "0.0", Units = "cm"))
	float ContinuationSearchRadius = 4000.f;

	/**
	 * Seconds a vehicle will stay held before ignoring its blockers, breaking mutual deadlocks where
	 * two vehicles each sit in the other's traffic volume. 0 disables the escape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Performance", meta = (ClampMin = "0.0", Units = "s"))
	float BlockedTimeout = 5.f;

	/**
	 * Allow following a route against its spline direction.
	 *
	 * **Must stay true for this project.** The Island splines run along road *centre lines* and the
	 * roads are two-way, so a single path serves traffic in both directions — there are only three
	 * splines for the whole network. Set false only for genuinely one-way routes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Path")
	bool bAllowReverseTravel = true;

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
	 * Which way along the spline this vehicle is travelling: +1 with the spline, -1 against it.
	 * Decided from the vehicle's heading whenever a route is assigned.
	 *
	 * Pure pursuit needs this explicitly. The Blueprint's cross-track controller only ever pushed the
	 * vehicle *towards* the spline, so direction fell out of whichever way the vehicle happened to be
	 * pointing; aiming at a look-ahead point does not have that property and will U-turn without it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	int32 TravelDirection = 1;

	/**
	 * Hard hold from AddBlocker / RemoveBlocker on the pawn. Throttle is cut and the brake held, but
	 * the route and progress are retained. **Subject to BlockedTimeout.**
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Path Follow|State")
	bool bBlockedAhead = false;

	/**
	 * Held at a red light. Deliberately separate from bBlockedAhead because it is **not** subject to
	 * BlockedTimeout — a deadlock escape that let vehicles run red lights would be worse than the
	 * deadlock. Cleared when the signal goes green or the vehicle leaves the volume.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	bool bHeldBySignal = false;

	/**
	 * Distance to the nearest obstacle ahead, from the pawn's front volume. Throttle scales down
	 * between SafeFollowDistance and MinFollowDistance so vehicles ease off rather than stopping dead.
	 * Defaults to a large value meaning "clear".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	float TrafficClearance = 1000000.f;

	/**
	 * Rate the gap ahead is shrinking, in cm/s. Positive means closing; negative means the vehicle
	 * ahead is pulling away. Zero when matching its speed.
	 *
	 * This is what a distance-only controller lacks. Holding 8 m behind a car doing the same speed is
	 * fine; holding 8 m while closing at 1100 cm/s is a collision already in progress. Braking is
	 * driven from this, which is the machine equivalent of watching brake lights.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Path Follow|State")
	float TrafficClosingSpeed = 0.f;

	/** On by default because BP_AI_Vehicle_Base's template had Draw Debug enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Follow|Debug")
	bool bDrawDebug = true;

	// ----------------------------------------------------------------- API

	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void StartMoving();

	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void StopMoving();

	/** Hold or release this vehicle at a traffic signal. Called by ADGTrafficLightActor. */
	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void SetSignalHold(bool bHold);

	/**
	 * Report the nearest obstacle ahead. Pass a large distance to mean "clear".
	 * @param DistanceCm   Gap to the obstacle.
	 * @param ClosingSpeed Rate the gap is shrinking, cm/s. Positive closing, negative opening.
	 */
	UFUNCTION(BlueprintCallable, Category = "Path Follow")
	void SetTrafficAhead(float DistanceCm, float ClosingSpeed);

	/**
	 * Brake demand from the obstacle ahead, 0 to 1, from the deceleration needed to avoid it.
	 * Uses `v^2 / 2d`: the deceleration required to lose the closing speed within the gap left.
	 */
	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetFollowBrake() const;

	/** Fraction of throttle permitted by the obstacle ahead: 1 clear, 0 stop. */
	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetFollowThrottleScale() const;

	/** Gap the vehicle currently wants to keep, given its speed. */
	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetDesiredFollowGap() const;

	/** Look-ahead in use this frame, scaled by speed between MinAimDistance and ForwardAimDistance. */
	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetEffectiveAimDistance() const;

	/**
	 * Speed the vehicle is currently aiming for: the road's limit (or CruiseSpeedMPH), reduced for
	 * the curvature coming up. 0 means no governing.
	 */
	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetTargetSpeedMPH() const;

	/** Fraction of the speed limit allowed by the upcoming corner: 1 straight, MinCornerSpeedScale tightest. */
	UFUNCTION(BlueprintPure, Category = "Path Follow")
	float GetCornerSpeedScale() const;

	/** True if anything is currently preventing this vehicle from driving. */
	UFUNCTION(BlueprintPure, Category = "Path Follow")
	bool IsHeld() const;

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
	ADGPathActor* GetCurrentPath() const { return TargetSpline; }

	/** True if Path runs the same way the vehicle is currently facing. */
	bool IsPathAligned(const ADGPathActor* Path) const;

	/**
	 * True if this vehicle could follow Path at all — either it runs with the vehicle's heading, or
	 * reverse travel is permitted. Public because ADGPathDeciderActor filters candidates with it.
	 */
	bool IsPathUsable(const ADGPathActor* Path) const;

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

	/**
	 * Nearest registered route starting near the current route's end that runs the same way the
	 * vehicle faces. Fallback for paths with no authored NextPaths — which is all of them, since the
	 * Blueprint design relied on deciders for continuations rather than path-to-path links.
	 */
	ADGPathActor* FindContinuationPath() const;

	/**
	 * Re-bind to the nearest route when the vehicle has strayed off its own.
	 * @return true if a route was found and assigned.
	 */
	bool ReacquireNearestPath();

	/** Track immobility and, past StuckTimeout, recover the vehicle onto its lane. */
	void UpdateStuckRecovery(float DeltaTime);

	/** Place the vehicle on its lane position facing along the route. Returns false if impossible. */
	bool SnapToLane();

	void DrawDebugVisuals() const;

	UChaosWheeledVehicleMovementComponent* GetMovement() const;

private:
	float TimeSinceLastUpdate = 0.f;
	float CurrentSteering = 0.f;
	float CurrentThrottle = 0.f;

	/** Seconds spent continuously blocked, for the BlockedTimeout escape. */
	float TimeBlocked = 0.f;

	/** Previous lane offset and its sample time, for the lane-damping rate term. */
	float PreviousLateralOffset = 0.f;
	float LastLateralSampleTime = -1.f;

	/** Counts up to ResumeRetryInterval while stopped. */
	float TimeSinceResumeAttempt = 0.f;

	mutable TWeakObjectPtr<UChaosWheeledVehicleMovementComponent> CachedMovement;
};
