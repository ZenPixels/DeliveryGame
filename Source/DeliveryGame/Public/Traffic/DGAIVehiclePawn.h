// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "DGAIVehiclePawn.generated.h"

class UAudioComponent;
class UBoxComponent;
class UDGPathFollowComponent;
class USoundBase;

/** Where this vehicle stands in the hit-by-something lifecycle. */
UENUM(BlueprintType)
enum class EDGImpactState : uint8
{
	/** Normal kinematic traffic. */
	Driving,

	/** Knocked into full physics simulation; waiting to come to rest. */
	Simulating,

	/** Recovered and upright, sitting out the personality-scaled shaken pause. */
	Shaken,

	/** Settled but not upright — a permanent wreck until some future tow system exists. */
	Wrecked,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDGVehicleStruckEvent, AActor*, StruckBy, float, ImpulseMagnitude);

/**
 * Base pawn for AI traffic vehicles. Native replacement for BP_AI_Vehicle_Base.
 *
 * Owns the path-follow component plus the three trigger volumes the Blueprint used:
 *   RouteCollider   - identifies this vehicle to ADGPathDeciderActor volumes.
 *   TrafficCollider - forward volume; anything in it holds the vehicle stationary.
 *   StopZone        - stop-sign / junction volume; also holds the vehicle stationary.
 *
 * Blocking is reference-counted, so two overlapping sources releasing at different times
 * cannot leave the vehicle stuck the way a single boolean could.
 */
