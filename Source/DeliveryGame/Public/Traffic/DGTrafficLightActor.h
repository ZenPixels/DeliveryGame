// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DGTrafficLightActor.generated.h"

class ADGAIVehiclePawn;
class UBoxComponent;
class USceneComponent;

/**
 * Signal aspect. Values match the old BP_Prop_Traffic_Light_Sm "Current Light State" encoding
 * (0 red, 1 yellow, 2 green) so the numbers stay recognisable.
 */
UENUM(BlueprintType)
enum class EDGSignalState : uint8
{
	Red = 0,
	Yellow = 1,
	Green = 2,
};

/**
 * Holds AI vehicles at a traffic signal. Native replacement for BP_Prop_Traffic_Light_Sm's
 * Begin Play delay chain.
 *
 * Cycles Green -> Yellow -> Red -> Green. Vehicles overlapping the trigger volume are held whenever
 * the aspect is **not** green, matching the original, which switched the zone's overlap events off on
 * green and on for yellow and red.
 *
 * **Creates no components** — it resolves the ones the Blueprint already owns (the box volume plus the
 * red / yellow / green light meshes), like ADGPathActor and ADGPathDeciderActor. Reparent
 * BP_Prop_Traffic_Light_Sm onto this class and its existing components are picked up.
 *
 * The hold uses UDGPathFollowComponent::SetSignalHold rather than StopMoving, so it is a distinct
 * flag from traffic blocking and cannot be released by the deadlock timeout.
 */
UCLASS(Blueprintable, ClassGroup = (Traffic), meta = (DisplayName = "Traffic Light"))
class DELIVERYGAME_API ADGTrafficLightActor : public AActor
{
	GENERATED_BODY()

public:
	ADGTrafficLightActor();

	// ----------------------------------------------------------- Components
	// All resolved in BeginPlay from the owning Blueprint; never created here.

	/** Trigger volume. Resolved from the actor's Box Component ("Slow Zone" on the Blueprint). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Signal")
	TObjectPtr<UBoxComponent> SignalBox;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Signal|Lights")
	TObjectPtr<USceneComponent> RedLight;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Signal|Lights")
	TObjectPtr<USceneComponent> YellowLight;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Signal|Lights")
	TObjectPtr<USceneComponent> GreenLight;

	// --------------------------------------------------------------- Timing

	/** Aspect to begin on. Stagger this across approaches so a crossroads alternates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	EDGSignalState StartingState = EDGSignalState::Green;

	/** Delay before cycling starts. Offset it per light to phase a junction without extra logic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (ClampMin = "0.0", Units = "s"))
	float InitDelay = 0.f;

	/** Cycle automatically. Off leaves the aspect entirely under Blueprint control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	bool bAutoCycle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (ClampMin = "0.1", Units = "s"))
	float GreenDuration = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (ClampMin = "0.1", Units = "s"))
	float YellowDuration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (ClampMin = "0.1", Units = "s"))
	float RedDuration = 10.f;

	// ---------------------------------------------------------------- State

	UPROPERTY(BlueprintReadOnly, Category = "Signal")
	EDGSignalState SignalState = EDGSignalState::Green;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal|Debug")
	bool bDrawDebug = false;

	/** Set the aspect, releasing every held vehicle when it turns green. */
	UFUNCTION(BlueprintCallable, Category = "Signal")
	void SetSignalState(EDGSignalState NewState);

	/** Step to the next aspect in the Green -> Yellow -> Red cycle. */
	UFUNCTION(BlueprintCallable, Category = "Signal")
	void AdvanceSignalState();

	/** True when vehicles should be held: any aspect other than green. */
	UFUNCTION(BlueprintPure, Category = "Signal")
	bool IsStopAspect() const { return SignalState != EDGSignalState::Green; }

	UFUNCTION(BlueprintPure, Category = "Signal")
	int32 GetNumHeldVehicles() const { return HeldVehicles.Num(); }

	/** Seconds the current aspect lasts. */
	UFUNCTION(BlueprintPure, Category = "Signal")
	float GetCurrentStateDuration() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void OnSignalBoxBeginOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSignalBoxEndOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

private:
	void ResolveComponents();

	/** Show only the lamp for the current aspect. */
	void UpdateLampVisibility();

	/** Hold or release one vehicle, keeping HeldVehicles in step. */
	void ApplyHold(ADGAIVehiclePawn* Vehicle, bool bHold);

	/** Hold every vehicle currently inside the volume. */
	void HoldAllInVolume();

	void ReleaseAll();

	/** Vehicles held by this signal, so each is released exactly once. */
	TSet<TWeakObjectPtr<ADGAIVehiclePawn>> HeldVehicles;

	float TimeInState = 0.f;

	/** Counts down InitDelay before the first automatic transition. */
	float TimeUntilStart = 0.f;
};
