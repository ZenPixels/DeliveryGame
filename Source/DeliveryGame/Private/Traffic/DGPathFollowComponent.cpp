// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGPathFollowComponent.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SplineComponent.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"
#include "Traffic/DGPathActor.h"
#include "Traffic/DGTrafficSubsystem.h"

UDGPathFollowComponent::UDGPathFollowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDGPathFollowComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!SplinePath && bAutoFindSpline)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UDGTrafficSubsystem* Traffic = World->GetSubsystem<UDGTrafficSubsystem>())
			{
				float FoundDistance = 0.f;
				if (ADGPathActor* Nearest = Traffic->FindNearestPath(GetOwner()->GetActorLocation(), FoundDistance))
				{
					SplinePath = Nearest;
					DistanceAlongSpline = FoundDistance;
				}
			}
		}
	}

	if (!SplinePath)
	{
		UE_LOG(LogDeliveryGame, Warning, TEXT("%s has no path to follow; it will stay put."),
			*GetNameSafe(GetOwner()));
		return;
	}

	UpdateDestination();

	if (bStartMovingOnBeginPlay)
	{
		StartMoving();
	}
}

UChaosWheeledVehicleMovementComponent* UDGPathFollowComponent::GetMovement() const
{
	if (CachedMovement.IsValid())
	{
		return CachedMovement.Get();
	}

	if (const AActor* Owner = GetOwner())
	{
		UChaosWheeledVehicleMovementComponent* Movement =
			Owner->FindComponentByClass<UChaosWheeledVehicleMovementComponent>();
		CachedMovement = Movement;
		return Movement;
	}

	return nullptr;
}

void UDGPathFollowComponent::StartMoving()
{
	bIsMoving = true;
}

void UDGPathFollowComponent::StopMoving()
{
	bIsMoving = false;

	// Release the throttle immediately rather than waiting for the next tick.
	if (UChaosWheeledVehicleMovementComponent* Movement = GetMovement())
	{
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(StoppingBrakeForce);
	}

	CurrentThrottle = 0.f;
}

void UDGPathFollowComponent::SetPath(ADGPathActor* NewPath, bool bSnapToClosestPoint)
{
	SplinePath = NewPath;

	if (!SplinePath)
	{
		StopMoving();
		return;
	}

	if (bSnapToClosestPoint)
	{
		UpdateDestination();
	}
	else
	{
		DistanceAlongSpline = 0.f;
		PercentageAlongSpline = 0.f;
	}
}

void UDGPathFollowComponent::UpdateDestination()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !SplinePath || !SplinePath->SplinePath)
	{
		return;
	}

	const USplineComponent* Spline = SplinePath->SplinePath;
	const float SplineLength = Spline->GetSplineLength();
	if (SplineLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Re-derive progress from where the vehicle actually is, so a shunt off the route corrects
	// itself instead of accumulating error the way an integrated distance would.
	FVector ClosestPoint;
	SplinePath->GetClosestPoint(Owner->GetActorLocation(), ClosestPoint, DistanceAlongSpline);
	PercentageAlongSpline = DistanceAlongSpline / SplineLength;

	float AimDistance = DistanceAlongSpline + ForwardAimDistance;
	if (Spline->IsClosedLoop())
	{
		AimDistance = FMath::Fmod(AimDistance, SplineLength);
	}
	else
	{
		AimDistance = FMath::Min(AimDistance, SplineLength);
	}

	Destination = Spline->GetLocationAtDistanceAlongSpline(AimDistance, ESplineCoordinateSpace::World);

	// Hand off to the next route once the aim point has nowhere left to advance.
	if (!Spline->IsClosedLoop() && (SplineLength - DistanceAlongSpline) <= PathEndTolerance)
	{
		AdvanceToNextPath();
	}
}

void UDGPathFollowComponent::AdvanceToNextPath()
{
	ADGPathActor* NextPath = SplinePath ? SplinePath->ChooseNextPath() : nullptr;

	if (!NextPath)
	{
		UE_LOG(LogDeliveryGame, Verbose, TEXT("%s reached the end of %s with no continuation; stopping."),
			*GetNameSafe(GetOwner()), *GetNameSafe(SplinePath));
		StopMoving();
		return;
	}

	// Enter the new route at its start rather than snapping to the closest point, which could
	// otherwise place the vehicle partway along and skip a section of road.
	SetPath(NextPath, /*bSnapToClosestPoint=*/false);
}

void UDGPathFollowComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (SplinePath)
	{
		TimeSinceLastUpdate += DeltaTime;
		if (TimeSinceLastUpdate >= DestinationUpdateInterval)
		{
			TimeSinceLastUpdate = 0.f;
			UpdateDestination();
		}
	}

	ProceedToDestination(DeltaTime);

	if (bDrawDebug)
	{
		DrawDebugVisuals();
	}
}

void UDGPathFollowComponent::ProceedToDestination(float DeltaTime)
{
	UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	const AActor* Owner = GetOwner();
	if (!Movement || !Owner)
	{
		return;
	}

	if (!bIsMoving || !SplinePath)
	{
		CurrentSteering = FMath::FInterpTo(CurrentSteering, 0.f, DeltaTime, SteeringInterpSpeed);
		CurrentThrottle = 0.f;
		Movement->SetSteeringInput(CurrentSteering);
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(StoppingBrakeForce);
		return;
	}

	// ---- Steering: yaw error between facing and the look-ahead aim point ----
	const FVector OwnerLocation = Owner->GetActorLocation();
	const FRotator LookAt = UKismetMathLibrary::FindLookAtRotation(OwnerLocation, Destination);
	const FRotator Delta = UKismetMathLibrary::NormalizedDeltaRotator(LookAt, Owner->GetActorRotation());

	const float TargetSteering = FMath::Clamp(Delta.Yaw / SteeringSaturationAngle, -1.f, 1.f);
	CurrentSteering = (SteeringInterpSpeed > 0.f)
		? FMath::FInterpTo(CurrentSteering, TargetSteering, DeltaTime, SteeringInterpSpeed)
		: TargetSteering;

	// ---- Throttle ----
	if (bBlockedAhead)
	{
		CurrentThrottle = 0.f;
		Movement->SetSteeringInput(CurrentSteering);
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(StoppingBrakeForce);
		return;
	}

	const float PathThrottleCap = (SplinePath->ThrottleOverride > 0.f) ? SplinePath->ThrottleOverride : MaxThrottle;

	// Ease off in corners: full lock scales throttle down to CorneringThrottleScale.
	const float CorneringFactor =
		FMath::Lerp(1.f, CorneringThrottleScale, FMath::Abs(CurrentSteering));

	float Throttle = PathThrottleCap * CorneringFactor;

	// Taper toward zero over the last 20% of the approach to cruise speed, so the vehicle
	// settles at CruiseSpeedMPH instead of oscillating around it.
	if (CruiseSpeedMPH > 0.f)
	{
		const float SpeedMPH = Movement->GetForwardSpeedMPH();
		const float SpeedRatio = SpeedMPH / CruiseSpeedMPH;
		const float SpeedScale = 1.f - FMath::GetMappedRangeValueClamped(
			FVector2f(0.8f, 1.0f), FVector2f(0.f, 1.f), SpeedRatio);
		Throttle *= SpeedScale;
	}

	CurrentThrottle = FMath::Clamp(Throttle, 0.f, 1.f);

	Movement->SetSteeringInput(CurrentSteering);
	Movement->SetThrottleInput(CurrentThrottle);
	Movement->SetBrakeInput(0.f);
}

FString UDGPathFollowComponent::GetDebugStatus() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	const float SpeedMPH = Movement ? Movement->GetForwardSpeedMPH() : 0.f;

	return FString::Printf(
		TEXT("%s\nPath: %s\nProgress: %.0f cm (%.0f%%)\nSpeed: %.1f mph\nThrottle: %.2f  Steer: %+.2f\n%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(SplinePath),
		DistanceAlongSpline,
		PercentageAlongSpline * 100.f,
		SpeedMPH,
		CurrentThrottle,
		CurrentSteering,
		bBlockedAhead ? TEXT("BLOCKED") : (bIsMoving ? TEXT("Moving") : TEXT("Stopped")));
}

void UDGPathFollowComponent::DrawDebugVisuals() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FVector From = Owner->GetActorLocation();
	const FColor LineColor = bBlockedAhead ? FColor::Red : (bIsMoving ? FColor::Cyan : FColor::Orange);

	DrawDebugLine(World, From, Destination, LineColor, false, -1.f, 0, 5.f);
	DrawDebugSphere(World, Destination, 60.f, 12, LineColor, false, -1.f, 0, 2.f);
	DrawDebugString(World, From + FVector(0.f, 0.f, 220.f), GetDebugStatus(), nullptr, FColor::White, 0.f, true, 1.2f);
}
