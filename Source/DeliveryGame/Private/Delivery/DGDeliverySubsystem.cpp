// Copyright Epic Games, Inc. All Rights Reserved.

#include "Delivery/DGDeliverySubsystem.h"

#include "Delivery/DGDeliveryPointActor.h"
#include "DeliveryGame.h"

void UDGDeliverySubsystem::RegisterPoint(ADGDeliveryPointActor* Point)
{
	if (!Point)
	{
		return;
	}

	if (const TWeakObjectPtr<ADGDeliveryPointActor>* Existing = Points.Find(Point->PointId);
		Existing && Existing->IsValid() && Existing->Get() != Point)
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("Delivery point id '%s' is used by both %s and %s; the later registration wins."),
			*Point->PointId.ToString(), *Existing->Get()->GetName(), *Point->GetName());
	}

	Points.Add(Point->PointId, Point);
	UE_LOG(LogDeliveryGame, Log, TEXT("Delivery point registered: %s ('%s')"),
		*Point->PointId.ToString(), *Point->DisplayName.ToString());
}

void UDGDeliverySubsystem::UnregisterPoint(ADGDeliveryPointActor* Point)
{
	if (!Point)
	{
		return;
	}

	if (const TWeakObjectPtr<ADGDeliveryPointActor>* Existing = Points.Find(Point->PointId);
		Existing && Existing->Get() == Point)
	{
		Points.Remove(Point->PointId);
	}

	// A vanishing point mid-job (streamed out, deleted) cannot be completed — fail cleanly rather
	// than leaving a job whose objective no longer exists.
	if (State != EDGDeliveryState::Idle && (ActiveJob.Pickup == Point || ActiveJob.Dropoff == Point))
	{
		FailActiveJob(TEXT("objective point removed"));
	}
}

TArray<ADGDeliveryPointActor*> UDGDeliverySubsystem::GetRegisteredPoints() const
{
	TArray<ADGDeliveryPointActor*> Result;
	Result.Reserve(Points.Num());
	for (const TPair<FName, TWeakObjectPtr<ADGDeliveryPointActor>>& Pair : Points)
	{
		if (ADGDeliveryPointActor* Point = Pair.Value.Get())
		{
			Result.Add(Point);
		}
	}
	return Result;
}

ADGDeliveryPointActor* UDGDeliverySubsystem::FindPoint(FName PointId) const
{
	const TWeakObjectPtr<ADGDeliveryPointActor>* Found = Points.Find(PointId);
	return Found ? Found->Get() : nullptr;
}

bool UDGDeliverySubsystem::StartRandomJob()
{
	if (State != EDGDeliveryState::Idle)
	{
		return false;
	}

	TArray<ADGDeliveryPointActor*> Candidates = GetRegisteredPoints();
	if (Candidates.Num() < 2)
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("StartRandomJob: only %d delivery point(s) registered; need at least 2."),
			Candidates.Num());
		return false;
	}

	const int32 PickupIndex = FMath::RandHelper(Candidates.Num());
	int32 DropoffIndex = FMath::RandHelper(Candidates.Num() - 1);
	if (DropoffIndex >= PickupIndex)
	{
		++DropoffIndex;
	}

	return StartJob(Candidates[PickupIndex]->PointId, Candidates[DropoffIndex]->PointId);
}

bool UDGDeliverySubsystem::StartJob(FName PickupId, FName DropoffId)
{
	if (State != EDGDeliveryState::Idle)
	{
		UE_LOG(LogDeliveryGame, Warning, TEXT("StartJob rejected: a job is already active."));
		return false;
	}

	FDGDeliveryJob Job;
	Job.Pickup = FindPoint(PickupId);
	Job.Dropoff = FindPoint(DropoffId);
	if (!Job.IsValid())
	{
		UE_LOG(LogDeliveryGame, Warning, TEXT("StartJob rejected: '%s' -> '%s' is not a valid pair."),
			*PickupId.ToString(), *DropoffId.ToString());
		return false;
	}

	PriceJob(Job);
	ActiveJob = Job;
	State = EDGDeliveryState::AwaitingPickup;

	UE_LOG(LogDeliveryGame, Log, TEXT("Job started: %s -> %s ($%d, %.0fs once picked up)"),
		*Job.Pickup->PointId.ToString(), *Job.Dropoff->PointId.ToString(),
		Job.PayoutDollars, Job.TimeLimitSeconds);

	ActiveJob.Pickup->OnBecameActiveObjective();
	OnJobStarted.Broadcast();
	return true;
}

