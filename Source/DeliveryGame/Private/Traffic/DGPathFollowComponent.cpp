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

	if (!TargetSpline && bAutoFindSpline)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UDGTrafficSubsystem* Traffic = World->GetSubsystem<UDGTrafficSubsystem>())
			{
				// Just take the nearest route. Direction is derived from the vehicle's heading in
				// SetPath, so a road running "backwards" relative to its spline is fine.
				float FoundDistance = 0.f;
				TargetSpline = Traffic->FindNearestPath(GetOwner()->GetActorLocation(), FoundDistance);
			}
		}
	}

	if (!TargetSpline)
	{
		UE_LOG(LogDeliveryGame, Warning, TEXT("%s has no path to follow; it will stay put."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// Decide travel direction from how the vehicle was placed. This covers the case where TargetSpline
	// was assigned in the editor rather than through SetPath, which would otherwise leave the default
	// of +1 and send a vehicle parked facing "backwards" into a U-turn.
	TravelDirection = IsPathAligned(TargetSpline) ? 1 : -1;

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

	// Release the handbrake StopMoving applied, or the vehicle sits there revving.
	if (UChaosWheeledVehicleMovementComponent* Movement = GetMovement())
	{
		Movement->SetHandbrakeInput(false);
	}
}

void UDGPathFollowComponent::StopMoving()
{
	bIsMoving = false;

	// Act immediately rather than waiting for the next tick. The handbrake is carried over from the
	// Blueprint's Stop Moving — without it a stopped vehicle rolls away on any slope.
	if (UChaosWheeledVehicleMovementComponent* Movement = GetMovement())
	{
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(StoppingBrakeForce);
		Movement->SetHandbrakeInput(true);
	}

	CurrentThrottle = 0.f;
}

void UDGPathFollowComponent::SetSignalHold(bool bHold)
{
	bHeldBySignal = bHold;
}

void UDGPathFollowComponent::SetTrafficAhead(float DistanceCm, float ClosingSpeed)
{
	TrafficClearance = (DistanceCm < 0.f) ? 0.f : DistanceCm;
	TrafficClosingSpeed = ClosingSpeed;
}

float UDGPathFollowComponent::GetFollowBrake() const
{
	// Nothing tracked ahead.
	if (TrafficClearance >= 1000000.f)
	{
		return 0.f;
	}

	// Already inside the minimum gap: stop regardless of closing speed.
	const float UsableGap = TrafficClearance - MinFollowDistance;
	if (UsableGap <= 0.f)
	{
		return 1.f;
	}

	// Matching speed or falling behind needs no braking, however close the vehicle ahead is. This is
	// what lets a queue sit nose-to-tail without everyone standing on the brakes.
	if (TrafficClosingSpeed <= 0.f)
	{
		return 0.f;
	}

	// Deceleration needed to shed the closing speed within the gap remaining.
	const float RequiredDeceleration = (TrafficClosingSpeed * TrafficClosingSpeed) / (2.f * UsableGap);
	return FMath::Clamp(RequiredDeceleration / ComfortableDeceleration, 0.f, 1.f);
}

float UDGPathFollowComponent::GetEffectiveAimDistance() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	const float Speed = Movement ? FMath::Abs(Movement->GetForwardSpeed()) : 0.f;

	// Short when slow so corners are taken tightly, long at speed so straights stay smooth.
	return FMath::Clamp(MinAimDistance + Speed * AimTimeAhead, MinAimDistance, ForwardAimDistance);
}

float UDGPathFollowComponent::GetCornerSpeedScale() const
{
	const USplineComponent* Spline = TargetSpline ? TargetSpline->GetRouteSpline() : nullptr;
	if (!Spline)
	{
		return 1.f;
	}

	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}

	// Compare heading here with heading a corner-lookahead further on. The bigger the change, the
	// tighter what is coming and the slower the vehicle needs to already be travelling.
	const float AheadDistance = DistanceAlongSpline + TravelDirection * CornerLookaheadDistance;
	const float Clamped = Spline->IsClosedLoop()
		? FMath::Fmod(AheadDistance + Length, Length)
		: FMath::Clamp(AheadDistance, 0.f, Length);

	const FVector HereDir = Spline->GetDirectionAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::World).GetSafeNormal2D();
	const FVector AheadDir = Spline->GetDirectionAtDistanceAlongSpline(Clamped, ESplineCoordinateSpace::World).GetSafeNormal2D();

	const float Dot = FMath::Clamp(FVector::DotProduct(HereDir, AheadDir), -1.f, 1.f);
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));

	const float Tightness = FMath::Clamp(AngleDegrees / CornerFullSlowAngle, 0.f, 1.f);
	return FMath::Lerp(1.f, MinCornerSpeedScale, Tightness);
}

