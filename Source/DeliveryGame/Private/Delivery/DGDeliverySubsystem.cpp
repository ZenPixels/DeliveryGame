// Copyright Epic Games, Inc. All Rights Reserved.

#include "Delivery/DGDeliverySubsystem.h"

#include "Delivery/DGDeliveryPointActor.h"
#include "DeliveryGame.h"
#include "Engine/StaticMesh.h"

// ---------------------------------------------------------------- Point registry

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

	PointsInRange.Remove(Point);

	// A point vanishing mid-job (streamed out, deleted) leaves jobs that can never complete.
	for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
	{
		const FDGDeliveryJob& Job = Jobs[Index];
		if (Job.Pickup == Point || Job.Dropoff == Point)
		{
			UE_LOG(LogDeliveryGame, Log, TEXT("Job %d dropped: an objective point was removed."), Job.JobId);
			OnJobLost.Broadcast(Job.JobId);
			Jobs.RemoveAt(Index);
		}
	}
	RefreshPointMarkers();
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

// ------------------------------------------------------------------ Job lookup

FDGDeliveryJob* UDGDeliverySubsystem::FindJob(int32 JobId)
{
	return Jobs.FindByPredicate([JobId](const FDGDeliveryJob& Job) { return Job.JobId == JobId; });
}

const FDGDeliveryJob* UDGDeliverySubsystem::FindJob(int32 JobId) const
{
	return Jobs.FindByPredicate([JobId](const FDGDeliveryJob& Job) { return Job.JobId == JobId; });
}

bool UDGDeliverySubsystem::GetJob(int32 JobId, FDGDeliveryJob& OutJob) const
{
	if (const FDGDeliveryJob* Job = FindJob(JobId))
	{
		OutJob = *Job;
		return true;
	}
	return false;
}

const FDGDeliveryJob* UDGDeliverySubsystem::GetPrimaryJob() const
{
	// Carrying beats fetching: what is in the vehicle is the thing under time pressure.
	if (const FDGDeliveryJob* Carrying = Jobs.FindByPredicate(
			[](const FDGDeliveryJob& Job) { return Job.Stage == EDGJobStage::Carrying; }))
	{
		return Carrying;
	}
	return Jobs.FindByPredicate([](const FDGDeliveryJob& Job) { return Job.Stage == EDGJobStage::Accepted; });
}

TArray<FDGDeliveryJob> UDGDeliverySubsystem::GetOffers() const
{
	TArray<FDGDeliveryJob> Result;
	for (const FDGDeliveryJob& Job : Jobs)
	{
		if (Job.Stage == EDGJobStage::Offered)
		{
			Result.Add(Job);
		}
	}
	// Soonest to lapse first — the board should read as a queue of urgency.
	Result.Sort([](const FDGDeliveryJob& A, const FDGDeliveryJob& B)
	{
		return A.OfferExpiryTime < B.OfferExpiryTime;
	});
	return Result;
}

TArray<FDGDeliveryJob> UDGDeliverySubsystem::GetHeldJobs() const
{
	TArray<FDGDeliveryJob> Result;
	for (const FDGDeliveryJob& Job : Jobs)
	{
		if (Job.IsHeld())
		{
			Result.Add(Job);
		}
	}
	return Result;
}

int32 UDGDeliverySubsystem::GetHeldJobCount() const
{
	int32 Count = 0;
	for (const FDGDeliveryJob& Job : Jobs)
	{
		Count += Job.IsHeld() ? 1 : 0;
	}
	return Count;
}

// ------------------------------------------------------------------- Economics