void UDGDeliverySubsystem::CancelJob()
{
	if (State == EDGDeliveryState::Idle)
	{
		return;
	}
	FailActiveJob(TEXT("cancelled by player"));
}

void UDGDeliverySubsystem::NotifyPlayerAtPoint(ADGDeliveryPointActor& Point)
{
	switch (State)
	{
	case EDGDeliveryState::AwaitingPickup:
		if (ActiveJob.Pickup == &Point)
		{
			State = EDGDeliveryState::Delivering;
			DeliveryDeadline = GetWorld()->GetTimeSeconds() + ActiveJob.TimeLimitSeconds;

			UE_LOG(LogDeliveryGame, Log, TEXT("Package picked up at %s; %.0fs to deliver to %s."),
				*Point.PointId.ToString(), ActiveJob.TimeLimitSeconds,
				*ActiveJob.Dropoff->PointId.ToString());

			Point.OnObjectiveCompleted();
			ActiveJob.Dropoff->OnBecameActiveObjective();
			OnPickupComplete.Broadcast();
		}
		break;

	case EDGDeliveryState::Delivering:
		if (ActiveJob.Dropoff == &Point)
		{
			const int32 Payout = ActiveJob.PayoutDollars;

			UE_LOG(LogDeliveryGame, Log, TEXT("Delivered to %s with %.1fs to spare: +$%d."),
				*Point.PointId.ToString(), GetTimeRemaining(), Payout);

			Point.OnObjectiveCompleted();
			ClearJobPoints();
			State = EDGDeliveryState::Idle;
			ActiveJob = FDGDeliveryJob();

			SetMoney(MoneyDollars + Payout);
			OnDelivered.Broadcast(Payout);
		}
		break;

	default:
		break;
	}
}

float UDGDeliverySubsystem::GetTimeRemaining() const
{
	if (State != EDGDeliveryState::Delivering)
	{
		return 0.f;
	}
	return FMath::Max(0.f, DeliveryDeadline - GetWorld()->GetTimeSeconds());
}

FVector UDGDeliverySubsystem::GetCurrentObjectiveLocation(bool& bValid) const
{
	const ADGDeliveryPointActor* Objective =
		State == EDGDeliveryState::AwaitingPickup ? ActiveJob.Pickup.Get() :
		State == EDGDeliveryState::Delivering    ? ActiveJob.Dropoff.Get() : nullptr;

	bValid = Objective != nullptr;
	return Objective ? Objective->GetActorLocation() : FVector::ZeroVector;
}

void UDGDeliverySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (State == EDGDeliveryState::Delivering && GetWorld()->GetTimeSeconds() >= DeliveryDeadline)
	{
		FailActiveJob(TEXT("out of time"));
	}
}

TStatId UDGDeliverySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDGDeliverySubsystem, STATGROUP_Tickables);
}

bool UDGDeliverySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDGDeliverySubsystem::PriceJob(FDGDeliveryJob& Job) const
{
	const float StraightCm = FVector::Dist(
		Job.Pickup->GetActorLocation(), Job.Dropoff->GetActorLocation());
	const float RouteCm = StraightCm * RouteDistanceFactor;

	Job.TimeLimitSeconds = RouteCm / ExpectedSpeedCmPerSec + TimeLimitGraceSeconds;
	Job.PayoutDollars = BasePayoutDollars + FMath::RoundToInt32(RouteCm / 100000.f) * PayoutDollarsPerKm;
}

void UDGDeliverySubsystem::SetMoney(int32 NewMoney)
{
	if (MoneyDollars == NewMoney)
	{
		return;
	}
	MoneyDollars = NewMoney;
	OnMoneyChanged.Broadcast(MoneyDollars);
}

void UDGDeliverySubsystem::FailActiveJob(const TCHAR* Reason)
{
	UE_LOG(LogDeliveryGame, Log, TEXT("Job failed (%s): %s -> %s"), Reason,
		ActiveJob.Pickup ? *ActiveJob.Pickup->PointId.ToString() : TEXT("?"),
		ActiveJob.Dropoff ? *ActiveJob.Dropoff->PointId.ToString() : TEXT("?"));

	ClearJobPoints();
	State = EDGDeliveryState::Idle;
	ActiveJob = FDGDeliveryJob();
	OnJobFailed.Broadcast();
}

void UDGDeliverySubsystem::ClearJobPoints()
{
	if (ActiveJob.Pickup)
	{
		ActiveJob.Pickup->OnCleared();
	}
	if (ActiveJob.Dropoff)
	{
		ActiveJob.Dropoff->OnCleared();
	}
}