float UDGPathFollowComponent::GetTargetSpeedMPH() const
{
	// A road's own limit wins over the vehicle's default.
	const float BaseLimit = (TargetSpline && TargetSpline->SpeedLimitMPH > 0.f)
		? TargetSpline->SpeedLimitMPH
		: CruiseSpeedMPH;

	if (BaseLimit <= 0.f)
	{
		return 0.f;
	}

	// Compliance scales the limit; corner slowdown still applies on top, so a speeder is fast on the
	// straight but does not carry that speed into a bend it physically cannot take.
	return BaseLimit * SpeedLimitCompliance * GetCornerSpeedScale();
}

float UDGPathFollowComponent::GetDesiredFollowGap() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	const float Speed = Movement ? FMath::Max(0.f, Movement->GetForwardSpeed()) : 0.f;

	// Distance covered during the headway, plus the standstill gap, never below the fixed floor.
	return FMath::Max(SafeFollowDistance, MinFollowDistance + Speed * FollowHeadwaySeconds);
}

float UDGPathFollowComponent::GetFollowThrottleScale() const
{
	const float DesiredGap = GetDesiredFollowGap();

	if (TrafficClearance >= DesiredGap)
	{
		return 1.f;
	}

	if (TrafficClearance <= MinFollowDistance)
	{
		return 0.f;
	}

	// Linear ramp: eases off approaching a slower vehicle instead of stopping dead.
	const float Span = DesiredGap - MinFollowDistance;
	return (Span > KINDA_SMALL_NUMBER) ? ((TrafficClearance - MinFollowDistance) / Span) : 0.f;
}

bool UDGPathFollowComponent::IsHeld() const
{
	return bHeldBySignal || bBlockedAhead || GetFollowThrottleScale() <= 0.f;
}

void UDGPathFollowComponent::SetPath(ADGPathActor* NewPath, bool bSnapToClosestPoint)
{
	TargetSpline = NewPath;

	if (!TargetSpline)
	{
		StopMoving();
		return;
	}

	// Lock in which way along this spline the vehicle will travel, before any aiming happens.
	TravelDirection = IsPathAligned(TargetSpline) ? 1 : -1;

	if (bSnapToClosestPoint)
	{
		UpdateDestination();
	}
	else
	{
		// Entering at whichever end we are actually driving towards.
		const float Length = TargetSpline->GetSplineLength();
		DistanceAlongSpline = (TravelDirection > 0) ? 0.f : Length;
		PercentageAlongSpline = (TravelDirection > 0) ? 0.f : 1.f;
	}
}

