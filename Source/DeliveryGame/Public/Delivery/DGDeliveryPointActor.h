// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delivery/DGDeliveryTypes.h"
#include "GameFramework/Actor.h"
#include "DGDeliveryPointActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

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

	/**
	 * The parcel waiting to be collected. Created and driven from C++ on purpose: the same
	 * component added through a Blueprint template never reached the placed instances — they
	 * carry archetype overrides, so every point sat there with an empty mesh (2026-08-12).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<UStaticMeshComponent> ParcelMesh;

	/** Show the parcel while this point is a pickup. Off for points that should look empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delivery")
	bool bShowParcelWhenPickup = true;

	/** Parcel used when the job does not name one of its own. Per-point, so a bakery can box differently. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delivery")
	TObjectPtr<UStaticMesh> DefaultParcelMesh;

	/** What this point is to the player right now. Maintained by UDGDeliverySubsystem. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	EDGPointRole CurrentRole = EDGPointRole::None;

	/**
	 * Adopt a role: shows the parcel only where something is actually waiting to be collected.
	 * @param ParcelMeshForRole  The waiting job's own parcel, or null to use DefaultParcelMesh.
	 */
	void ApplyRole(EDGPointRole NewRole, UStaticMesh* ParcelMeshForRole = nullptr);

	/** Per-actor debug switch; dg.TrafficDebugDraw gates the lot, same contract as traffic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Delivery|Debug")
	bool bDrawDebug = false;

	/** True while a player-controlled pawn is inside the trigger. Drives the interact prompt. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	bool bPlayerInRange = false;

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

	/**
	 * The player entered or left interaction range. Show/hide the "Press E" prompt here — the
	 * prompt should appear whenever the player is in range at all, even with nothing to do, so
	 * that the point reads as interactive rather than broken.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnPlayerInRangeChanged(bool bInRange);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void OnTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

private:
	/** Player pawns only — AI traffic driving through a forecourt must not count. */
	static bool IsPlayerPawn(const AActor* Actor);
};
