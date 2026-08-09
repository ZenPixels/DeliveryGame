// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "DGAIVehiclePawn.generated.h"

class UAudioComponent;
class UBoxComponent;
class UDGPathFollowComponent;
class USoundBase;

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
};