void UDGPathFollowComponent::UpdateDestination()
{
	const AActor* Owner = GetOwner();
	const USplineComponent* Spline = TargetSpline ? TargetSpline->GetRouteSpline() : nullptr;
	if (!Owner || !Spline)
	{
		return;
	}

	if (Spline->GetSplineLength() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Re-derive progress from where the vehicle actually is, so a shunt off the route corrects
	// itself instead of accumulating error the way an integrated distance would.
	FVector ClosestPoint;
	TargetSpline->GetClosestPoint(Owner->GetActorLocation(), ClosestPoint, DistanceAlongSpline);

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector OwnerForward = Owner->GetActorForwardVector().GetSafeNormal2D();

	// Lost: a vehicle that has drifted this far is not on its road any more. Re-acquire rather than
	// carry on toward a stale aim point with no steering error and the throttle open.
	if (MaxDistanceFromPath > 0.f && FVector::Dist2D(ClosestPoint, OwnerLocation) > MaxDistanceFromPath)
	{
		UE_LOG(LogDeliveryGame, Verbose, TEXT("%s strayed %.0f cm from %s; re-acquiring."),
			*GetNameSafe(Owner), FVector::Dist2D(ClosestPoint, OwnerLocation), *GetNameSafe(TargetSpline));

		if (!ReacquireNearestPath())
		{
			StopMoving();
			return;
		}

		Spline = TargetSpline->GetRouteSpline();
		if (!Spline)
		{
			StopMoving();
			return;
		}

		TargetSpline->GetClosestPoint(OwnerLocation, ClosestPoint, DistanceAlongSpline);
	}

	const float Length = Spline->GetSplineLength();
	const bool bClosed = Spline->IsClosedLoop();

	// Score each look-ahead option by how far in front of the vehicle it sits, and keep the current
	// direction unless the other is clearly better. This is what stops a U-turn: if the latched
	// direction points behind the vehicle it simply loses and gets swapped.
	const float AimAhead = GetEffectiveAimDistance();

	auto AimDistanceFor = [&](int32 Direction)
	{
		const float Raw = DistanceAlongSpline + Direction * AimAhead;
		return bClosed ? FMath::Fmod(Raw + Length, Length) : FMath::Clamp(Raw, 0.f, Length);
	};

	auto AheadScore = [&](int32 Direction)
	{
		const FVector Point = Spline->GetLocationAtDistanceAlongSpline(AimDistanceFor(Direction), ESplineCoordinateSpace::World);
		return FVector::DotProduct((Point - OwnerLocation).GetSafeNormal2D(), OwnerForward);
	};

	const int32 SafeDirection = (TravelDirection >= 0) ? 1 : -1;
	TravelDirection = SafeDirection;

	// Only reconsider direction at low speed. Mid-swerve the opposite direction can briefly score
	// better, and flipping there silently redefines the oncoming lane as correct.
	{
		const UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
		const float Speed = Movement ? FMath::Abs(Movement->GetForwardSpeed()) : 0.f;

		if (Speed <= DirectionFlipMaxSpeed &&
			AheadScore(-SafeDirection) > AheadScore(SafeDirection) + DirectionFlipHysteresis)
		{
			TravelDirection = -SafeDirection;
		}
	}

	PercentageAlongSpline = DistanceAlongSpline / Length;

	const float AimDistance = AimDistanceFor(TravelDirection);
	Destination = Spline->GetLocationAtDistanceAlongSpline(AimDistance, ESplineCoordinateSpace::World);

	// ---- Lane keeping ----
	// Offset along the *route's* right rather than the vehicle's. Vehicle-relative offsetting dilutes
	// itself: as the vehicle yaws toward the aim point its right vector swings with it, so part of the
	// offset turns into forward distance and the vehicle settles short of the lane. Route-relative
	// keeps the shift purely lateral. Safe now that TravelDirection is stable.
	{
		const FVector SplineDirAtVehicle =
			Spline->GetDirectionAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::World);
		const FVector TravelDir = (SplineDirAtVehicle * TravelDirection).GetSafeNormal2D();
		const FVector RouteRight = FVector::CrossProduct(FVector::UpVector, TravelDir).GetSafeNormal();

		CurrentLateralOffset = FVector::DotProduct(OwnerLocation - ClosestPoint, RouteRight);

		// Rate at which the lane gap is already closing, for the damping term below.
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		float LateralRate = 0.f;
		if (LastLateralSampleTime >= 0.f)
		{
			const float Elapsed = Now - LastLateralSampleTime;
			if (Elapsed > KINDA_SMALL_NUMBER)
			{
				LateralRate = (CurrentLateralOffset - PreviousLateralOffset) / Elapsed;
			}
		}
		PreviousLateralOffset = CurrentLateralOffset;
		LastLateralSampleTime = Now;

		// Proportional-derivative: pull toward the lane by the error outstanding, but ease off by how
		// fast the gap is already closing. Proportional alone overshoots into the oncoming lane.
		const float LaneError = LateralOffset - CurrentLateralOffset;
		const float Correction = FMath::Clamp(
			LaneError * LaneCorrectionGain - LateralRate * LaneDampingGain,
			-MaxLaneCorrection, MaxLaneCorrection);

		Destination += RouteRight * (LateralOffset + Correction);
	}

	// Hand off once the end we are driving towards is reached — distance 0 when reversed.
	if (!bClosed)
	{
		const float RemainingDistance = (TravelDirection > 0) ? (Length - DistanceAlongSpline) : DistanceAlongSpline;
		if (RemainingDistance <= PathEndTolerance)
		{
			AdvanceToNextPath();
		}
	}
}

