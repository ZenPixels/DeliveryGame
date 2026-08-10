// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delivery/DGDeliveryTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DGDeliverySubsystem.generated.h"

class ADGDeliveryPointActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDGDeliveryEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDGDeliveryPayoutEvent, int32, PayoutDollars);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDGMoneyChangedEvent, int32, NewMoneyDollars);

/**
 * The delivery loop: request -> pickup -> timed drive -> deliver -> payout.
 *
 * World subsystem so the phone app, the guidance arrow, and the delivery points all talk to one
 * authority without a manager actor to place per map. Points register themselves on BeginPlay
 * (same pattern as UDGTrafficSubsystem's path registry).
 *
 * Scope notes for this first slice (author roadmap, 2026-08-10):
 *  - One job at a time, offered as a random pair of registered points.
 *  - Running out of time fails the job outright. Softening (late = reduced pay) can come later.
 *  - Money lives here for now. When save games / survival costs arrive it moves to player state —
 *    everything reads it through GetMoney so the move is cheap.
 */
UCLASS()
class DELIVERYGAME_API UDGDeliverySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------- Registry

	void RegisterPoint(ADGDeliveryPointActor* Point);
	void UnregisterPoint(ADGDeliveryPointActor* Point);

	/** All currently registered points. Editor-placed points appear once their BeginPlay runs. */
	UFUNCTION(BlueprintPure, Category = "Delivery")
	TArray<ADGDeliveryPointActor*> GetRegisteredPoints() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	ADGDeliveryPointActor* FindPoint(FName PointId) const;

	// --------------------------------------------------------------- Jobs

	/**
	 * Accept a job between two random distinct registered points.
	 * The phone's "accept" button in its simplest possible form.
	 */
	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool StartRandomJob();

	/** Accept a specific job. Fails (false) if a job is active or a point cannot be found. */
	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool StartJob(FName PickupId, FName DropoffId);

	/** Abandon the active job. No payout, no penalty — penalties are an economy-era decision. */
	UFUNCTION(BlueprintCallable, Category = "Delivery")
	void CancelJob();

	/**
	 * A player-controlled pawn arrived at a point. Called by ADGDeliveryPointActor; ignored unless
	 * the point is the active objective.
	 */
	void NotifyPlayerAtPoint(ADGDeliveryPointActor& Point);

	// -------------------------------------------------------------- State

	UFUNCTION(BlueprintPure, Category = "Delivery")
	EDGDeliveryState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Delivery")
	const FDGDeliveryJob& GetActiveJob() const { return ActiveJob; }

	/** Seconds left to deliver. 0 when no timer is running. */
	UFUNCTION(BlueprintPure, Category = "Delivery")
	float GetTimeRemaining() const;

	/** Where the player should be heading right now. bValid false when idle. */
	UFUNCTION(BlueprintPure, Category = "Delivery")
	FVector GetCurrentObjectiveLocation(bool& bValid) const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	int32 GetMoney() const { return MoneyDollars; }

	// ------------------------------------------------------------- Events

	/** A job was accepted; head to the pickup. */
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGDeliveryEvent OnJobStarted;

	/** Package collected; the timer is now running. */
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGDeliveryEvent OnPickupComplete;

	/** Delivered in time. Payout already applied when this fires. */
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGDeliveryPayoutEvent OnDelivered;

	/** Timer expired (or job cancelled). No payout. */
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGDeliveryEvent OnJobFailed;

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGMoneyChangedEvent OnMoneyChanged;

	// ------------------------------------------------- UTickableWorldSubsystem

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Registered points, by PointId. Weak so a destroyed point cannot dangle. */
	TMap<FName, TWeakObjectPtr<ADGDeliveryPointActor>> Points;

	UPROPERTY(Transient)
	FDGDeliveryJob ActiveJob;

	EDGDeliveryState State = EDGDeliveryState::Idle;

	/** World seconds at which the running delivery fails. */
	float DeliveryDeadline = 0.f;

	/** Starting cash: broke enough to need the job, not so broke the first fine ends the run. */
	int32 MoneyDollars = 50;

	// Tuning. Constants for the slice; promote to settings when the economy grows around them.

	/** Straight-line to road-distance fudge: routes on the grid are longer than the crow flies. */
	static constexpr float RouteDistanceFactor = 1.5f;

	/** Average speed a competent delivery, in cm/s, expects to hold (~22 mph). */
	static constexpr float ExpectedSpeedCmPerSec = 1000.f;

	/** Grace added on top of the distance-derived time, in seconds. */
	static constexpr float TimeLimitGraceSeconds = 25.f;

	static constexpr int32 BasePayoutDollars = 5;
	static constexpr int32 PayoutDollarsPerKm = 2;

	/** Build the job's timer and payout from the distance between its two points. */
	void PriceJob(FDGDeliveryJob& Job) const;

	void SetMoney(int32 NewMoney);
	void FailActiveJob(const TCHAR* Reason);
	void ClearJobPoints();
};
