// Copyright Epic Games, Inc. All Rights Reserved.

#include "Delivery/DGDeliveryPointActor.h"

#include "Components/BoxComponent.h"
#include "Delivery/DGDeliverySubsystem.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

ADGDeliveryPointActor::ADGDeliveryPointActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);

	// Big enough that stopping the jeep roughly at the door counts; deliveries should feel
	// generous, not like threading a needle. Overridable per instance.
	Trigger->SetBoxExtent(FVector(350.f, 350.f, 250.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
}

void ADGDeliveryPointActor::BeginPlay()
{
	Super::BeginPlay();

	if (PointId.IsNone())
	{
		PointId = GetFName();
	}
	if (DisplayName.IsEmpty())
	{
		DisplayName = FText::FromName(PointId);
	}

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ADGDeliveryPointActor::OnTriggerBeginOverlap);

	if (UDGDeliverySubsystem* Delivery = GetWorld() ? GetWorld()->GetSubsystem<UDGDeliverySubsystem>() : nullptr)
	{
		Delivery->RegisterPoint(this);
	}

	SetActorTickEnabled(bDrawDebug);
}

void ADGDeliveryPointActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDGDeliverySubsystem* Delivery = GetWorld() ? GetWorld()->GetSubsystem<UDGDeliverySubsystem>() : nullptr)
	{
		Delivery->UnregisterPoint(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ADGDeliveryPointActor::OnTriggerBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	// Player only — on foot or driving. AI traffic wandering through a gas station forecourt must
	// never complete anyone's delivery.
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}

	if (UDGDeliverySubsystem* Delivery = GetWorld() ? GetWorld()->GetSubsystem<UDGDeliverySubsystem>() : nullptr)
	{
		Delivery->NotifyPlayerAtPoint(*this);
	}
}

void ADGDeliveryPointActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bDrawDebug || !CVarDGTrafficDebugDraw.GetValueOnGameThread())
	{
		return;
	}

	DrawDebugBox(GetWorld(), Trigger->GetComponentLocation(), Trigger->GetScaledBoxExtent(),
		Trigger->GetComponentQuat(), FColor::Cyan, false, -1.f, 0, 4.f);
}