bool UDGPathFollowComponent::IsPathAligned(const ADGPathActor* Path) const
{
	const AActor* Owner = GetOwner();
	if (!Path || !Owner)
	{
		return false;
	}

	return Path->GetAlignmentWith(Owner->GetActorLocation(), Owner->GetActorForwardVector()) >= 0.f;
}

bool UDGPathFollowComponent::IsPathUsable(const ADGPathActor* Path) const
{
	if (!Path)
	{
		return false;
	}

	return bAllowReverseTravel || IsPathAligned(Path);
}

ADGPathActor* UDGPathFollowComponent::FindContinuationPath() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner || !TargetSpline)
	{
		return nullptr;
	}

	const UDGTrafficSubsystem* Traffic = World->GetSubsystem<UDGTrafficSubsystem>();
	if (!Traffic)
	{
		return nullptr;
	}

	// The end we actually arrive at is the spline's start when travelling in reverse.
	const USplineComponent* Spline = TargetSpline->GetRouteSpline();
	if (!Spline)
	{
		return nullptr;
	}

	const float ArrivalDistance = (TravelDirection > 0) ? Spline->GetSplineLength() : 0.f;
	const FVector EndLocation = Spline->GetLocationAtDistanceAlongSpline(ArrivalDistance, ESplineCoordinateSpace::World);

	const double RadiusSq = static_cast<double>(ContinuationSearchRadius) * ContinuationSearchRadius;

	ADGPathActor* Best = nullptr;
	double BestDistSq = RadiusSq;

	for (ADGPathActor* Candidate : Traffic->GetRegisteredPaths())
	{
		if (!Candidate || Candidate == TargetSpline || !IsPathUsable(Candidate))
		{
			continue;
		}

		const double DistSq = Candidate->GetDistanceSquaredTo(EndLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}

	return Best;
}

bool UDGPathFollowComponent::ReacquireNearestPath()
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return false;
	}

	UDGTrafficSubsystem* Traffic = World->GetSubsystem<UDGTrafficSubsystem>();
	if (!Traffic)
	{
		return false;
	}

	float FoundDistance = 0.f;
	ADGPathActor* Nearest = Traffic->FindNearestPath(Owner->GetActorLocation(), FoundDistance);
	if (!Nearest)
	{
		return false;
	}

	TargetSpline = Nearest;
	DistanceAlongSpline = FoundDistance;

	// Direction is re-scored from the vehicle's heading on the next aim update, so nothing is latched
	// here beyond the route itself.
	return true;
}

bool UDGPathFollowComponent::SnapToLane()
{
	AActor* Owner = GetOwner();
	if (!Owner || !TargetSpline)
	{
		return false;
	}

	const USplineComponent* Spline = TargetSpline->GetRouteSpline();
	if (!Spline)
	{
		return false;
	}

	const float Length = Spline->GetSplineLength();
	const float Distance = FMath::Clamp(DistanceAlongSpline, 0.f, Length);

	const FVector OnSpline = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	const FVector SplineDir = Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	const FVector TravelDir = (SplineDir * TravelDirection).GetSafeNormal2D();
	const FVector RouteRight = FVector::CrossProduct(FVector::UpVector, TravelDir).GetSafeNormal();

	// Lane position, lifted clear of the road so the vehicle settles rather than spawning inside it.
	const FVector Target = OnSpline + RouteRight * LateralOffset + FVector(0.f, 0.f, 60.f);

	if (UChaosWheeledVehicleMovementComponent* Movement = GetMovement())
	{
		Movement->StopMovementImmediately();
	}

	const bool bMoved = Owner->SetActorLocationAndRotation(
		Target, TravelDir.Rotation(), /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	UE_LOG(LogDeliveryGame, Warning, TEXT("%s was stuck for %.1fs off-route; placed back on its lane."),
		*GetNameSafe(Owner), TimeStuck);

	return bMoved;
}

void UDGPathFollowComponent::UpdateStuckRecovery(float DeltaTime)
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	const float Speed = Movement ? FMath::Abs(Movement->GetForwardSpeed()) : 0.f;

	// Only count as stuck when the vehicle is *trying* to move. Waiting at a red light or behind
	// stopped traffic is not stuck, and recovering out of a queue would look absurd.
	const bool bWantsToMove = bIsMoving && !bHeldBySignal && !bBlockedAhead && GetFollowBrake() < 0.5f;

	if (!bWantsToMove || Speed > StuckSpeedThreshold)
	{
		TimeStuck = 0.f;
		return;
	}

	TimeStuck += DeltaTime;
	if (TimeStuck < StuckTimeout)
	{
		return;
	}

	TimeStuck = 0.f;

	// Steering back is already being attempted every update; if we are here it has not worked.
	if (bRecoverByTeleport)
	{
		if (!TargetSpline)
		{
			ReacquireNearestPath();
		}
		SnapToLane();
	}
}

