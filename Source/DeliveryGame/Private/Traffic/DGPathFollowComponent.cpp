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

float UDGPathFollowComponent::GetVehicleSpeed() const
{
	if (MoveMode == EDGPathFollowMoveMode::Kinematic)
	{
		return KinematicSpeed;
	}

	const UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	return Movement ? FMath::Abs(Movement->GetForwardSpeed()) : 0.f;
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
	if (bHold != bHeldBySignal)
	{
		UE_LOG(LogDeliveryGame, Log, TEXT("%s %s by signal."),
			*GetNameSafe(GetOwner()), bHold ? TEXT("held") : TEXT("released"));
	}

	bHeldBySignal = bHold;
}

void UDGPathFollowComponent::SetTrafficAhead(float DistanceCm, float ClosingSpeed)
{
	TrafficClearance = (DistanceCm < 0.f) ? 0.f : DistanceCm;
	TrafficClosingSpeed = ClosingSpeed;
}

void UDGPathFollowComponent::SetSignalStopAhead(float DistanceCm)
{
	SignalStopDistance = DistanceCm;
}

float UDGPathFollowComponent::GetSignalBrake() const
{
	// Nothing governing ahead.
	if (SignalStopDistance >= 999999.f)
	{
		return 0.f;
	}

	// The stop point is the end of the current spline — the splines are authored to end at the
	// junctions — or the zone's own line if that is somehow nearer. Only measured once a governing
	// light is known, or every vehicle would brake for every ordinary spline end.
	float StopDistance = SignalStopDistance;
	if (const USplineComponent* Spline = TargetSpline ? TargetSpline->GetRouteSpline() : nullptr)
	{
		if (!Spline->IsClosedLoop())
		{
			const float Length = Spline->GetSplineLength();
			const float RemainingToEnd = (TravelDirection > 0) ? (Length - DistanceAlongSpline) : DistanceAlongSpline;
			StopDistance = FMath::Min(StopDistance, RemainingToEnd);
		}
	}

	// At the line: hold there. This is also what parks the vehicle *before* the junction rather than
	// in it — v^2/2d alone falls to zero at standstill and would let the vehicle creep forward again.
	if (StopDistance <= SignalStopMargin)
	{
		return 1.f;
	}

	const float Speed = GetVehicleSpeed();

	const float RequiredDeceleration = (Speed * Speed) / (2.f * (StopDistance - SignalStopMargin));
	return FMath::Clamp(RequiredDeceleration / ComfortableDeceleration, 0.f, 1.f);
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
	const float Speed = GetVehicleSpeed();

	// Short when slow so corners are taken tightly, long at speed so straights stay smooth.
	float Aim = FMath::Clamp(MinAimDistance + Speed * AimTimeAhead, MinAimDistance, ForwardAimDistance);

	// Anti-orbit: a capped yaw rate means a minimum turn radius (v / omega). A goal inside that
	// circle can never be reached — the vehicle laps it forever — so the aim point must always sit
	// outside it, even past the normal cap.
	if (MoveMode == EDGPathFollowMoveMode::Kinematic && KinematicYawRate > 1.f)
	{
		const float TurnRadius = Speed / FMath::DegreesToRadians(KinematicYawRate);
		Aim = FMath::Max(Aim, TurnRadius * 1.2f);
	}

	return Aim;
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

	const float Speed = GetVehicleSpeed();

	// Scan as far ahead as it would take to stop from the current speed. A fixed window is the reason
	// the bus arrived at a corner still doing full cruise: at 1100 cm/s, 900 cm is under a second of
	// warning, and no amount of braking can shed the speed in that distance.
	const float BrakingDistance = (Speed * Speed) / (2.f * FMath::Max(ComfortableDeceleration, 1.f));
	const float ScanDistance = FMath::Clamp(BrakingDistance + CornerLookaheadDistance, CornerLookaheadDistance, 6000.f);

	const FVector HereDir = Spline->GetDirectionAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::World).GetSafeNormal2D();

	// Walk the route in steps, and for each bend work out how fast we may be travelling *now* to
	// still be down to that bend's safe speed by the time we reach it.
	const int32 NumSamples = 8;
	float TightestScale = 1.f;

	for (int32 i = 1; i <= NumSamples; ++i)
	{
		const float Along = (ScanDistance * i) / NumSamples;
		const float Raw = DistanceAlongSpline + TravelDirection * Along;
		const float Sample = Spline->IsClosedLoop()
			? FMath::Fmod(Raw + Length, Length)
			: FMath::Clamp(Raw, 0.f, Length);

		const FVector SampleDir = Spline->GetDirectionAtDistanceAlongSpline(Sample, ESplineCoordinateSpace::World).GetSafeNormal2D();
		const float Dot = FMath::Clamp(FVector::DotProduct(HereDir, SampleDir), -1.f, 1.f);
		const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));

		const float Tightness = FMath::Clamp(AngleDegrees / CornerFullSlowAngle, 0.f, 1.f);
		const float CornerScale = FMath::Lerp(1.f, MinCornerSpeedScale, Tightness);

		if (CornerScale >= TightestScale)
		{
			continue;
		}

		// Distance still available to slow down buys back some speed now: v0 = sqrt(v1^2 + 2*a*d).
		// Far-off bends barely restrict present speed; near ones restrict it hard.
		const float BaseLimit = (TargetSpline && TargetSpline->SpeedLimitMPH > 0.f)
			? TargetSpline->SpeedLimitMPH : CruiseSpeedMPH;
		const float CornerSpeedCms = BaseLimit * CornerScale * 44.704f;
		const float AllowedNowCms = FMath::Sqrt(FMath::Square(CornerSpeedCms) + 2.f * ComfortableDeceleration * Along);
		const float LimitCms = FMath::Max(BaseLimit * 44.704f, 1.f);

		TightestScale = FMath::Min(TightestScale, FMath::Clamp(AllowedNowCms / LimitCms, MinCornerSpeedScale, 1.f));
	}

	// The junction itself is a bend this spline cannot show: the road runs straight to its end and
	// then the planned route may turn hard. Fold the heading change onto the planned next route in
	// as one more bend sitting at the end of this one — this is what slows the bus *before* the
	// turn instead of after the goal has already jumped.
	if (!Spline->IsClosedLoop() && PlannedNextPath)
	{
		if (const USplineComponent* NextSpline = PlannedNextPath->GetRouteSpline())
		{
			const float Remaining = (TravelDirection > 0) ? (Length - DistanceAlongSpline) : DistanceAlongSpline;
			if (Remaining < ScanDistance)
			{
				const float EndDistance = (TravelDirection > 0) ? Length : 0.f;
				const FVector EndDir =
					(Spline->GetDirectionAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::World) * TravelDirection).GetSafeNormal2D();
				const FVector EndLocation = Spline->GetLocationAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::World);

				const float EntryKey = NextSpline->FindInputKeyClosestToWorldLocation(EndLocation);
				const float EntryDistance = NextSpline->GetDistanceAlongSplineAtSplineInputKey(EntryKey);
				FVector EntryDir =
					NextSpline->GetDirectionAtDistanceAlongSpline(EntryDistance, ESplineCoordinateSpace::World).GetSafeNormal2D();

				// Sign the entry direction by whichever way we would actually travel the next route.
				if (FVector::DotProduct(EntryDir, EndDir) < 0.f)
				{
					EntryDir = -EntryDir;
				}

				const float Dot = FMath::Clamp(FVector::DotProduct(EndDir, EntryDir), -1.f, 1.f);
				const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
				const float Tightness = FMath::Clamp(AngleDegrees / CornerFullSlowAngle, 0.f, 1.f);
				const float CornerScale = FMath::Lerp(1.f, MinCornerSpeedScale, Tightness);

				if (CornerScale < TightestScale)
				{
					const float BaseLimit = (TargetSpline && TargetSpline->SpeedLimitMPH > 0.f)
						? TargetSpline->SpeedLimitMPH : CruiseSpeedMPH;
					const float CornerSpeedCms = BaseLimit * CornerScale * 44.704f;
					const float AllowedNowCms =
						FMath::Sqrt(FMath::Square(CornerSpeedCms) + 2.f * ComfortableDeceleration * FMath::Max(Remaining, 1.f));
					const float LimitCms = FMath::Max(BaseLimit * 44.704f, 1.f);

					TightestScale = FMath::Min(TightestScale, FMath::Clamp(AllowedNowCms / LimitCms, MinCornerSpeedScale, 1.f));
				}
			}
		}
	}

	return TightestScale;
}

