// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traffic/DGTrafficSubsystem.h"

#include "DeliveryGame.h"
#include "Traffic/DGPathActor.h"

void UDGTrafficSubsystem::RegisterPath(ADGPathActor* Path)
{
	if (Path)
	{
		RegisteredPaths.AddUnique(Path);
	}
}

void UDGTrafficSubsystem::UnregisterPath(ADGPathActor* Path)
{
	RegisteredPaths.RemoveSingleSwap(Path);
}

ADGPathActor* UDGTrafficSubsystem::FindNearestPath(const FVector& WorldLocation, float& OutDistanceAlongSpline) const
{
	OutDistanceAlongSpline = 0.f;

	ADGPathActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();

	for (const TObjectPtr<ADGPathActor>& Path : RegisteredPaths)
	{
		if (!Path)
		{
			continue;
		}

		const double DistSq = Path->GetDistanceSquaredTo(WorldLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Path;
		}
	}

	if (Best)
	{
		FVector ClosestPoint;
		Best->GetClosestPoint(WorldLocation, ClosestPoint, OutDistanceAlongSpline);
	}
	else
	{
		UE_LOG(LogDeliveryGame, Warning,
			TEXT("FindNearestPath found no registered paths. Are there any ADGPathActor (BP_Path) actors in the level?"));
	}

	return Best;
}

TArray<ADGPathActor*> UDGTrafficSubsystem::GetRegisteredPaths() const
{
	TArray<ADGPathActor*> Result;
	Result.Reserve(RegisteredPaths.Num());
	for (const TObjectPtr<ADGPathActor>& Path : RegisteredPaths)
	{
		if (Path)
		{
			Result.Add(Path);
		}
	}
	return Result;
}
