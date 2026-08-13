// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delivery/DGDeliveryTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DGDeliverySubsystem.generated.h"

class ADGDeliveryPointActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDGOffersChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDGJobEvent, int32, JobId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDGJobPaidEvent, int32, JobId, int32, PayoutDollars);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDGMoneyChangedEvent, int32, NewMoneyDollars);

/**
 * The delivery economy: a board of expiring offers, a handful of jobs held at once, and payouts
 * that decay rather than fail.
 *
 * Shape of the loop (author, 2026-08-11/12):
 *  - Several **offers** sit on the board at once, each with its own countdown. Choosing under
 *    time pressure — triage — is the game, not route optimisation.
 *  - The player holds up to **JobCapacity** jobs (a jeep upgrade raises it: roof rack, storage).
 *  - Delivering after the full-value window does not fail the job; the **tip decays** toward the
 *    floor instead, so the story continues for a struggling player.
 *  - **Dawdling blocks the queue**: while any held job is decaying, no new offers are generated,
 *    so the board drains and the work dries up until the player digs out.
 *  - **VIP jobs are taken alone** and are exempt from the queue rules — story work never competes
 *    with routine work.
 *
 * A "primary job" view (GetState / GetActiveJob / GetTimeRemaining) is retained on top of the
 * multi-job model so the existing phone widget keeps working until the real board UI is built.
 */
