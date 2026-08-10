// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DGDeliveryPointActor.generated.h"

class UBoxComponent;

/**
 * A named place packages are collected from or delivered to. Native parent for the
 * BP_Delivery_Start / BP_Delivery_End Blueprints (reparented in place, like the traffic classes).
 *
 * The actor is deliberately dumb: it registers itself with UDGDeliverySubsystem, reports when a
 * player-controlled pawn enters its trigger, and exposes Blueprint events for the visual dressing
 * (beacon VFX, door highlights). Whether an arrival *means* anything is entirely the subsystem's
 * call — the same point can be a pickup in one job and a dropoff in the next.
 */
UCLASS(Blueprintable, ClassGroup = (Delivery), meta = (DisplayName = "Delivery Point"))
class DELIVERYGAME_API ADGDeliveryPointActor : public AActor
{
	GENERATED_BODY()

public:
	ADGDeliveryPointActor();

	/**
	 * Stable identifier used by job authoring and lookups. None falls back to the actor's FName,
	 * which is fine for testing but not stable across renames — name the real destinations
	 * explicitly (e.g. "NessMart", "BushedBaby", "Home").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery")
	FName PointId;

	/** Name shown on the phone: "Ness Mart", "The Bushed Baby"... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery")
	FText DisplayName;

	/**
	 * The arrival zone. Native root component (the Blueprints being reparented carry only an empty
	 * DefaultSceneRoot, so unlike the traffic classes there is nothing to resolve and preserve —
	 * creating our own is safe and keeps every instance's zone consistent).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<UBoxComponent> Trigger;

	/** Per-actor debug switch; dg.TrafficDebugDraw gates the lot, same contract as traffic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delivery|Debug")
	bool bDrawDebug = false;

	// ------------------------------------------------------------ BP hooks
	// Visual dressing lives in the Blueprint children. Called by the subsystem.

	/** This point just became the active objective (drive here now). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnBecameActiveObjective();

	/** The player just completed this point's part of the job (picked up / delivered). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnObjectiveCompleted();

	/** This point is no longer part of the active job (job ended, failed, or moved on). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnCleared();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void OnTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
