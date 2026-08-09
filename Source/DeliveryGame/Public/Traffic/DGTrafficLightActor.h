// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DGTrafficLightActor.generated.h"

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
 * A traffic signal. Native replacement for BP_Prop_Traffic_Light_Sm's Begin Play delay chain.
 *
 * Cycles Green -> Yellow -> Red -> Green and **publishes state only** — it holds nothing itself.
 * Each ADGAIVehiclePawn polls the registered signals every tick (UpdateSignalHold) and stops when
 * one shows a stop aspect *and* IsActorInZone says the vehicle is inside its volume. Vehicles are
 * held on yellow as well as red, matching the original, which armed its zone for both.
 *
 * **Creates no components** — it resolves the ones the Blueprint already owns (the box volume plus
 * the red / yellow / green light meshes), like ADGPathActor and ADGPathDeciderActor.
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

	/** Set the aspect. Vehicles notice on their next poll; nothing is pushed. */
	UFUNCTION(BlueprintCallable, Category = "Signal")
	void SetSignalState(EDGSignalState NewState);

	/** Step to the next aspect in the Green -> Yellow -> Red cycle. */
	UFUNCTION(BlueprintCallable, Category = "Signal")
	void AdvanceSignalState();

	/** True when vehicles should be held: any aspect other than green. */
	UFUNCTION(BlueprintPure, Category = "Signal")
	bool IsStopAspect() const { return SignalState != EDGSignalState::Green; }

	/**
	 * True if this signal's volume currently contains Actor.
	 *
	 * Vehicles call this each update to decide for themselves whether to stop. The signal no longer
	 * pushes a hold onto them: a push leaves the flag set if the release is ever missed — an exit
	 * event lost, two lights sharing one boolean, a light destroyed mid-hold — which stranded a van
	 * 50 m from the nearest junction.
	 */
	UFUNCTION(BlueprintPure, Category = "Signal")
	bool IsActorInZone(const AActor* Actor) const;

	/**
	 * Distance along Forward from From to this zone's near face, or -1 when the line never enters the
	 * zone. Vehicles use it to brake smoothly toward the stop line of a red or yellow ahead — and
	 * because it is a line-through-box test, a pad in the opposite lane or on the cross street misses
	 * and cannot brake traffic it does not govern.
	 */
	UFUNCTION(BlueprintPure, Category = "Signal")
	float GetStopLineDistance(const FVector& From, const FVector& Forward) const;

	/** Seconds the current aspect lasts. */
	UFUNCTION(BlueprintPure, Category = "Signal")
	float GetCurrentStateDuration() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ResolveComponents();

	/** Show only the lamp for the current aspect. */
	void UpdateLampVisibility();

	float TimeInState = 0.f;

	/** Counts down InitDelay before the first automatic transition. */
	float TimeUntilStart = 0.f;
};