void UDGPathFollowComponent::AdvanceToNextPath()
{
	ADGPathActor* NextPath = TargetSpline ? TargetSpline->ChooseNextPath() : nullptr;

	// NextPaths is empty on every existing path actor — the Blueprint relied on deciders for
	// continuations — so fall back to finding a connecting route rather than parking permanently.
	if (!NextPath)
	{
		NextPath = FindContinuationPath();
	}

	if (!NextPath)
	{
		UE_LOG(LogDeliveryGame, Verbose, TEXT("%s reached the end of %s with no continuation; stopping."),
			*GetNameSafe(GetOwner()), *GetNameSafe(TargetSpline));
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

	// Break mutual holds: two vehicles can each sit in the other's traffic volume and wait forever.
	// Deliberately ignores bHeldBySignal — releasing that would mean running red lights.
	if (bBlockedAhead && !bHeldBySignal)
	{
		TimeBlocked += DeltaTime;
		if (BlockedTimeout > 0.f && TimeBlocked >= BlockedTimeout)
		{
			UE_LOG(LogDeliveryGame, Verbose,
				TEXT("%s held for %.1fs; releasing to break a deadlock."),
				*GetNameSafe(GetOwner()), TimeBlocked);
			bBlockedAhead = false;
			TimeBlocked = 0.f;
		}
	}
	else
	{
		TimeBlocked = 0.f;
	}

	// Recover from a dead end. StopMoving is otherwise terminal, and one stranded vehicle blocks
	// everything queued behind it.
	if (!bIsMoving && bAutoResume && !bHeldBySignal)
	{
		TimeSinceResumeAttempt += DeltaTime;
		if (TimeSinceResumeAttempt >= ResumeRetryInterval)
		{
			TimeSinceResumeAttempt = 0.f;

			// Prefer a genuine continuation from where we sit; fall back to the nearest route.
			ADGPathActor* Resume = FindContinuationPath();
			if (!Resume && !TargetSpline)
			{
				Resume = nullptr;
			}

			if (Resume)
			{
				SetPath(Resume, /*bSnapToClosestPoint=*/true);
				StartMoving();
			}
			else if (ReacquireNearestPath())
			{
				UpdateDestination();
				StartMoving();
			}

			if (bIsMoving)
			{
				UE_LOG(LogDeliveryGame, Verbose, TEXT("%s resumed onto %s."),
					*GetNameSafe(GetOwner()), *GetNameSafe(TargetSpline));
			}
		}
	}
	else
	{
		TimeSinceResumeAttempt = 0.f;
	}

	if (TargetSpline)
	{
		TimeSinceLastUpdate += DeltaTime;
		if (TimeSinceLastUpdate >= DestinationUpdateInterval)
		{
			TimeSinceLastUpdate = 0.f;
			UpdateDestination();
		}
	}

	ProceedToDestination(DeltaTime);
	UpdateStuckRecovery(DeltaTime);

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

	if (!bIsMoving || !TargetSpline)
	{
		CurrentSteering = FMath::FInterpTo(CurrentSteering, 0.f, DeltaTime, SteeringInterpSpeed);
		CurrentThrottle = 0.f;
		Movement->SetSteeringInput(CurrentSteering);
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(StoppingBrakeForce);
		Movement->SetHandbrakeInput(true);
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
	// A red light or a hard block stops the vehicle outright; steering is still applied so it stays
	// pointed down its lane while waiting.
	if (bHeldBySignal || bBlockedAhead)
	{
		CurrentThrottle = 0.f;
		Movement->SetSteeringInput(CurrentSteering);
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(StoppingBrakeForce);
		Movement->SetHandbrakeInput(bHeldBySignal);
		return;
	}

	const float PathThrottleCap = (TargetSpline->ThrottleOverride > 0.f) ? TargetSpline->ThrottleOverride : MaxThrottle;

	// Ease off in corners: full lock scales throttle down to CorneringThrottleScale.
	const float CorneringFactor =
		FMath::Lerp(1.f, CorneringThrottleScale, FMath::Abs(CurrentSteering));

	float Throttle = PathThrottleCap * CorneringFactor;

	// Govern to the target speed: the road's limit, reduced for the corner ahead. Taper over the last
	// 20% so the vehicle settles rather than oscillating around it.
	const float TargetSpeedMPH = GetTargetSpeedMPH();
	float OverspeedBrake = 0.f;

	if (TargetSpeedMPH > 0.f)
	{
		const float SpeedMPH = Movement->GetForwardSpeedMPH();
		const float SpeedRatio = SpeedMPH / TargetSpeedMPH;

		const float SpeedScale = 1.f - FMath::GetMappedRangeValueClamped(
			FVector2f(0.8f, 1.0f), FVector2f(0.f, 1.f), SpeedRatio);
		Throttle *= SpeedScale;

		// Lifting off is not enough to lose speed before a corner, especially for something as heavy
		// as the bus. Brake once meaningfully over the limit.
		OverspeedBrake = FMath::GetMappedRangeValueClamped(
			FVector2f(1.05f, 1.35f), FVector2f(0.f, 1.f), SpeedRatio);
	}

	// Two independent limits on the vehicle ahead: the gap it wants to keep, and the deceleration
	// needed not to hit it. The stricter one wins.
	const float FollowScale = GetFollowThrottleScale();
	const float FollowBrake = GetFollowBrake();
	Throttle *= FMath::Min(FollowScale, 1.f - FollowBrake);

	CurrentThrottle = FMath::Clamp(Throttle, 0.f, 1.f);

	Movement->SetSteeringInput(CurrentSteering);
	Movement->SetThrottleInput(CurrentThrottle);

	// Whichever demands more braking: the vehicle ahead, or being over the limit for the corner.
	Movement->SetBrakeInput(FMath::Max(FollowBrake, OverspeedBrake) * StoppingBrakeForce);
	Movement->SetHandbrakeInput(false);
}

FString UDGPathFollowComponent::GetDebugStatus() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	const float SpeedMPH = Movement ? Movement->GetForwardSpeedMPH() : 0.f;

	const float FollowScale = GetFollowThrottleScale();

	FString Status;
	if (bHeldBySignal)
	{
		Status = TEXT("RED LIGHT");
	}
	else if (bBlockedAhead)
	{
		Status = TEXT("BLOCKED");
	}
	else if (FollowScale < 1.f || GetFollowBrake() > 0.f)
	{
		Status = FString::Printf(TEXT("Following: %.0f cm, closing %.0f cm/s, brake %.0f%%"),
			TrafficClearance, TrafficClosingSpeed, GetFollowBrake() * 100.f);
	}
	else
	{
		Status = bIsMoving ? TEXT("Moving") : TEXT("Stopped");
	}

	return FString::Printf(
		TEXT("%s\nPath: %s (%s)\nLane: %+.0f cm / target %+.0f\nProgress: %.0f cm (%.0f%%)\n")
		TEXT("Speed: %.1f mph\nThrottle: %.2f  Steer: %+.2f\n%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(TargetSpline),
		(TravelDirection > 0) ? TEXT("forward") : TEXT("reverse"),
		CurrentLateralOffset,
		LateralOffset,
		DistanceAlongSpline,
		PercentageAlongSpline * 100.f,
		SpeedMPH,
		CurrentThrottle,
		CurrentSteering,
		*Status);
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