void UDGDeliverySubsystem::PriceJob(FDGDeliveryJob& Job) const
{
	if (!Job.IsValidJob())
	{
		return;
	}

	const float StraightCm = FVector::Dist(Job.Pickup->GetActorLocation(), Job.Dropoff->GetActorLocation());
	const float RouteCm = StraightCm * RouteDistanceFactor;

	// Fare per 100 m, not per km: island routes are 150-400 m, so a per-km rate rounded to zero
	// and every job paid the same flat fare (observed 2026-08-10).
	if (Job.PayoutDollars <= 0)
	{
		Job.PayoutDollars = BaseFareDollars + FMath::RoundToInt32((RouteCm / 10000.f) * FarePer100m);
	}

	if (Job.TimeLimitSeconds <= 0.f && Job.Kind != EDGJobKind::VIP)
	{
		Job.TimeLimitSeconds = RouteCm / FMath::Max(ExpectedSpeedCmPerSec, 1.f) + FullValueGraceSeconds;
	}

	Job.DecaySeconds = FMath::Max(Job.TimeLimitSeconds * DecayWindowFraction, MinDecaySeconds);
}

int32 UDGDeliverySubsystem::ComputePayout(const FDGDeliveryJob& Job) const
{
	// Untimed work (VIP) and anything not yet carried pays in full.
	if (!Job.IsTimed() || Job.Stage != EDGJobStage::Carrying)
	{
		return Job.PayoutDollars;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return Job.PayoutDollars;
	}

	const float Overdue = World->GetTimeSeconds() - Job.FullValueDeadline;
	if (Overdue <= 0.f)
	{
		return Job.PayoutDollars;
	}

	// Linear slide from full value to the floor. Late still pays something until it doesn't —
	// the job is never lost, only devalued.
	const float Alpha = FMath::Clamp(Overdue / FMath::Max(Job.DecaySeconds, 1.f), 0.f, 1.f);
	return FMath::RoundToInt32(FMath::Lerp(
		static_cast<float>(Job.PayoutDollars), static_cast<float>(Job.FloorDollars), Alpha));
}

int32 UDGDeliverySubsystem::GetCurrentPayout(int32 JobId) const
{
	const FDGDeliveryJob* Job = FindJob(JobId);
	return Job ? ComputePayout(*Job) : 0;
}

float UDGDeliverySubsystem::GetSecondsRemaining(int32 JobId) const
{
	const FDGDeliveryJob* Job = FindJob(JobId);
	const UWorld* World = GetWorld();
	if (!Job || !World)
	{
		return 0.f;
	}

	const float Now = World->GetTimeSeconds();
	switch (Job->Stage)
	{
	case EDGJobStage::Offered:
		return FMath::Max(0.f, Job->OfferExpiryTime - Now);
	case EDGJobStage::Carrying:
		return Job->IsTimed() ? FMath::Max(0.f, Job->FullValueDeadline - Now) : 0.f;
	default:
		return 0.f;
	}
}

