// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DGDeliveryTypes.generated.h"

class ADGDeliveryPointActor;
class UStaticMesh;

/**
 * Coarse state of the player's delivery work, derived from the held jobs.
 *
 * Kept because the phone widget polls it. With several jobs in hand it describes the *primary*
 * job (the one being carried, else the one being fetched) — see UDGDeliverySubsystem::GetState.
 */
UENUM(BlueprintType)
enum class EDGDeliveryState : uint8
{
	/** Nothing accepted. */
	Idle,

	/** At least one job accepted; none picked up yet. */
	AwaitingPickup,

	/** Carrying something. */
	Delivering,
};

/** What a job is carrying, which decides how it behaves rather than just how it reads. */
UENUM(BlueprintType)
enum class EDGJobKind : uint8
{
	/** An ordinary package. The bread and butter. */
	Package,

	/** A person (the "Uber app"). Rides along and talks — the lore-delivery vehicle. */
	Passenger,

	/**
	 * A story delivery. Taken alone, exempt from the offer queue, and usually untimed: these
	 * become special by *removing* pressure, so dialog can breathe (author, 2026-08-12).
	 */
	VIP,
};

/**
 * What a delivery point currently *is* to the player, derived from their held jobs.
 *
 * Deliberately not baked into the actor: a point is a place, and its role changes job to job —
 * Ness Mart is a pickup for one delivery and a drop-off for the next, sometimes both at once.
 */
UENUM(BlueprintType)
enum class EDGPointRole : uint8
{
	/** Not part of any held job right now. */
	None,

	/** Something is waiting to be collected here. */
	Pickup,

	/** Something in the player's hands belongs here. */
	Dropoff,

	/** Both at once — hand over and collect in one stop. */
	Both,
};

/** Where a job sits in its lifecycle. */
UENUM(BlueprintType)
enum class EDGJobStage : uint8
{
	/** On the board, not yet accepted. Lapses at OfferExpiryTime. */
	Offered,

	/** Accepted; the player is heading to the pickup. No value clock yet. */
	Accepted,

	/** Picked up; the full-value window is running, then the payout decays. */
	Carrying,

	Delivered,

	/** The offer lapsed unaccepted. */
	Expired,

	/** Dropped by the player. */
	Abandoned,
};

/**
 * One delivery job, at any stage of its life — offered, held, or finished.
 *
 * **Payout decays rather than failing.** Missing the full-value window does not lose the job; the
 * tip ticks down toward FloorDollars instead (author, 2026-08-12), which keeps the story moving
 * for a struggling player and doubles as the difficulty dial for a future story mode.
 */
USTRUCT(BlueprintType)
struct FDGDeliveryJob
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 JobId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	EDGJobKind Kind = EDGJobKind::Package;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	EDGJobStage Stage = EDGJobStage::Offered;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<ADGDeliveryPointActor> Pickup = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<ADGDeliveryPointActor> Dropoff = nullptr;

	/** Seconds of full-value time granted at pickup. 0 means untimed (VIP). */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	float TimeLimitSeconds = 0.f;

	/** The undecayed payout, in whole dollars. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 PayoutDollars = 0;

	/** What the payout decays to. 0 means the whole cut can evaporate. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 FloorDollars = 0;

	/** Seconds from the end of the full-value window until the payout reaches FloorDollars. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	float DecaySeconds = 0.f;

	/** World time this offer lapses. Meaningful while Offered. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	float OfferExpiryTime = 0.f;

	/** World time the full-value window closes. Set at pickup; meaningful while Carrying. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	float FullValueDeadline = 0.f;

	/** One-line description for the opportunity board, so the player can triage at a glance. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	FText Summary;

	/**
	 * What this delivery physically *is*, shown waiting at the pickup and (later) carried.
	 *
	 * Per-job rather than per-point on purpose: recognising the order is the point — the
	 * arsonist's single bottle of water, the dog's translation collar, a ShellStop taco tray.
	 * Unset falls back to the point's own default parcel.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	TSoftObjectPtr<UStaticMesh> ParcelMesh;

	bool IsValidJob() const { return Pickup && Dropoff && Pickup != Dropoff; }

	/** Accepted or carrying — i.e. occupying one of the player's job slots. */
	bool IsHeld() const { return Stage == EDGJobStage::Accepted || Stage == EDGJobStage::Carrying; }

	/** VIP work is untimed by convention; TimeLimitSeconds of 0 means "no clock". */
	bool IsTimed() const { return TimeLimitSeconds > 0.f; }
};
