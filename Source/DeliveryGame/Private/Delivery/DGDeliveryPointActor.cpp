// Copyright Epic Games, Inc. All Rights Reserved.

#include "Delivery/DGDeliveryPointActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Delivery/DGDeliverySubsystem.h"
#include "DeliveryGame.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

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

	ParcelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ParcelMesh"));
	ParcelMesh->SetupAttachment(Trigger);
	ParcelMesh->SetRelativeLocation(FVector(0.f, 0.f, -30.f));
	ParcelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ParcelMesh->SetVisibility(false);

	// Placeholder parcel until the real prop is sourced (see docs/ASSET_TODO.md).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ParcelMeshFinder(
		TEXT("/Game/Game/Meshes/Props/StreetProps/SM_Prop_CarboardBox_03.SM_Prop_CarboardBox_03"));
	if (ParcelMeshFinder.Succeeded())
	{
		DefaultParcelMesh = ParcelMeshFinder.Object;
		ParcelMesh->SetStaticMesh(ParcelMeshFinder.Object);
	}
}

void ADGDeliveryPointActor::ApplyRole(EDGPointRole NewRole, UStaticMesh* ParcelMeshForRole)
{
	CurrentRole = NewRole;

	if (!ParcelMesh)
	{
		return;
	}

	// A parcel only exists where something is waiting to be picked up. Drop-offs get the marker
	// and the prompt, but no box sitting on the pavement to confuse things.
	const bool bShow = bShowParcelWhenPickup &&
		(NewRole == EDGPointRole::Pickup || NewRole == EDGPointRole::Both);

	if (bShow)
	{
		// The waiting job decides what is waiting: recognising the order at a glance is the
		// point (the arsonist's bottle of water reads very differently from a parcel).
		UStaticMesh* const Wanted = ParcelMeshForRole ? ParcelMeshForRole : DefaultParcelMesh.Get();
		if (Wanted && ParcelMesh->GetStaticMesh() != Wanted)
		{
			ParcelMesh->SetStaticMesh(Wanted);
		}
	}

	ParcelMesh->SetVisibility(bShow);
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
	Trigger->OnComponentEndOverlap.AddDynamic(this, &ADGDeliveryPointActor::OnTriggerEndOverlap);

	// These began life as one-shot VFX markers and the old Blueprint system hid them, so four of
	// the five points carry Actor-Hidden-In-Game saved in the map. A hidden actor hides *every*
	// component it owns, which silently defeated the parcel's own visibility — only Ness Mart,
	// the one point that was not hidden, ever showed its box (2026-08-12). The point actor itself
	// draws nothing but the parcel, so un-hiding it here is safe and cannot be shadowed.
	SetActorHiddenInGame(false);

	// Rest the parcel on whatever is under this point rather than at a fixed offset. Points sit
	// at whatever height they were dropped at — a shop floor, a kerb, grass — so a constant
	// offset buried the box underground at some of them and floated it at others (2026-08-12:
	// the Bakery point sat 9 cm above its ground, putting the parcel 21 cm below it).
	if (ParcelMesh)
	{
		// Start the trace just above the point, not high above it: several points sit inside or
		// under buildings, and a trace beginning overhead lands on the roof — ShellStop's parcel
		// ended up 115 cm in the air and Home's at z=412 (2026-08-12). Hits above the point are
		// rejected outright, so the parcel can only ever come to rest on the floor beneath it.
		// Start only just above the point. Points can sit *inside* a building (ShellStop and Home
		// are now within their placeholder shops), and a trace starting higher begins inside that
		// mesh: it reports a hit at its own start position and the parcel ends up embedded in the
		// structure. bStartPenetrating catches that case, and hits above the point are rejected
		// so the parcel can only rest on a floor beneath it.
		const FVector Origin = GetActorLocation();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DGParcelGroundSnap), /*bTraceComplex=*/false, this);

		const bool bHit = GetWorld()->LineTraceSingleByChannel(
			Hit, Origin + FVector(0.f, 0.f, 10.f), Origin - FVector(0.f, 0.f, 600.f),
			ECC_WorldStatic, Params);

		const bool bUsable = bHit && !Hit.bStartPenetrating && Hit.ImpactPoint.Z <= Origin.Z + 10.f;
		if (bUsable)
		{
			ParcelMesh->SetWorldLocation(Hit.ImpactPoint + FVector(0.f, 0.f, 2.f));
		}
		else
		{
			// Never leave it buried: sitting slightly proud of the point beats invisible.
			ParcelMesh->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
			UE_LOG(LogDeliveryGame, Warning,
				TEXT("%s could not find a floor (%s); parcel placed at the point itself. "
					 "Move the point out of solid geometry if the parcel looks wrong."),
				*GetName(),
				!bHit ? TEXT("no hit") :
				Hit.bStartPenetrating ? TEXT("point is inside geometry") : TEXT("surface was above the point"));
		}
	}

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

bool ADGDeliveryPointActor::IsPlayerPawn(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	return Pawn && Pawn->IsPlayerControlled();
}

void ADGDeliveryPointActor::OnTriggerBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	// Player only — on foot or driving. AI traffic wandering through a gas station forecourt must
	// never complete anyone's delivery.
	if (!IsPlayerPawn(OtherActor) || bPlayerInRange)
	{
		return;
	}

	bPlayerInRange = true;
	OnPlayerInRangeChanged(true);

	UDGDeliverySubsystem* Delivery = GetWorld() ? GetWorld()->GetSubsystem<UDGDeliverySubsystem>() : nullptr;
	if (!Delivery)
	{
		return;
	}

	Delivery->SetPointInRange(this, true);

	// Arriving is no longer the same as handing the package over: the player presses the interact
	// button (author, 2026-08-10). The auto-complete path survives only as a testing fallback.
	if (!Delivery->bRequireInteractToComplete)
	{
		Delivery->NotifyPlayerAtPoint(*this);
	}
}

void ADGDeliveryPointActor::OnTriggerEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	if (!IsPlayerPawn(OtherActor) || !bPlayerInRange)
	{
		return;
	}

	// The player can be in the trigger twice over — on foot beside the parked jeep — so only
	// leave range once nothing player-controlled is still inside.
	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, APawn::StaticClass());
	for (const AActor* Actor : Overlapping)
	{
		if (Actor != OtherActor && IsPlayerPawn(Actor))
		{
			return;
		}
	}

	bPlayerInRange = false;
	OnPlayerInRangeChanged(false);

	if (UDGDeliverySubsystem* Delivery = GetWorld() ? GetWorld()->GetSubsystem<UDGDeliverySubsystem>() : nullptr)
	{
		Delivery->SetPointInRange(this, false);
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