bool UDGDeliverySubsystem::IsOfferQueueBlocked() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	const float Now = World->GetTimeSeconds();

	for (const FDGDeliveryJob& Job : Jobs)
	{
		// Story work never shares the board.
		if (Job.IsHeld() && Job.Kind == EDGJobKind::VIP)
		{
			return true;
		}

		// Dawdling costs access to work, not just the tip on this job.
		if (Job.Stage == EDGJobStage::Carrying && Job.IsTimed() && Now > Job.FullValueDeadline)
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------- Board actions

bool UDGDeliverySubsystem::MakePackageOffer(FDGDeliveryJob& OutJob)
{
	TArray<ADGDeliveryPointActor*> Candidates = GetRegisteredPoints();
	if (Candidates.Num() < 2)
	{
		return false;
	}

	const int32 PickupIndex = FMath::RandHelper(Candidates.Num());
	int32 DropoffIndex = FMath::RandHelper(Candidates.Num() - 1);
	if (DropoffIndex >= PickupIndex)
	{
		++DropoffIndex;
	}

	OutJob = FDGDeliveryJob();
	OutJob.JobId = NextJobId++;
	OutJob.Kind = EDGJobKind::Package;
	OutJob.Stage = EDGJobStage::Offered;
	OutJob.Pickup = Candidates[PickupIndex];
	OutJob.Dropoff = Candidates[DropoffIndex];

	// Ordinary work gets a random parcel from the pool when one is configured; otherwise the
	// pickup point's own default stands in.
	if (!GeneratedParcelMeshes.IsEmpty())
	{
		OutJob.ParcelMesh = GeneratedParcelMeshes[FMath::RandHelper(GeneratedParcelMeshes.Num())];
	}

	PriceJob(OutJob);

	if (const UWorld* World = GetWorld())
	{
		OutJob.OfferExpiryTime = World->GetTimeSeconds() + OfferLifetimeSeconds;
	}

	OutJob.Summary = FText::Format(
		NSLOCTEXT("Delivery", "PackageSummary", "{0} to {1} - ${2}"),
		OutJob.Pickup->DisplayName, OutJob.Dropoff->DisplayName, FText::AsNumber(OutJob.PayoutDollars));
	return true;
}

void UDGDeliverySubsystem::RefreshOffers()
{
	if (IsOfferQueueBlocked())
	{
		return;
	}

	int32 OfferCount = 0;
	for (const FDGDeliveryJob& Job : Jobs)
	{
		OfferCount += (Job.Stage == EDGJobStage::Offered) ? 1 : 0;
	}

	bool bAdded = false;
	while (OfferCount < MaxOffers)
	{
		FDGDeliveryJob NewOffer;
		if (!MakePackageOffer(NewOffer))
		{
			break;
		}
		Jobs.Add(NewOffer);
		++OfferCount;
		bAdded = true;
	}

	if (bAdded)
	{
		OnOffersChanged.Broadcast();
	}
}

bool UDGDeliverySubsystem::AcceptOffer(int32 JobId)
{
	FDGDeliveryJob* Job = FindJob(JobId);
	if (!Job || Job->Stage != EDGJobStage::Offered)
	{
		return false;
	}

	const int32 Held = GetHeldJobCount();
	const bool bHoldingVip = Jobs.ContainsByPredicate(
		[](const FDGDeliveryJob& Other) { return Other.IsHeld() && Other.Kind == EDGJobKind::VIP; });

	// VIP work is taken alone, in both directions: nothing else may be in hand when it starts,
	// and nothing else may be started while it is.
	if (Job->Kind == EDGJobKind::VIP && Held > 0)
	{
		UE_LOG(LogDeliveryGame, Log, TEXT("VIP job %d refused: %d other job(s) in hand."), JobId, Held);
		return false;
	}
	if (bHoldingVip)
	{
		UE_LOG(LogDeliveryGame, Log, TEXT("Job %d refused: a VIP delivery is in progress."), JobId);
		return false;
	}
	if (Job->Kind != EDGJobKind::VIP && Held >= JobCapacity)
	{
		UE_LOG(LogDeliveryGame, Log, TEXT("Job %d refused: at capacity (%d)."), JobId, JobCapacity);
		return false;
	}

	Job->Stage = EDGJobStage::Accepted;
	UE_LOG(LogDeliveryGame, Log, TEXT("Accepted job %d: %s -> %s ($%d)"),
		Job->JobId, *Job->Pickup->PointId.ToString(), *Job->Dropoff->PointId.ToString(), Job->PayoutDollars);

	RefreshPointMarkers();
	OnJobAccepted.Broadcast(JobId);
	OnOffersChanged.Broadcast();
	return true;
}

bool UDGDeliverySubsystem::DeclineOffer(int32 JobId)
{
	for (int32 Index = 0; Index < Jobs.Num(); ++Index)
	{
		if (Jobs[Index].JobId == JobId && Jobs[Index].Stage == EDGJobStage::Offered)
		{
			Jobs.RemoveAt(Index);
			OnJobLost.Broadcast(JobId);
			OnOffersChanged.Broadcast();
			return true;
		}
	}
	return false;
}

bool UDGDeliverySubsystem::AbandonJob(int32 JobId)
{
	for (int32 Index = 0; Index < Jobs.Num(); ++Index)
	{
		if (Jobs[Index].JobId == JobId && Jobs[Index].IsHeld())
		{
			UE_LOG(LogDeliveryGame, Log, TEXT("Job %d abandoned."), JobId);
			Jobs.RemoveAt(Index);
			RefreshPointMarkers();
			OnJobLost.Broadcast(JobId);
			return true;
		}
	}
	return false;
}

int32 UDGDeliverySubsystem::OfferVipJob(FName PickupId, FName DropoffId, int32 PayoutDollars,
	float TimeLimitSeconds, const FText& Summary, UStaticMesh* ParcelMesh)
{
	FDGDeliveryJob Job;
	Job.JobId = NextJobId++;
	Job.Kind = EDGJobKind::VIP;
	Job.Stage = EDGJobStage::Offered;
	Job.Pickup = FindPoint(PickupId);
	Job.Dropoff = FindPoint(DropoffId);
	if (!Job.IsValidJob())
	{
		UE_LOG(LogDeliveryGame, Warning, TEXT("OfferVipJob: '%s' -> '%s' is not a valid pair."),
			*PickupId.ToString(), *DropoffId.ToString());
		return 0;
	}

	Job.PayoutDollars = PayoutDollars;
	Job.TimeLimitSeconds = FMath::Max(0.f, TimeLimitSeconds);
	Job.Summary = Summary;
	Job.ParcelMesh = ParcelMesh;
	PriceJob(Job);

	// Story offers do not lapse: the player should never miss a plot beat by being busy.
	Job.OfferExpiryTime = TNumericLimits<float>::Max();

	Jobs.Add(Job);
	UE_LOG(LogDeliveryGame, Log, TEXT("VIP job %d posted: %s -> %s ($%d, %s)"),
		Job.JobId, *PickupId.ToString(), *DropoffId.ToString(), Job.PayoutDollars,
		Job.IsTimed() ? TEXT("timed") : TEXT("untimed"));
	OnOffersChanged.Broadcast();
	return Job.JobId;
}

// -------------------------------------------------------------- Interaction

void UDGDeliverySubsystem::SetPointInRange(ADGDeliveryPointActor* Point, bool bInRange)
{
	if (!Point)
	{
		return;
	}

	if (bInRange)
	{
		PointsInRange.Add(Point);
	}
	else
	{
		PointsInRange.Remove(Point);
	}
}

void UDGDeliverySubsystem::CountPendingAt(const ADGDeliveryPointActor& Point,
	int32& OutDeliveries, int32& OutPickups) const
{
	OutDeliveries = 0;
	OutPickups = 0;
	for (const FDGDeliveryJob& Job : Jobs)
	{
		if (Job.Stage == EDGJobStage::Carrying && Job.Dropoff == &Point)
		{
			++OutDeliveries;
		}
		else if (Job.Stage == EDGJobStage::Accepted && Job.Pickup == &Point)
		{
			++OutPickups;
		}
	}
}

ADGDeliveryPointActor* UDGDeliverySubsystem::FindBestInteractPoint() const
{
	// Deliveries outrank collections when the player is somehow standing in two triggers at once:
	// handing over money-in-hand should never be blocked by picking up more work.
	ADGDeliveryPointActor* Best = nullptr;
	int32 BestScore = 0;

	for (const TWeakObjectPtr<ADGDeliveryPointActor>& Weak : PointsInRange)
	{
		ADGDeliveryPointActor* Point = Weak.Get();
		if (!Point)
		{
			continue;
		}

		int32 Deliveries = 0;
		int32 Pickups = 0;
		CountPendingAt(*Point, Deliveries, Pickups);

		const int32 Score = Deliveries * 10 + Pickups;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Point;
		}
	}
	return Best;
}

