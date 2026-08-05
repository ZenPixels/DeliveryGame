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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle")
	TObjectPtr<UDGPathFollowComponent> PathFollow;

	/** Marks this vehicle to path-decider volumes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Colliders")
	TObjectPtr<UBoxComponent> RouteCollider;

	/** Forward-facing volume; overlaps hold the vehicle stationary. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Colliders")
	TObjectPtr<UBoxComponent> TrafficCollider;

	/** Stop-sign / junction volume; overlaps hold the vehicle stationary. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Colliders")
	TObjectPtr<UBoxComponent> StopZone;

	/** Plays the crash MetaSound. Assign a MetaSound source to drive TriggerParameterName. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Vehicle|Audio")
	TObjectPtr<UAudioComponent> CrashAudio;

	// ---------------------------------------------------------------- Audio

	/** Crash sound. A MetaSound source lets TriggerParameterName and IntensityParameterName apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio")
	TObjectPtr<USoundBase> CrashSound;

	/** Trigger input fired on the crash MetaSound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio")
	FName CrashTriggerParameterName = TEXT("Crash");

	/** Float input set to normalised impact strength before the trigger fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio")
	FName CrashIntensityParameterName = TEXT("Intensity");

	/** Impacts below this impulse magnitude are ignored, so kerbs and scrapes stay silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio", meta = (ClampMin = "0.0"))
	float CrashImpulseThreshold = 20000.f;

	/** Impulse magnitude that maps to full crash intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio", meta = (ClampMin = "1.0"))
	float CrashImpulseAtFullIntensity = 250000.f;

	/** Minimum seconds between crash sounds, so one collision cannot machine-gun the trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Vehicle|Audio", meta = (ClampMin = "0.0", Units = "s"))
	float CrashSoundCooldown = 0.35f;

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
};