float UDGPathFollowComponent::GetTimeSinceLastPathChange() const
{
	const UWorld* World = GetWorld();
	return World ? (World->GetTimeSeconds() - LastPathChangeTime) : 1000000.f;
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
	const float Speed = GetVehicleSpeed();

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

	// A new route invalidates any junction plan, and the timestamp lets the decider tell a vehicle
	// that just committed to a route from one that still needs a decision.
	PlannedNextPath = nullptr;
	if (const UWorld* World = GetWorld())
	{
		LastPathChangeTime = World->GetTimeSeconds();
	}

	if (!TargetSpline)
	{
		StopMoving();
		return;
	}

	if (bSnapToClosestPoint)
	{
		// Snapping near a terminus IS a junction entry, whatever the caller thought: the only
		// direction that isn't a U-turn is inward. Deciders route with snap=true, and their boxes
		// fire ~10 m before the pad — deriving direction from heading alignment there was a coin
		// flip against a perpendicular road, and tails was the mid-street U-turn 2-3 car lengths
		// short of the junction (author observation, 2026-08-10). Alignment still decides genuine
		// mid-route joins, where either direction is legal.
		float SnappedDistance = 0.f;
		if (const AActor* Owner = GetOwner())
		{
			FVector Ignored;
			TargetSpline->GetClosestPoint(Owner->GetActorLocation(), Ignored, SnappedDistance);
		}

		const float Length = TargetSpline->GetSplineLength();
		const float NearEnd = FMath::Min(1500.f, Length * 0.25f);
		if (SnappedDistance <= NearEnd)
		{
			TravelDirection = 1;
		}
		else if (SnappedDistance >= Length - NearEnd)
		{
			TravelDirection = -1;
		}
		else
		{
			TravelDirection = IsPathAligned(TargetSpline) ? 1 : -1;
		}
		UpdateDestination();
	}
	else
	{
		// Entering a route at a junction: take whichever terminus is nearer and travel *inward* from
		// it. Heading alignment is useless here — the new road is often perpendicular to the vehicle,
		// so alignment is a coin flip, and "enter at the start" can name the terminus at the far end
		// of the map, putting the goal on the wrong side of the junction. That was the wild veer.
		const USplineComponent* Spline = TargetSpline->GetRouteSpline();
		const AActor* Owner = GetOwner();
		const float Length = TargetSpline->GetSplineLength();

		bool bEnterAtStart = true;
		if (Spline && Owner && Length > KINDA_SMALL_NUMBER)
		{
			const FVector Here = Owner->GetActorLocation();
			const FVector StartPoint = Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
			const FVector EndPoint = Spline->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World);
			bEnterAtStart = FVector::DistSquared(Here, StartPoint) <= FVector::DistSquared(Here, EndPoint);
		}

		DistanceAlongSpline = bEnterAtStart ? 0.f : Length;
		PercentageAlongSpline = bEnterAtStart ? 0.f : 1.f;
		TravelDirection = bEnterAtStart ? 1 : -1;
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
	//
	// Grace period after a route change: a vehicle that just handed off at a junction is legitimately
	// 10-20 m from its NEW lane while it drives the turn — judging it "strayed" there re-acquired the
	// old road, the decider then re-routed it, and the three fought over the goal all the way through
	// the corner (the kerb-jumping thrash in the 2026-08-09 trace, six strays in 35 s).
	if (MaxDistanceFromPath > 0.f && GetTimeSinceLastPathChange() > 3.f &&
		FVector::Dist2D(ClosestPoint, OwnerLocation) > MaxDistanceFromPath)
	{
		UE_LOG(LogDeliveryGame, Log, TEXT("%s strayed %.0f cm from %s; re-acquiring."),
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

	// Direction may only flip when the current one is *manifestly wrong* — the aim point clearly
	// behind the vehicle — not merely when the other side scores better. Every vehicle in a queue at
	// a red light is below the speed threshold, and letting them re-score freely flipped directions
	// mid-queue: the lane offset mirrors with the flip (the bus wandering across the road at the
	// light), and a vehicle whose direction alternates chases a goal that jumps ahead/behind — the
	// "driving in circles" van.
	{
		const float Speed = GetVehicleSpeed();
		const float CurrentScore = AheadScore(SafeDirection);

		if (Speed <= DirectionFlipMaxSpeed &&
			CurrentScore < -0.3f &&
			AheadScore(-SafeDirection) > CurrentScore + DirectionFlipHysteresis)
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

		// Bound the *chase angle*: the goal's sideways offset from the vehicle may never exceed
		// ~35 degrees of the aim distance. At low speed the aim is short (~2 m) while the lane
		// correction can demand metres of sideways — an unbounded goal sits 60+ degrees off the
		// nose, and a kinematic vehicle faithfully turns and drives at it: the queued vans wandering
		// off the road, and the "jump turn" when pulling away from a light. Physics understeer used
		// to hide this. The clamp caps the approach angle without changing where the goal converges.
		const float MaxChase = GetEffectiveAimDistance() * 0.7f;
		const float ChaseFromVehicle = FMath::Clamp(LaneError + Correction, -MaxChase, MaxChase);

		Destination += RouteRight * (CurrentLateralOffset + ChaseFromVehicle);
	}

	if (!bClosed)
	{
		const float RemainingDistance = (TravelDirection > 0) ? (Length - DistanceAlongSpline) : DistanceAlongSpline;

		// Decide the next route well before the junction. Corner anticipation folds the planned
		// turn into its speed target, so the vehicle sheds speed on the approach — deciding at the
		// handoff itself is too late: the road looks straight right up to its end, and the turn
		// only becomes visible once the goal has already jumped.
		if (!PlannedNextPath && RemainingDistance <= JunctionPlanDistance)
		{
			PlannedNextPath = TargetSpline->ChooseNextPath();
			if (!PlannedNextPath)
			{
				PlannedNextPath = FindContinuationPath();
			}

			if (PlannedNextPath)
			{
				UE_LOG(LogDeliveryGame, Log, TEXT("%s planned %s -> %s, %.0f cm before the junction."),
					*GetNameSafe(GetOwner()), *GetNameSafe(TargetSpline), *GetNameSafe(PlannedNextPath),
					RemainingDistance);
			}
		}

		// Hand off once the end we are driving towards is reached — distance 0 when reversed.
		// A stop-aspect signal gates the handoff: the goal stays pinned at this spline's end (the
		// stop point) until green, and only then jumps to the next spline. A vehicle that already
		// jumped is past the light's line and continues through untouched — commitment for free.
		//
		// The one-second settling time prevents the bounce: a route assigned near its own end (a
		// decider snap, a re-acquire) must not instantly hand off again in the same breath — that
		// chain is how a vehicle departs back the way it came without any single choice being a
		// U-turn.
		if (RemainingDistance <= PathEndTolerance && GetSignalBrake() <= 0.f &&
			GetTimeSinceLastPathChange() > 1.f)
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

bool UDGPathFollowComponent::WouldEnterBackwards(const ADGPathActor* Path, const FVector& From, const FVector& Forward) const
{
	const USplineComponent* Spline = Path ? Path->GetRouteSpline() : nullptr;
	if (!Spline)
	{
		return false;
	}

	const float Length = Spline->GetSplineLength();
	const FVector StartPoint = Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
	const FVector EndPoint = Spline->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World);
	const bool bEnterAtStart = FVector::DistSquared(From, StartPoint) <= FVector::DistSquared(From, EndPoint);

	// Direction of travel entering at the nearest terminus, heading into the route.
	const float EntryDistance = bEnterAtStart ? 0.f : Length;
	const FVector InwardDir =
		(Spline->GetDirectionAtDistanceAlongSpline(EntryDistance, ESplineCoordinateSpace::World)
			* (bEnterAtStart ? 1.f : -1.f)).GetSafeNormal2D();

	return FVector::DotProduct(InwardDir, Forward.GetSafeNormal2D()) < -0.5f;
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

	// Random among every road connecting at this junction, not nearest-wins. Since routes are planned
	// ahead of the decider boxes now, this *is* the junction decision for most vehicles — nearest-wins
	// here would send every vehicle round the same deterministic loop forever.
	//
	// U-turns are banned as a route choice: entering a road backwards relative to our travel is only
	// permitted when it is the *only* option (a genuine dead end), where turning back beats parking.
	const FVector ArrivalHeading =
		(Spline->GetDirectionAtDistanceAlongSpline(ArrivalDistance, ESplineCoordinateSpace::World)
			* TravelDirection).GetSafeNormal2D();

	TArray<ADGPathActor*> Candidates;
	TArray<ADGPathActor*> UTurnFallbacks;
	for (ADGPathActor* Candidate : Traffic->GetRegisteredPaths())
	{
		if (!Candidate || Candidate == TargetSpline || !IsPathUsable(Candidate))
		{
			continue;
		}

		if (Candidate->GetDistanceSquaredTo(EndLocation) <= RadiusSq)
		{
			if (WouldEnterBackwards(Candidate, EndLocation, ArrivalHeading))
			{
				UTurnFallbacks.Add(Candidate);
			}
			else
			{
				Candidates.Add(Candidate);
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		Candidates = UTurnFallbacks; // dead end: turning back beats parking forever
	}

	return Candidates.IsEmpty() ? nullptr : Candidates[FMath::RandHelper(Candidates.Num())];
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

	// Re-derive travel direction from the vehicle's current heading. Keeping the OLD road's
	// direction was a trap: if it pointed backwards on the new road, the aim sat behind the vehicle
	// and the low-speed gate on direction flips blocked the correction — a fast re-acquired vehicle
	// was locked chasing a goal behind it, veering kilometres off the map.
	//
	// Near a terminus, though, inward beats alignment — same junction-entry rule as SetPath's
	// snap branch, and the same coin-flip U-turn if skipped (a strayed vehicle usually re-binds
	// right next to the junction it fumbled).
	const float NearestLength = Nearest->GetSplineLength();
	const float NearEnd = FMath::Min(1500.f, NearestLength * 0.25f);
	if (FoundDistance <= NearEnd)
	{
		TravelDirection = 1;
	}
	else if (FoundDistance >= NearestLength - NearEnd)
	{
		TravelDirection = -1;
	}
	else
	{
		TravelDirection = IsPathAligned(Nearest) ? 1 : -1;
	}

	// Same bookkeeping as SetPath. Skipping it left the path-change timestamp stale, so the decider
	// treated a just-re-acquired vehicle as fair game and immediately re-routed it — one corner of
	// the junction thrash triangle.
	PlannedNextPath = nullptr;
	LastPathChangeTime = World->GetTimeSeconds();

	// Direction is re-scored from the vehicle's heading on the next aim update, so nothing is latched
	// here beyond the route itself.
	return true;
}

void UDGPathFollowComponent::UpdateStuckRecovery(float DeltaTime)
{
	const float Speed = GetVehicleSpeed();

	// Stillness with an observable reason is never "stuck": a red light, a queue, or a hold all
	// explain themselves and clear themselves, and the vehicle must respond the frame they do.
	// Parking these was the "stopped at the light and never started through multiple cycles" bug —
	// the park/retry cadence phased against the light cycle and could miss green indefinitely.
	// Stuck detection is only for UNEXPLAINED immobility: throttle on, road clear, not moving.
	const bool bWaitingWithReason =
		bHeldBySignal || bBlockedAhead || GetFollowBrake() >= 0.5f || GetSignalBrake() >= 0.5f ||
		GetFollowThrottleScale() <= 0.f; // queued at the gap floor is a reason too

	if (!bIsMoving || bWaitingWithReason || Speed > StuckSpeedThreshold)
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

	// Author's rule: a vehicle never stops unless something explicitly stops it. Unexplained
	// immobility — off-road, goal gone stale — re-acquires the nearest route and resets the goal,
	// then keeps driving. Under kinematic movement the drive back always succeeds; parking is the
	// last resort reserved for having no route at all.
	UE_LOG(LogDeliveryGame, Log, TEXT("%s immobile for %.1fs with no reason to wait; re-acquiring a route."),
		*GetNameSafe(GetOwner()), StuckTimeout);

	if (ReacquireNearestPath())
	{
		UpdateDestination();
		StartMoving();
	}
	else
	{
		StopMoving();
		TimeSinceResumeAttempt = -StuckRetryDelay;
	}
}

void UDGPathFollowComponent::AdvanceToNextPath()
{
	// The route was normally decided JunctionPlanDistance ago; choosing here is the fallback for a
	// handoff that arrives without a plan (very short splines, or a route assigned near its end).
	ADGPathActor* NextPath = PlannedNextPath;

	if (!NextPath)
	{
		NextPath = TargetSpline ? TargetSpline->ChooseNextPath() : nullptr;
	}

	// NextPaths is empty on every existing path actor — the Blueprint relied on deciders for
	// continuations — so fall back to finding a connecting route rather than parking permanently.
	if (!NextPath)
	{
		NextPath = FindContinuationPath();
	}

	if (!NextPath)
	{
		UE_LOG(LogDeliveryGame, Log, TEXT("%s reached the end of %s with no continuation; stopping."),
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

#if !UE_BUILD_SHIPPING
	// Off-road tattletale (author request, 2026-08-10: "detect when the vehicles are leaving").
	// The roadway is ~5 m from centreline to kerb; a vehicle further out than 7 m is on the grass.
	// Modulo throttle instead of a member flag so this stays Live-Coding safe.
	if (bIsMoving && FMath::Abs(CurrentLateralOffset) > 700.f)
	{
		if (const UWorld* World = GetWorld(); World && FMath::Fmod(World->GetTimeSeconds(), 2.f) < DeltaTime)
		{
			UE_LOG(LogDeliveryGame, Warning,
				TEXT("%s OFF-ROAD: %.0f cm %s of %s (dir %d, %.0f cm along, route age %.1fs, planned %s)"),
				*GetNameSafe(GetOwner()), FMath::Abs(CurrentLateralOffset),
				CurrentLateralOffset < 0.f ? TEXT("left") : TEXT("right"),
				*GetNameSafe(TargetSpline), TravelDirection, DistanceAlongSpline,
				GetTimeSinceLastPathChange(), *GetNameSafe(PlannedNextPath));
		}
	}
#endif

	// Break mutual holds: two vehicles can each sit in the other's traffic volume and wait forever.
	// Deliberately ignores bHeldBySignal — releasing that would mean running red lights.
	if (bBlockedAhead && !bHeldBySignal)
	{
		TimeBlocked += DeltaTime;
		if (BlockedTimeout > 0.f && TimeBlocked >= BlockedTimeout)
		{
			UE_LOG(LogDeliveryGame, Log,
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

			// Resume the route we already have — a parked vehicle's route is almost always still the
			// right one. Picking a fresh route here (an earlier version did) re-pathed vehicles that
			// were merely waiting near a junction, and a snap onto a route whose end they were sitting
			// at triggered an instant re-advance: path ping-pong while physically frozen.
			if (TargetSpline)
			{
				UpdateDestination();
				StartMoving();
			}
			else if (ReacquireNearestPath())
			{
				UpdateDestination();
				StartMoving();
			}

			if (bIsMoving)
			{
				UE_LOG(LogDeliveryGame, Log, TEXT("%s resumed onto %s."),
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

	if (MoveMode == EDGPathFollowMoveMode::Kinematic)
	{
		ProceedKinematic(DeltaTime);
	}
	else
	{
		ProceedToDestination(DeltaTime);
	}

	UpdateStuckRecovery(DeltaTime);

	if (bDrawDebug && CVarDGTrafficDebugDraw.GetValueOnGameThread())
	{
		DrawDebugVisuals();
	}
}

void UDGPathFollowComponent::ApplyHoldOutputs(UChaosWheeledVehicleMovementComponent& Movement) const
{
	// Holding with the handbrake and a *released* pedal matters: with bReverseAsBrake enabled, a held
	// brake at standstill is a reverse-throttle request — which is how "stopped" vans were slowly
	// driving themselves backwards. The pedal is for shedding speed; the handbrake is for staying put.
	Movement.SetThrottleInput(0.f);
	Movement.SetBrakeInput(0.f);
	Movement.SetHandbrakeInput(true);
}

void UDGPathFollowComponent::ProceedKinematic(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!Owner || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// ---- Desired speed: the same evaluators that fed throttle/brake, expressed directly ----
	float DesiredSpeed = 0.f;

	if (bIsMoving && TargetSpline && !bHeldBySignal && !bBlockedAhead)
	{
		// Road limit scaled for the corner ahead — in cm/s. 0 target means "no governing", so fall
		// back to the vehicle's own cruise number rather than standing still.
		const float TargetMPH = GetTargetSpeedMPH();
		DesiredSpeed = ((TargetMPH > 0.f) ? TargetMPH : CruiseSpeedMPH) * 44.704f;

		// The vehicle ahead and the stop line both cap speed; the strictest wins. Where physics mode
		// converted these into brake pedal pressure, kinematic just obeys them exactly.
		DesiredSpeed *= GetFollowThrottleScale();
		DesiredSpeed *= 1.f - FMath::Max(GetFollowBrake(), GetSignalBrake());
	}

	// ---- Integrate speed ----
	const float Rate = (DesiredSpeed > KinematicSpeed) ? KinematicAcceleration : KinematicBraking;
	KinematicSpeed = FMath::FInterpConstantTo(KinematicSpeed, FMath::Max(DesiredSpeed, 0.f), DeltaTime, Rate);

	// Mirror into the debug fields so the overlay stays meaningful in either mode.
	CurrentThrottle = (DesiredSpeed > KINDA_SMALL_NUMBER) ? KinematicSpeed / FMath::Max(DesiredSpeed, 1.f) : 0.f;

	if (KinematicSpeed < 1.f)
	{
		return;
	}

	// ---- Heading: swing toward the goal at a capped rate ----
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	const FVector ToGoal = (Destination - Owner->GetActorLocation()).GetSafeNormal2D();

	FVector NewForward = Forward;
	if (!ToGoal.IsNearlyZero())
	{
		const float CurrentYaw = FMath::Atan2(Forward.Y, Forward.X);
		const float GoalYaw = FMath::Atan2(ToGoal.Y, ToGoal.X);
		const float HeadingError = FMath::FindDeltaAngleRadians(CurrentYaw, GoalYaw);

		// Exponential approach, capped by the yaw rate. Taking the full error every tick transmitted
		// each little jitter of the goal (the lane PD recomputes it constantly) straight into the
		// heading — the in-lane wobble. SteeringInterpSpeed plays the same smoothing role it does for
		// the physics steering.
		const float MaxStep = FMath::DegreesToRadians(KinematicYawRate) * DeltaTime;
		const float SmoothedStep = HeadingError * FMath::Min(SteeringInterpSpeed * DeltaTime, 1.f);
		const float Delta = FMath::Clamp(SmoothedStep, -MaxStep, MaxStep);

		const float NewYaw = CurrentYaw + Delta;
		NewForward = FVector(FMath::Cos(NewYaw), FMath::Sin(NewYaw), 0.f);

		CurrentSteering = FMath::Clamp(FMath::RadiansToDegrees(Delta) / FMath::Max(FMath::RadiansToDegrees(MaxStep), KINDA_SMALL_NUMBER), -1.f, 1.f);
	}

	// ---- Move. Z is held: the map is flat and the assets assume it; varied terrain later means a
	// ground trace here, not physics. ----
	const FVector NewLocation = Owner->GetActorLocation() + NewForward * (KinematicSpeed * DeltaTime);

	Owner->SetActorLocationAndRotation(NewLocation, NewForward.Rotation(),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
}

void UDGPathFollowComponent::ProceedToDestination(float DeltaTime)
{
	UChaosWheeledVehicleMovementComponent* Movement = GetMovement();
	const AActor* Owner = GetOwner();
	if (!Movement || !Owner)
	{
		return;
	}

	const float CurrentSpeed = FMath::Abs(Movement->GetForwardSpeed());

	if (!bIsMoving || !TargetSpline)
	{
		CurrentSteering = FMath::FInterpTo(CurrentSteering, 0.f, DeltaTime, SteeringInterpSpeed);
		CurrentThrottle = 0.f;
		Movement->SetSteeringInput(CurrentSteering);

		if (CurrentSpeed <= StuckSpeedThreshold)
		{
			ApplyHoldOutputs(*Movement);
		}
		else
		{
			// Still rolling: shed speed with the service brake; the handbrake takes over once parked.
			Movement->SetThrottleInput(0.f);
			Movement->SetBrakeInput(StoppingBrakeForce);
			Movement->SetHandbrakeInput(false);
		}
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

		if (CurrentSpeed <= StuckSpeedThreshold)
		{
			ApplyHoldOutputs(*Movement);
		}
		else
		{
			Movement->SetThrottleInput(0.f);
			Movement->SetBrakeInput(StoppingBrakeForce);
			Movement->SetHandbrakeInput(false);
		}
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

	// Three independent limits: the gap to the vehicle ahead, the deceleration needed not to hit it,
	// and the deceleration needed to stop at a signal's line. The strictest wins.
	const float FollowScale = GetFollowThrottleScale();
	const float FollowBrake = GetFollowBrake();
	const float SignalBrake = GetSignalBrake();
	Throttle *= FMath::Min(FollowScale, 1.f - FMath::Max(FollowBrake, SignalBrake));

	CurrentThrottle = FMath::Clamp(Throttle, 0.f, 1.f);

	// Whichever demands the most braking: the vehicle ahead, the stop line, or the corner.
	const float DemandedBrake = FMath::Max3(FollowBrake, SignalBrake, OverspeedBrake) * StoppingBrakeForce;

	Movement->SetSteeringInput(CurrentSteering);

	// Stationary and meant to stay that way (queued, or at a stop line): park on the handbrake
	// instead of leaning on the pedal — see ApplyHoldOutputs for why the pedal must be released.
	if (CurrentThrottle < 0.05f && CurrentSpeed <= StuckSpeedThreshold && DemandedBrake > 0.f)
	{
		ApplyHoldOutputs(*Movement);
		return;
	}

	Movement->SetThrottleInput(CurrentThrottle);
	Movement->SetBrakeInput(DemandedBrake);
	Movement->SetHandbrakeInput(false);

	// Launch insurance. These manual gearboxes rely on arcade mode to engage a gear from throttle
	// input, and after a long handbrake hold that re-engagement can fail — the trace showed a van
	// released on green sitting at full throttle for 5 s without moving. If we are demanding motion
	// at a standstill and the box is in neutral, shift it ourselves.
	if (CurrentThrottle > 0.1f && CurrentSpeed < 5.f && Movement->GetCurrentGear() == 0)
	{
		Movement->SetTargetGear(1, /*bImmediate=*/true);
	}
}

FString UDGPathFollowComponent::GetDebugStatus() const
{
	const float SpeedMPH = GetVehicleSpeed() / 44.704f;

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
	else if (GetSignalBrake() > 0.f)
	{
		Status = FString::Printf(TEXT("STOPPING FOR SIGNAL: %.0f cm to line"), SignalStopDistance);
	}
	else if (FollowScale < 1.f || GetFollowBrake() > 0.f)
	{
		Status = FString::Printf(TEXT("Following: %.0f cm gap, closing %.0f cm/s, brake %.0f%%"),
			TrafficClearance, TrafficClosingSpeed, GetFollowBrake() * 100.f);
	}
	else
	{
		Status = bIsMoving ? TEXT("Moving") : TEXT("Stopped");
	}

	const FString PathLine = PlannedNextPath
		? FString::Printf(TEXT("%s (%s) -> %s"), *GetNameSafe(TargetSpline),
			(TravelDirection > 0) ? TEXT("forward") : TEXT("reverse"), *GetNameSafe(PlannedNextPath))
		: FString::Printf(TEXT("%s (%s)"), *GetNameSafe(TargetSpline),
			(TravelDirection > 0) ? TEXT("forward") : TEXT("reverse"));

	return FString::Printf(
		TEXT("%s\nPath: %s\nLane: %+.0f cm / target %+.0f\nProgress: %.0f cm (%.0f%%)\n")
		TEXT("Speed: %.1f mph\nThrottle: %.2f  Steer: %+.2f\n%s"),
		*GetNameSafe(GetOwner()),
		*PathLine,
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