bool UDGDeliverySubsystem::TryInteract()
{
	ADGDeliveryPointActor* Point = FindBestInteractPoint();
	if (!Point)
	{
		return false;
	}

	NotifyPlayerAtPoint(*Point);
	return true;
}

bool UDGDeliverySubsystem::GetInteractPrompt(FText& OutPrompt) const
{
	for (const TWeakObjectPtr<ADGDeliveryPointActor>& Weak : PointsInRange)
	{
		const ADGDeliveryPointActor* Point = Weak.Get();
		if (!Point)
		{
			continue;
		}

		int32 Deliveries = 0;
		int32 Pickups = 0;
		CountPendingAt(*Point, Deliveries, Pickups);

		if (Deliveries > 0 && Pickups > 0)
		{
			OutPrompt = NSLOCTEXT("Delivery", "PromptBoth", "Hand over and collect");
			return true;
		}
		if (Deliveries > 0)
		{
			OutPrompt = (Deliveries == 1)
				? FText::Format(NSLOCTEXT("Delivery", "PromptDeliver", "Deliver to {0}"), Point->DisplayName)
				: FText::Format(NSLOCTEXT("Delivery", "PromptDeliverMany", "Deliver {0} parcels"), FText::AsNumber(Deliveries));
			return true;
		}
		if (Pickups > 0)
		{
			OutPrompt = (Pickups == 1)
				? NSLOCTEXT("Delivery", "PromptCollect", "Collect parcel")
				: FText::Format(NSLOCTEXT("Delivery", "PromptCollectMany", "Collect {0} parcels"), FText::AsNumber(Pickups));
			return true;
		}
	}
	return false;
}