UCLASS()
class DELIVERYGAME_API UDGDeliverySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------- Point registry

	void RegisterPoint(ADGDeliveryPointActor* Point);
	void UnregisterPoint(ADGDeliveryPointActor* Point);

	UFUNCTION(BlueprintPure, Category = "Delivery")
	TArray<ADGDeliveryPointActor*> GetRegisteredPoints() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	ADGDeliveryPointActor* FindPoint(FName PointId) const;

	// ------------------------------------------------------------------ Board

	/** Offers currently on the board, soonest to expire first. */
	UFUNCTION(BlueprintPure, Category = "Delivery|Board")
	TArray<FDGDeliveryJob> GetOffers() const;

	/** Jobs the player is holding (accepted or carrying). */
	UFUNCTION(BlueprintPure, Category = "Delivery|Board")
	TArray<FDGDeliveryJob> GetHeldJobs() const;

	UFUNCTION(BlueprintPure, Category = "Delivery|Board")
	bool GetJob(int32 JobId, FDGDeliveryJob& OutJob) const;

	/** Take an offer. Fails when at capacity, or when VIP rules forbid it. */
	UFUNCTION(BlueprintCallable, Category = "Delivery|Board")
	bool AcceptOffer(int32 JobId);

	/** Remove an offer from the board without taking it. */
	UFUNCTION(BlueprintCallable, Category = "Delivery|Board")
	bool DeclineOffer(int32 JobId);

	/** Drop a held job. No payout; the package is simply not delivered. */
	UFUNCTION(BlueprintCallable, Category = "Delivery|Board")
	bool AbandonJob(int32 JobId);

	/** What this job would pay if delivered right now, after any decay. */
	UFUNCTION(BlueprintPure, Category = "Delivery|Board")
	int32 GetCurrentPayout(int32 JobId) const;

	/**
	 * Seconds left on whatever clock currently governs this job: the offer countdown while
	 * offered, the full-value window while carrying. 0 when untimed or already expired.
	 */
	UFUNCTION(BlueprintPure, Category = "Delivery|Board")
	float GetSecondsRemaining(int32 JobId) const;

	/** True while a held job is overdue, which stops new offers arriving. */
	UFUNCTION(BlueprintPure, Category = "Delivery|Board")
	bool IsOfferQueueBlocked() const;

	UFUNCTION(BlueprintPure, Category = "Delivery|Board")
	int32 GetHeldJobCount() const;

	/**
	 * Post a story delivery. Untimed when TimeLimit is 0; always taken alone.
	 * @param ParcelMesh  What is being carried. Null uses the pickup point's default parcel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Delivery|Board")
	int32 OfferVipJob(FName PickupId, FName DropoffId, int32 PayoutDollars, float TimeLimitSeconds,
		const FText& Summary, UStaticMesh* ParcelMesh = nullptr);

	/** Change what a job is carrying. The pickup updates on the spot if it is already showing. */
	UFUNCTION(BlueprintCallable, Category = "Delivery|Board")
	bool SetJobParcelMesh(int32 JobId, UStaticMesh* ParcelMesh);

	/**
	 * Parcels that ordinary generated jobs pick from at random. Empty means every generated job
	 * uses whatever its pickup point defaults to — fill this to get variety on the streets.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Delivery|Tuning")
	TArray<TSoftObjectPtr<UStaticMesh>> GeneratedParcelMeshes;

	// ------------------------------------------------------------------ Money

	UFUNCTION(BlueprintPure, Category = "Delivery")
	int32 GetMoney() const { return MoneyDollars; }

	/** Charge the player (fines, insurance, rent). Negative amounts are ignored. */
	UFUNCTION(BlueprintCallable, Category = "Delivery")
	void ChargeMoney(int32 Amount);

	// --------------------------------------------------- Primary-job compatibility
	//
	// The phone widget polls these. They describe whichever held job matters most right now:
	// the one being carried, else the one being fetched.

	UFUNCTION(BlueprintPure, Category = "Delivery")
	EDGDeliveryState GetState() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FDGDeliveryJob GetActiveJob() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	float GetTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FVector GetCurrentObjectiveLocation(bool& bValid) const;

	/** Accept the best offer going, generating one if the board is empty. */
	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool StartRandomJob();

	/** Abandon the primary job. */
	UFUNCTION(BlueprintCallable, Category = "Delivery")
	void CancelJob();

	// -------------------------------------------------------------- Interaction
	//
	// Arriving no longer completes anything: the player presses interact (IA_Action / E) while in
	// range. Blueprint side just calls TryInteract from that input.

	/** Require an interact press to collect or hand over. False restores arrival-completes, for testing. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	bool bRequireInteractToComplete = true;

	/** Called by ADGDeliveryPointActor as the player enters and leaves its trigger. */
	void SetPointInRange(ADGDeliveryPointActor* Point, bool bInRange);

	/**
	 * Do whatever the nearest in-range point offers — collect parcels, hand parcels over, or both.
	 * @return true if anything happened, so the caller can play a sound or animation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool TryInteract();

	/** Prompt for the point the player is standing at, e.g. "Collect for The Bushed Baby". */
	UFUNCTION(BlueprintPure, Category = "Delivery")
	bool GetInteractPrompt(FText& OutPrompt) const;

	/** How many parcels are currently being carried — for showing boxes on the player or jeep. */
	UFUNCTION(BlueprintPure, Category = "Delivery")
	int32 GetCarriedCount() const;

	/**
	 * What this point is to the player right now. A point is a *place*; whether it is a pickup or
	 * a drop-off is a property of the current jobs, not of the actor.
	 */
	UFUNCTION(BlueprintPure, Category = "Delivery")
	EDGPointRole GetPointRole(const ADGDeliveryPointActor* Point) const;

	/** A player-controlled pawn reached a point. Called by ADGDeliveryPointActor. */
	void NotifyPlayerAtPoint(ADGDeliveryPointActor& Point);

	// ------------------------------------------------------------------ Events

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGOffersChanged OnOffersChanged;

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGJobEvent OnJobAccepted;

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGJobEvent OnJobPickedUp;

	/** Delivered. The payout passed here is the decayed amount actually paid. */
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGJobPaidEvent OnJobDelivered;

	/** An offer lapsed, or a held job was abandoned. */
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGJobEvent OnJobLost;

	/** A held job just passed its full-value deadline and started losing money. */
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGJobEvent OnJobDecayStarted;

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FDGMoneyChangedEvent OnMoneyChanged;

	// ----------------------------------------------------------------- Tuning
	//
	// Runtime-writable so they can be tuned live over MCP, and so the future upgrade/difficulty
	// systems have something to turn: job capacity is a jeep upgrade, and the decay numbers are
	// the difficulty dial a story mode would relax.

	/** How many offers to keep on the board. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	int32 MaxOffers = 3;

	/** How long an offer survives unaccepted. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	float OfferLifetimeSeconds = 45.f;

	/** How many jobs may be held at once. Raised by the roof-rack/storage upgrades. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	int32 JobCapacity = 2;

	/** Flat fare before distance. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	int32 BaseFareDollars = 4;

	/** Added per 100 m of estimated route. The island is small; per-km rounded to nothing. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	float FarePer100m = 1.2f;

	/** Straight-line to road-distance fudge. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	float RouteDistanceFactor = 1.5f;

	/** Pace a competent delivery is expected to hold, cm/s (~22 mph). */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	float ExpectedSpeedCmPerSec = 1000.f;

	/** Slack added to the distance-derived full-value window. */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	float FullValueGraceSeconds = 20.f;

	/** Decay window as a fraction of the full-value window (never below MinDecaySeconds). */
	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	float DecayWindowFraction = 0.8f;

	UPROPERTY(BlueprintReadWrite, Category = "Delivery|Tuning")
	float MinDecaySeconds = 25.f;

	// ------------------------------------------------- UTickableWorldSubsystem

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TMap<FName, TWeakObjectPtr<ADGDeliveryPointActor>> Points;

	/** Offers and held jobs together; Stage says which is which. Finished jobs are removed. */
	UPROPERTY(Transient)
	TArray<FDGDeliveryJob> Jobs;

	int32 NextJobId = 1;

	int32 MoneyDollars = 50;

	/** Points currently flagged as objectives, so markers can be added and cleared on change. */
	TSet<TWeakObjectPtr<ADGDeliveryPointActor>> MarkedPoints;

	/** Points whose trigger the player is standing in. Usually one; two if points overlap. */
	TSet<TWeakObjectPtr<ADGDeliveryPointActor>> PointsInRange;

	/** Counts what interacting at this point would do right now. */
	void CountPendingAt(const ADGDeliveryPointActor& Point, int32& OutDeliveries, int32& OutPickups) const;

	/** In-range point with something to do, nearest first; null when there is nothing to do. */
	ADGDeliveryPointActor* FindBestInteractPoint() const;

	FDGDeliveryJob* FindJob(int32 JobId);
	const FDGDeliveryJob* FindJob(int32 JobId) const;

	/** The job the primary-job compatibility view describes. */
	const FDGDeliveryJob* GetPrimaryJob() const;

	/** Top up the board, unless the queue is blocked or there is nowhere to deliver. */
	void RefreshOffers();

	/** Build one plausible package job between two distinct registered points. */
	bool MakePackageOffer(FDGDeliveryJob& OutJob);

	/** Distance-derived timing and fare, shared by generated and authored jobs. */
	void PriceJob(FDGDeliveryJob& Job) const;

	int32 ComputePayout(const FDGDeliveryJob& Job) const;

	/** Add/clear objective markers so every held job's current target is lit. */
	void RefreshPointMarkers();

	/** Parcel of the job waiting to be collected at this point, loading it if needed. */
	UStaticMesh* ResolveParcelMeshFor(const ADGDeliveryPointActor& Point) const;

	void SetMoney(int32 NewMoney);
};
