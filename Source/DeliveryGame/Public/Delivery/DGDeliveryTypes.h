// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DGDeliveryTypes.generated.h"

class ADGDeliveryPointActor;

/** Where the active delivery job currently stands. */
UENUM(BlueprintType)
enum class EDGDeliveryState : uint8
{
	/** No job accepted. The phone can offer one. */
	Idle,

	/** Job accepted; drive to the pickup point. No timer pressure yet. */
	AwaitingPickup,

	/** Package aboard; the delivery timer is running. */
	Delivering,
};

/**
 * One delivery job: take a package from Pickup to Dropoff inside TimeLimitSeconds for
 * PayoutDollars. Plain data — the subsystem owns all behaviour.
 */
USTRUCT(BlueprintType)
struct FDGDeliveryJob
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<ADGDeliveryPointActor> Pickup = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<ADGDeliveryPointActor> Dropoff = nullptr;

	/** Seconds allowed once the package is picked up. Derived from distance when the job is built. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	float TimeLimitSeconds = 0.f;

	/** Payment on on-time delivery. Whole dollars — this economy does not need cents. */
	UPROPERTY(BlueprintReadOnly, Category = "Delivery")
	int32 PayoutDollars = 0;

	bool IsValid() const { return Pickup && Dropoff && Pickup != Dropoff; }
};