UStaticMesh* UDGDeliverySubsystem::ResolveParcelMeshFor(const ADGDeliveryPointActor& Point) const
{
	// The first job waiting to be collected here decides what is sitting on the pavement.
	for (const FDGDeliveryJob& Job : Jobs)
	{
		if (Job.Stage == EDGJobStage::Accepted && Job.Pickup == &Point && !Job.ParcelMesh.IsNull())
		{
			// Parcels are small props; a synchronous load on a job transition is cheap and keeps
			// the box from popping in a frame late.
			return Job.ParcelMesh.LoadSynchronous();
		}
	}
	return nullptr;
}

bool UDGDeliverySubsystem::SetJobParcelMesh(int32 JobId, UStaticMesh* ParcelMesh)
{
	FDGDeliveryJob* Job = FindJob(JobId);
	if (!Job)
	{
		return false;
	}

	Job->ParcelMesh = ParcelMesh;
	RefreshPointMarkers();
	return true;
}

EDGPointRole UDGDeliverySubsystem::GetPointRole(const ADGDeliveryPointActor* Point) const
{
	if (!Point)
	{
		return EDGPointRole::None;
	}

	int32 Deliveries = 0;
	int32 Pickups = 0;
	CountPendingAt(*Point, Deliveries, Pickups);

	if (Deliveries > 0 && Pickups > 0)
	{
		return EDGPointRole::Both;
	}
	if (Deliveries > 0)
	{
		return EDGPointRole::Dropoff;
	}
	return (Pickups > 0) ? EDGPointRole::Pickup : EDGPointRole::None;
}

int32 UDGDeliverySubsystem::GetCarriedCount() const
{
	int32 Count = 0;
	for (const FDGDeliveryJob& Job : Jobs)
	{
		Count += (Job.Stage == EDGJobStage::Carrying) ? 1 : 0;
	}
	return Count;
}

// ---------------------------------------------------------------- Arrivals