UCLASS(Blueprintable, ClassGroup = (Traffic), meta = (DisplayName = "AI Vehicle"))
class DELIVERYGAME_API ADGAIVehiclePawn : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	ADGAIVehiclePawn(const FObjectInitializer& ObjectInitializer);

	// ------------------------------------------------------------ Components
	//
	// All of these are **resolved from the components the Blueprint already owns**, not created
	// here. BP_AI_Vehicle_Base carries its own Route Collider, Traffic Collider, path-follow and
	// crash-audio components, and creating natives alongside them would leave every vehicle with
	// two path-follow components fighting over the throttle each frame. Resolution happens in
	// BeginPlay via ResolveComponents().

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle")
	TObjectPtr<UDGPathFollowComponent> PathFollow;

	/** Marks this vehicle to path-decider volumes. Matched by the "routing" tag, then by name. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Colliders")
	TObjectPtr<UBoxComponent> RouteCollider;

	/** Forward-facing volume; overlaps hold the vehicle stationary. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Colliders")
	TObjectPtr<UBoxComponent> TrafficCollider;

	/** Stop-sign / junction volume; overlaps hold the vehicle stationary. Optional. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Colliders")
	TObjectPtr<UBoxComponent> StopZone;

	/** Plays the crash MetaSound. Assign a MetaSound source to drive CrashTriggerParameterName. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Audio")
	TObjectPtr<UAudioComponent> CrashAudio;

	// ---------------------------------------------------------------- Audio

	/** Crash sound. A MetaSound source lets TriggerParameterName and IntensityParameterName apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio")
	TObjectPtr<USoundBase> CrashSound;

	/**
	 * Trigger fired on the crash MetaSound for an impact at or above the threshold.
	 * "Reset Sound" is the input name MetaSound_Car_Crash actually exposes — it restarts the sound
	 * so consecutive impacts retrigger rather than being swallowed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio")
	FName CrashTriggerParameterName = TEXT("Reset Sound");

	/** Trigger fired for a sub-threshold impact, silencing any crash still playing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio")
	FName CrashStopTriggerParameterName = TEXT("Stop Sound");

	/**
	 * Optional float input set to normalised impact strength before the trigger fires.
	 * None by default: MetaSound_Car_Crash exposes no such input, and the original never set one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio")
	FName CrashIntensityParameterName = NAME_None;

	/**
	 * Impacts below this impulse magnitude stop the crash sound instead of playing it.
	 * 100000 matches the original's `VectorLength(NormalImpulse) / 1000 > 100` test.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio", meta = (ClampMin = "0.0"))
	float CrashImpulseThreshold = 100000.f;

	/** Impulse magnitude that maps to full crash intensity, when CrashIntensityParameterName is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio", meta = (ClampMin = "1.0"))
	float CrashImpulseAtFullIntensity = 500000.f;

	/** Minimum seconds between crash sounds, so one collision cannot machine-gun the trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio", meta = (ClampMin = "0.0", Units = "s"))
	float CrashSoundCooldown = 0.35f;

	// ------------------------------------------------- Detection volume shape

	/**
	 * Force the traffic volume's shape at BeginPlay rather than trusting the authored component.
	 *
	 * **Keep this on.** The authored volume is a 32 cm cube 100 cm *above* the vehicle centre, which
	 * can never overlap anything ahead, and every placed vehicle carries that as a per-instance
	 * override. Fixing it in the asset proved unreliable — archetype overrides shadow the template,
	 * and the editor tooling writes only the X component of a vector to an instance — so the geometry
	 * is applied from code, which nothing can shadow or partially write.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Colliders")
	bool bOverrideTrafficColliderShape = true;

	/**
	 * Centre of the traffic volume, relative to the vehicle. Reaches from roughly
	 * `X - Extent.X` to `X + Extent.X` ahead, which must cover the braking distance at cruise speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Colliders")
	FVector TrafficColliderOffset = FVector(1300.f, 0.f, 30.f);

	/**
	 * Half-extent of the traffic volume. Y should be about a lane half-width.
	 *
	 * **Keep `Offset.X - Extent.X` near zero.** At 1450/1150 the volume began 300 cm ahead of the
	 * vehicle, leaving a blind spot right in front of the bumper: a follower that closed inside 3 m
	 * lost sight of the car ahead, cleared its clearance to "nothing there", and accelerated into it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Colliders")
	FVector TrafficColliderExtent = FVector(1250.f, 90.f, 60.f);

	// --------------------------------------------------------------- Signals

	/**
	 * In-zone speed above which a stop aspect is ignored: the vehicle is already crossing, so it
	 * completes the manoeuvre instead of freezing mid-junction when the light flips. At or below it,
	 * the vehicle is queueing and holds. Stateless commitment — no per-light bookkeeping to go stale.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Signals", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SignalCommitSpeed = 200.f;

	/** How far ahead a stop-aspect signal starts being braked for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Signals", meta = (ClampMin = "0.0", Units = "cm"))
	float SignalDetectionRange = 3500.f;

	// ---------------------------------------------------------- Right of way
	//
	// Author's rules (2026-08-10): turning yields to straight; a left turn yields to oncoming; at
	// an uncontrolled T the terminating road yields to the through road — which falls out of
	// "turning yields to straight" for free, because a stem arrival has no straight option.
	// Evaluated pairwise per vehicle against the subsystem's registry, pushed to PathFollow as a
	// give-way line. No junction manager: intent (PlannedNextPath) is known ~25 m early, so each
	// vehicle can classify every rival's movement the same way it classifies its own.

	/** How far before its junction a vehicle starts weighing right-of-way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|RightOfWay", meta = (ClampMin = "0.0", Units = "cm"))
	float YieldEvaluateDistance = 2000.f;

	/** Stop this far short of the junction centre when giving way — roughly the pad edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|RightOfWay", meta = (ClampMin = "0.0", Units = "cm"))
	float YieldStandoffDistance = 650.f;

	/** A conflicting vehicle arriving within this many seconds is given way to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|RightOfWay", meta = (ClampMin = "0.1", Units = "s"))
	float YieldEtaWindow = 3.f;

	/** Give way to a vehicle that is itself stopped for at most this long, then proceed anyway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|RightOfWay", meta = (ClampMin = "0.0", Units = "s"))
	float YieldDeadlockTimeout = 5.f;

	/** Movements within this angle of dead ahead count as "straight" for right-of-way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|RightOfWay", meta = (ClampMin = "5.0", ClampMax = "80.0", Units = "deg"))
	float YieldStraightAngle = 25.f;

	// --------------------------------------------------------- Impact reaction
	//
	// The second half of the "hybrid: kinematic until hit" decision (author, 2026-08-09): a hard
	// hit hands the body to full physics, the crash plays out honestly, and once the vehicle
	// settles it recovers — with a personality-scaled pause — or stays wrecked if it's on its
	// roof. Reactions are personality expressions: tune the pause per vehicle instance.

	/** Hit impulse above which the vehicle is knocked out of kinematic driving into physics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "0.0"))
	float PhysicsImpactThreshold = 250000.f;

	/** Arcade shove: fraction of the striker's velocity applied to this vehicle on takeover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float ImpactShoveScale = 0.9f;

	/** Speed below which the simulating body counts as settled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "1.0", Units = "cm/s"))
	float SettleSpeed = 60.f;

	/** Continuous settled seconds before recovery is attempted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "0.1", Units = "s"))
	float SettleTime = 1.2f;

	/**
	 * How long the driver sits shaken after recovering, before driving on. The personality knob:
	 * the cautious van should sit much longer than the menace (set per instance alongside the
	 * other personality values).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "0.0", Units = "s"))
	float PostCrashPauseSeconds = 3.f;

	/** Minimum dot(vehicle up, world up) to count as upright enough to recover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UprightDot = 0.7f;

	UPROPERTY(BlueprintReadOnly, Category = "AI Vehicle|Impact")
	EDGImpactState ImpactState = EDGImpactState::Driving;

	/**
	 * Allow a single wheel to be torn off by a hard hit landing near it.
	 *
	 * The chassis-only simulation below is what stops **every** wheel flying off (handing the
	 * whole skeletal mesh to physics ragdolls each body, and a big impulse rips the joints apart —
	 * observed 2026-08-11, "too much and kind of silly"). This is the deliberate version: one
	 * wheel, near the impact, only past WheelDetachImpulse.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact")
	bool bAllowWheelDetach = true;

	/**
	 * Impulse needed to tear a wheel off. Deliberately an order of magnitude above
	 * PhysicsImpactThreshold: measured player hits run ~500k for a shunt to ~4.5M for a
	 * flat-out ram (2026-08-11 log), and at 700k every solid hit took a wheel. 3M keeps it for
	 * the spectacular ones, per the author's "only the worst crashes" rule.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "0.0"))
	float WheelDetachImpulse = 3000000.f;

	/** How close to a wheel the impact must land to take it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Impact", meta = (ClampMin = "0.0", Units = "cm"))
	float WheelDetachRadius = 130.f;

	/** Fired once when something knocks this vehicle into physics. The future fines/insurance hook. */
	UPROPERTY(BlueprintAssignable, Category = "AI Vehicle|Impact")
	FDGVehicleStruckEvent OnStruck;

	/** Knock this vehicle into physics with a shove, as if hit. Also used by impacts internally. */
	UFUNCTION(BlueprintCallable, Category = "AI Vehicle|Impact")
	void EnterPhysicsReaction(AActor* StruckBy, const FVector& ShoveVelocity, float ImpulseMagnitude);

	// ---------------------------------------------------------- Near-miss dodge

	/** Range within which a fast-closing player counts as a threat worth dodging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Dodge", meta = (ClampMin = "0.0", Units = "cm"))
	float DodgeSenseRange = 1400.f;

	/** Closing speed toward this vehicle that triggers the dodge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Dodge", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DodgeCloseSpeed = 700.f;

	/** How far the vehicle swerves off its lane, in cm. Relaxes back on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Dodge", meta = (ClampMin = "0.0", Units = "cm"))
	float DodgeAmount = 190.f;

	/** Optional horn honked when dodging. Null-safe: silence until an asset is assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Dodge")
	TObjectPtr<USoundBase> HornSound;

	/** Minimum seconds between horn honks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Dodge", meta = (ClampMin = "0.0", Units = "s"))
	float HornCooldown = 2.5f;

	// ---------------------------------------------------------------- Debug

	/** Draw the path-follow aim line and status text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Debug")
	bool bDrawDebug = false;

	/** Also draw the three trigger volumes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Debug")
	bool bDrawDebugColliders = false;

	// ------------------------------------------------------------------ API

	/**
	 * Reference-counted hold for sources that are not overlap-driven — a path decider waiting
	 * on a junction, say. Each AddBlocker must be paired with a RemoveBlocker.
	 * Overlaps of TrafficCollider / StopZone are tracked separately and need no calls here.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI Vehicle")
	void AddBlocker();

	UFUNCTION(BlueprintCallable, Category = "AI Vehicle")
	void RemoveBlocker();

	/** True while any manual blocker or any blocking overlap is active. */
	UFUNCTION(BlueprintPure, Category = "AI Vehicle")
	bool IsBlocked() const;

	UFUNCTION(BlueprintPure, Category = "AI Vehicle")
	float GetForwardSpeedMPH() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void OnColliderBeginOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnColliderEndOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	UFUNCTION()
	void OnMeshHit(
		UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	/** True if an overlapping actor should hold this vehicle. Override to add cases. */
	UFUNCTION(BlueprintNativeEvent, Category = "AI Vehicle")
	bool ShouldBlockFor(AActor* OtherActor) const;
	virtual bool ShouldBlockFor_Implementation(AActor* OtherActor) const;

private:
	/** Bind PathFollow / colliders / audio to the components this actor already owns. */
	void ResolveComponents();

	/** First box component matching Tag, else one whose name contains NameSubstring. */
	UBoxComponent* FindBox(const FName& Tag, const FString& NameSubstring) const;

	/** Wire overlap events on a resolved blocking volume. */
	void BindBlockingVolume(UBoxComponent* Volume);

	/** Push the current blocker count onto the path-follow component. */
	void SyncBlockedState();

	/**
	 * True while this vehicle is committed through a signal zone: it entered moving (or on green) and
	 * has not yet left. A committed vehicle is immune to holds, so a light flipping mid-crossing — or
	 * a tight turn clipping the *oncoming approach's* pad — cannot freeze it inside the junction.
	 * Cleared the moment it is outside every signal zone.
	 */
	bool bSignalCommitted = false;

	/** Manual holds from AddBlocker / RemoveBlocker. */
	int32 BlockerCount = 0;

	float TimeSinceLastCrashSound = 0.f;

	/**
	 * Actors currently overlapping a blocking volume, rebuilt from scratch on every overlap
	 * change. One actor can sit in both TrafficCollider and StopZone and each volume reports
	 * end-overlap separately, so incremental add/remove would either double-count on entry or
	 * release early on exit. Recomputing is only paid on overlap events, not per frame.
	 */
	TSet<TWeakObjectPtr<AActor>> BlockingActors;

	/** Rebuild BlockingActors from the current overlaps of TrafficCollider and StopZone. */
	void RecomputeOverlapBlockers();

	/**
	 * Re-measure the distance to the nearest tracked obstacle ahead and push it to PathFollow.
	 * Runs per tick while an overlap is live: the gap closes continuously, so sampling it only on
	 * overlap enter/exit would leave a stale clearance and the wrong throttle.
	 */
	void UpdateTrafficClearance();

	/**
	 * Ask every registered signal whether it applies to this vehicle: hold if queueing inside a
	 * stop-aspect zone, and feed the nearest governing stop line to PathFollow so the vehicle brakes
	 * on approach instead of only reacting once inside. Pull rather than push — recomputed from
	 * ground truth every tick, so no hold can go stale.
	 */
	void UpdateSignalAwareness();

	/**
	 * Evaluate the right-of-way rules against every other registered vehicle approaching the same
	 * junction, and push the resulting give-way line (or "none") to PathFollow. Same pull-model
	 * contract as UpdateSignalAwareness.
	 */
	void UpdateYieldAwareness(float DeltaSeconds);

	/** Who this vehicle is currently giving way to, for transition logging only. */
	TWeakObjectPtr<const AActor> CurrentYieldTo;

	/** Seconds continuously spent giving way, for the deadlock escape. */
	float TimeYieldHeld = 0.f;

	/** World time before which yields are ignored, set by the deadlock escape. */
	float YieldSuppressedUntilTime = -1.f;

	/** Drive the Simulating -> Shaken -> Driving recovery. */
	void UpdateImpactReaction(float DeltaSeconds);

	/** Swerve away from a fast-closing player. */
	void UpdateNearMissDodge();

	float TimeSettled = 0.f;
	float TimeSimulating = 0.f;
	float TimeShaken = 0.f;
	float TimeSinceHorn = 1000.f;

	/** Wheel bone names, read from the movement component's wheel setups rather than hard-coded. */
	TArray<FName> WheelBoneNames;

	/** A wheel came off: this vehicle never drives again, however upright it lands. */
	bool bWheelDetached = false;

	void CacheWheelBones();

	/** Keep wheel bodies kinematic so only the chassis simulates — see bAllowWheelDetach. */
	void LockWheelBodies() const;

	/** Tear off the wheel nearest the impact, if one is close enough. */
	bool TryDetachWheel(const FVector& ImpactPoint, const FVector& Impulse);
};
