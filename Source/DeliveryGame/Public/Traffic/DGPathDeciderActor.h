// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DGPathDeciderActor.generated.h"

class ADGAIVehiclePawn;
class ADGPathActor;
class UBoxComponent;

/** How a decider picks among its target routes. */
UENUM(BlueprintType)
enum class EDGPathChoiceMode : uint8
{
	/** Uniform random. Can bunch several vehicles onto the same route in a row. */
	Random,

	/** Cycle through the routes in order, spreading traffic evenly. */
	RoundRobin,
};

/**
 * Volume that reroutes AI vehicles onto a new path as they drive through it.
 * Native replacement for BP_Path_Decider.
 */
UCLASS(Blueprintable, ClassGroup = (Traffic), meta = (DisplayName = "Path Decider"))
class DELIVERYGAME_API ADGPathDeciderActor : public AActor
{
	GENERATED_BODY()

public:
	ADGPathDeciderActor();

	/**
	 * Trigger volume, **resolved from the box component this actor owns rather than created here.**
	 *
	 * BP_Path_Decider carries its own "Decision Box", and creating a native one would displace the
	 * Blueprint's root, break its component binding, and leave every placed decider with the wrong
	 * volume. Same reasoning as ADGPathActor::RouteSpline.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Decider")
	TObjectPtr<UBoxComponent> DecisionBox;

	/**
	 * Routes a vehicle may be sent onto.
	 *
	 * **Leave empty to discover candidates dynamically** from whichever ADGPathActors overlap
	 * DecisionBox — which is how BP_Path_Decider worked (`GetOverlappingActors(DecisionBox,
	 * BP_Path_C)`), so existing deciders keep working with no authoring. Fill this in only to
	 * constrain the choice to a specific set.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Decider")
	TArray<TObjectPtr<ADGPathActor>> TargetPaths;

	/** Random by default: intersection choices should look arbitrary rather than cycle predictably. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decider")
	EDGPathChoiceMode ChoiceMode = EDGPathChoiceMode::Random;

	/**
	 * Re-derive the vehicle's progress from its position on the new route instead of entering at
	 * the route's start. Correct when routes physically overlap the volume; leave off when the
	 * target routes begin elsewhere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decider")
	bool bSnapToClosestPoint = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decider|Debug")
	bool bDrawDebug = false;

	/** Next route this decider would hand out, without consuming a round-robin step. */
	UFUNCTION(BlueprintPure, Category = "Decider")
	ADGPathActor* PeekNextPath() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void OnDecisionBoxBeginOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Route selection. Override to add conditions such as time of day or traffic density. */
	UFUNCTION(BlueprintNativeEvent, Category = "Decider")
	ADGPathActor* ChoosePathFor(ADGAIVehiclePawn* Vehicle);
	virtual ADGPathActor* ChoosePathFor_Implementation(ADGAIVehiclePawn* Vehicle);

private:
	/** Valid entries of TargetPaths, so a null entry can never be handed to a vehicle. */
	TArray<ADGPathActor*> GatherValidTargets() const;

	int32 RoundRobinIndex = 0;
};