void UDGDeliverySubsystem::NotifyPlayerAtPoint(ADGDeliveryPointActor& Point)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	bool bChanged = false;

	// Deliveries first, so arriving at a point that is both a drop-off and another job's pickup
	// pays out before it hands over the next parcel.
	for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
	{
		FDGDeliveryJob& Job = Jobs[Index];
		if (Job.Stage != EDGJobStage::Carrying || Job.Dropoff != &Point)
		{
			continue;
		}

		const int32 Paid = ComputePayout(Job);
		const int32 JobId = Job.JobId;
		const bool bLate = Job.IsTimed() && Now > Job.FullValueDeadline;

		UE_LOG(LogDeliveryGame, Log, TEXT("Delivered job %d to %s: +$%d%s"),
			JobId, *Point.PointId.ToString(), Paid,
			bLate ? TEXT(" (late, decayed)") : TEXT(""));

		Point.OnObjectiveCompleted();
		Jobs.RemoveAt(Index);
		SetMoney(MoneyDollars + Paid);
		OnJobDelivered.Broadcast(JobId, Paid);
		bChanged = true;
	}

	// Then pickups. Several parcels can be collected from one stop.
	for (FDGDeliveryJob& Job : Jobs)
	{
		if (Job.Stage != EDGJobStage::Accepted || Job.Pickup != &Point)
		{
			continue;
		}

		Job.Stage = EDGJobStage::Carrying;
		Job.FullValueDeadline = Job.IsTimed() ? Now + Job.TimeLimitSeconds : TNumericLimits<float>::Max();

		UE_LOG(LogDeliveryGame, Log, TEXT("Picked up job %d at %s; %s to reach %s."),
			Job.JobId, *Point.PointId.ToString(),
			Job.IsTimed() ? *FString::Printf(TEXT("%.0fs of full value"), Job.TimeLimitSeconds) : TEXT("no clock"),
			*Job.Dropoff->PointId.ToString());

		Point.OnObjectiveCompleted();
		OnJobPickedUp.Broadcast(Job.JobId);
		bChanged = true;
	}

	if (bChanged)
	{
		RefreshPointMarkers();
	}
}

// --------------------------------------------------------------- Markers

void UDGDeliverySubsystem::RefreshPointMarkers()
{
	// Every held job lights its current objective, so multi-job work shows several markers.
	TSet<TWeakObjectPtr<ADGDeliveryPointActor>> Wanted;
	for (const FDGDeliveryJob& Job : Jobs)
	{
		if (Job.Stage == EDGJobStage::Accepted && Job.Pickup)
		{
			Wanted.Add(Job.Pickup);
		}
		else if (Job.Stage == EDGJobStage::Carrying && Job.Dropoff)
		{
			Wanted.Add(Job.Dropoff);
		}
	}

	for (const TWeakObjectPtr<ADGDeliveryPointActor>& Old : MarkedPoints)
	{
		if (ADGDeliveryPointActor* Point = Old.Get(); Point && !Wanted.Contains(Old))
		{
			Point->OnCleared();
		}
	}

	for (const TWeakObjectPtr<ADGDeliveryPointActor>& New : Wanted)
	{
		if (ADGDeliveryPointActor* Point = New.Get(); Point && !MarkedPoints.Contains(New))
		{
			Point->OnBecameActiveObjective();
		}
	}

	MarkedPoints = MoveTemp(Wanted);

	// Roles are refreshed on every registered point, not just the ones that changed: a point can
	// switch from pickup to drop-off (or gain a second job) without entering or leaving the set.
	for (const TPair<FName, TWeakObjectPtr<ADGDeliveryPointActor>>& Pair : Points)
	{
		if (ADGDeliveryPointActor* Point = Pair.Value.Get())
		{
			Point->ApplyRole(GetPointRole(Point), ResolveParcelMeshFor(*Point));
		}
	}
}

// ------------------------------------------------------- Primary-job compatibility

EDGDeliveryState UDGDeliverySubsystem::GetState() const
{
	const FDGDeliveryJob* Primary = GetPrimaryJob();
	if (!Primary)
	{
		return EDGDeliveryState::Idle;
	}
	return Primary->Stage == EDGJobStage::Carrying ? EDGDeliveryState::Delivering : EDGDeliveryState::AwaitingPickup;
}

FDGDeliveryJob UDGDeliverySubsystem::GetActiveJob() const
{
	const FDGDeliveryJob* Primary = GetPrimaryJob();
	return Primary ? *Primary : FDGDeliveryJob();
}

float UDGDeliverySubsystem::GetTimeRemaining() const
{
	const FDGDeliveryJob* Primary = GetPrimaryJob();
	return Primary ? GetSecondsRemaining(Primary->JobId) : 0.f;
}

FVector UDGDeliverySubsystem::GetCurrentObjectiveLocation(bool& bValid) const
{
	const FDGDeliveryJob* Primary = GetPrimaryJob();
	const ADGDeliveryPointActor* Objective = nullptr;
	if (Primary)
	{
		Objective = (Primary->Stage == EDGJobStage::Carrying) ? Primary->Dropoff.Get() : Primary->Pickup.Get();
	}

	bValid = Objective != nullptr;
	return Objective ? Objective->GetActorLocation() : FVector::ZeroVector;
}

bool UDGDeliverySubsystem::StartRandomJob()
{
	// Keeps the current phone widget working: it polls, sees Idle, and asks for work.
	TArray<FDGDeliveryJob> Offers = GetOffers();
	if (Offers.IsEmpty())
	{
		RefreshOffers();
		Offers = GetOffers();
	}

	for (const FDGDeliveryJob& Offer : Offers)
	{
		if (AcceptOffer(Offer.JobId))
		{
			return true;
		}
	}
	return false;
}

void UDGDeliverySubsystem::CancelJob()
{
	if (const FDGDeliveryJob* Primary = GetPrimaryJob())
	{
		AbandonJob(Primary->JobId);
	}
}

// ------------------------------------------------------------------- Money

void UDGDeliverySubsystem::SetMoney(int32 NewMoney)
{
	if (MoneyDollars == NewMoney)
	{
		return;
	}
	MoneyDollars = NewMoney;
	OnMoneyChanged.Broadcast(MoneyDollars);
}

void UDGDeliverySubsystem::ChargeMoney(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}
	// Debt is allowed: going negative is the economy's pressure, not a hard stop.
	SetMoney(MoneyDollars - Amount);
}

// -------------------------------------------------------------------- Tick

void UDGDeliverySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	bool bBoardChanged = false;

	for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
	{
		FDGDeliveryJob& Job = Jobs[Index];

		// Offers lapse.
		if (Job.Stage == EDGJobStage::Offered && Now >= Job.OfferExpiryTime)
		{
			UE_LOG(LogDeliveryGame, Log, TEXT("Offer %d lapsed."), Job.JobId);
			const int32 JobId = Job.JobId;
			Jobs.RemoveAt(Index);
			OnJobLost.Broadcast(JobId);
			bBoardChanged = true;
			continue;
		}

		// Held work never fails on the clock; it just starts costing money. Announce the moment
		// it turns, once, so the phone can complain about it.
		if (Job.Stage == EDGJobStage::Carrying && Job.IsTimed() &&
			Now > Job.FullValueDeadline && Job.FloorDollars < Job.PayoutDollars)
		{
			const int32 Current = ComputePayout(Job);
			if (Current < Job.PayoutDollars && Job.DecaySeconds > 0.f)
			{
				const float JustCrossed = Now - Job.FullValueDeadline;
				if (JustCrossed <= DeltaTime)
				{
					UE_LOG(LogDeliveryGame, Log, TEXT("Job %d is now decaying (was $%d)."),
						Job.JobId, Job.PayoutDollars);
					OnJobDecayStarted.Broadcast(Job.JobId);
				}
			}
		}
	}

	RefreshOffers();

	if (bBoardChanged)
	{
		OnOffersChanged.Broadcast();
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
