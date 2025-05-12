#include "FishingRod.h"
#include "FishingBobber.h" // Includes EBobberState
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "FishingLineComponent.h" // New Custom Fishing Line Component
#include "GameFramework/Character.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/Engine.h" // For GEngine, CVars
#include "Kismet/KismetMathLibrary.h"
#include "FishingLogChannels.h" // For custom logging
#include "DrawDebugHelpers.h" // For DrawDebugLine

// CVars for debugging
static TAutoConsoleVariable<int32> CVarDrawDebugFishingForces(
	TEXT("r.Fishing.DrawDebugForces"),
	0, // Default to off
	TEXT("Draw debug lines for forces on the fishing rod tip.\n")
	TEXT("0: Off\n")
	TEXT("1: On"),
	ECVF_Cheat);

// --- CONSTRUCTOR ---

AFishingRod::AFishingRod()
{
	PrimaryActorTick.bCanEverTick = true;

	RodRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RodRoot"));
	RootComponent = RodRootComponent;

	RodMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RodMesh"));
	RodMeshComponent->SetupAttachment(RodRootComponent);
    RodMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LineAttachPointComponent = CreateDefaultSubobject<USceneComponent>(TEXT("LineAttachPoint"));
	LineAttachPointComponent->SetupAttachment(RodMeshComponent);

	FishingLineComponent = nullptr; // Initialize to nullptr, will be created in OnConstruction

    // Default class for the line component to be spawned (can be overridden in BP_Rod)
    FishingLineClass = UFishingLineComponent::StaticClass();

	BobberClass = AFishingBobber::StaticClass();
	DefaultLaunchImpulse = 1000.0f;
	ReelInSpeed = 250.0f;
	ExtendSpeed = 150.0f;
	MinLineLength = 75.0f;
	MaxLineLength = 5000.0f;
    CastAimPitchAdjustment = 10.0f;

	bIsEquipped = false;
	CurrentOwnerCharacter = nullptr;
	AttachedBobber = nullptr;
	bIsPreparingToCast = false;
	bLineIsCastOut = false;
	bIsActivelyReeling = false;
	bIsActivelyExtending = false;
	CurrentLineLengthSetting = MinLineLength;
	ForceOnRodTip = FVector::ZeroVector;

	UE_LOG(LogFishingSystemSetup, Log, TEXT("AFishingRod Constructor: Base setup complete. FishingLineComponent will be created from FishingLineClass in OnConstruction."));
}

void AFishingRod::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    if (FishingLineComponent && (FishingLineComponent->GetClass() != FishingLineClass || !FishingLineClass))
    {
        if (FishingLineComponent->IsRegistered()) // Check if registered before trying to unregister
        {
            FishingLineComponent->UnregisterComponent();
        }
        FishingLineComponent->DestroyComponent(); // Will also detach
        FishingLineComponent = nullptr;
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s OnConstruction: Destroyed old FishingLineComponent due to class change or null class."), *GetName());
    }

    if (!FishingLineComponent && FishingLineClass)
    {
        FishingLineComponent = NewObject<UFishingLineComponent>(this, FishingLineClass, TEXT("InstancedFishingLine"));
        if (FishingLineComponent)
        {
            FishingLineComponent->SetupAttachment(LineAttachPointComponent);
            if (!FishingLineComponent->IsRegistered()) // Check before registering
            {
                FishingLineComponent->RegisterComponent();
            }

            FishingLineComponent->SetVisibility(false);
            FishingLineComponent->TargetCableLength = 10.0f; // Initial small length
            FishingLineComponent->DesiredSegmentLength = 10.0f; // Default, can be overridden by BP_Line defaults
            FishingLineComponent->SolverIterations = 10;
            FishingLineComponent->CableWidth = 2.0f;
            FishingLineComponent->StiffnessFactor = 0.85f; // Rod's preferred default
            FishingLineComponent->DampingFactor = 0.1f;   // Rod's preferred default
            FishingLineComponent->CableGravityScale = 1.0f;
            FishingLineComponent->DefaultParticleMass = 0.01f; // Rod's preferred default
            // bAutoSpawnAndAttachBobber was removed from UFishingLineComponent
            FishingLineComponent->bUseBezierInitialization = false;
            FishingLineComponent->MeshTessellation = 4;

            UE_LOG(LogFishingSystemSetup, Log, TEXT("%s OnConstruction: Created and configured new FishingLineComponent of class %s."), *GetName(), *FishingLineClass->GetName());
        }
        else
        {
            UE_LOG(LogFishingSystemSetup, Error, TEXT("%s OnConstruction: FAILED to create FishingLineComponent from class %s!"), *GetName(), FishingLineClass ? *FishingLineClass->GetName() : TEXT("NULL"));
        }
    }
    else if (FishingLineClass == nullptr && FishingLineComponent != nullptr)
    {
        if (FishingLineComponent->IsRegistered())
        {
            FishingLineComponent->UnregisterComponent();
        }
        FishingLineComponent->DestroyComponent();
        FishingLineComponent = nullptr;
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s OnConstruction: FishingLineClass is None, destroyed existing FishingLineComponent."), *GetName());
    }
}


// --- UE4 LIFECYCLE ---

void AFishingRod::BeginPlay()
{
	Super::BeginPlay();

    // OnConstruction should have created the FishingLineComponent if FishingLineClass was valid.
    // We primarily ensure its initial visibility is correct here.
	if (FishingLineComponent)
	{
		FishingLineComponent->SetVisibility(false); // Ensure it's hidden until equip
		UE_LOG(LogFishingSystemRod, Log, TEXT("%s BeginPlay: FishingLineComponent is VALID. Initial visibility set to false."), *GetName());
	}
	else
	{
		UE_LOG(LogFishingSystemRod, Error, TEXT("%s BeginPlay: FishingLineComponent is NULL! This might indicate an issue with OnConstruction or FishingLineClass setup."), *GetName());
	}
	// Removed duplicated logging.
}

void AFishingRod::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ForceOnRodTip = FVector::ZeroVector;

	if (!bIsEquipped || !AttachedBobber || !LineAttachPointComponent || !FishingLineComponent)
	{
		if (FishingLineComponent && FishingLineComponent->IsVisible())
		{
			FishingLineComponent->SetVisibility(false);
		}
		return;
	}

    if (FishingLineComponent && !FishingLineComponent->IsVisible())
    {
	    FishingLineComponent->SetVisibility(true);
    }

    if (bIsActivelyReeling)
    {
        if (bIsActivelyExtending) {
            bIsActivelyExtending = false;
            UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s Tick: Was extending, but actively reeling. Stopping extend."), *GetName());
        }

        float DistToPull = ReelInSpeed * DeltaTime;
        CurrentLineLengthSetting -= DistToPull;
        CurrentLineLengthSetting = FMath::Max(CurrentLineLengthSetting, MinLineLength);

        if (CurrentLineLengthSetting <= MinLineLength + KINDA_SMALL_NUMBER)
        {
            if (bLineIsCastOut) 
            {
                UE_LOG(LogFishingSystemRod, Log, TEXT("%s Tick (Reeling): Reached MinLineLength (%.1f) while cast. Switching to FullReelIn (Dangle)."), *GetName(), MinLineLength);
                FullReelIn(); 
            } else {
                 UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s Tick (Reeling): Reached MinLineLength (%.1f) while already dangling. Stopping reel."), *GetName(), MinLineLength);
                 if (bIsActivelyReeling) StopIncrementalReel(); 
            }
        }
    }
    else if (bIsActivelyExtending)
    {
        if (!bLineIsCastOut || (bLineIsCastOut && AttachedBobber && AttachedBobber->GetCurrentBobberState() != EBobberState::Flying) )
        {
            CurrentLineLengthSetting += ExtendSpeed * DeltaTime;
            CurrentLineLengthSetting = FMath::Min(CurrentLineLengthSetting, MaxLineLength);
            UE_LOG(LogFishingSystemLine, Verbose, TEXT("%s Extending line in Tick. New CurrentLineLengthSetting: %.1f. bLineIsCastOut: %d"), *GetName(), CurrentLineLengthSetting, bLineIsCastOut);
        }
        else if (bLineIsCastOut && AttachedBobber && AttachedBobber->GetCurrentBobberState() == EBobberState::Flying)
        {
             UE_LOG(LogFishingSystemLine, Verbose, TEXT("%s Tick: Tried to extend line, but bobber is flying. No manual extension, line pays out."), *GetName());
        }
    }
	
	if (bLineIsCastOut)
	{
		UpdateLineAndBobberWhenCast(DeltaTime);
	}
	else 
	{
		UpdateLineWhenDangling(DeltaTime);
	}

	CalculateForceOnRodTip();
	if (CVarDrawDebugFishingForces.GetValueOnGameThread() > 0)
	{
		DrawDebugForceOnRodTip();
	}

	if (GEngine && AttachedBobber && LineAttachPointComponent && FishingLineComponent)
	{
		FString BobberPhysState = TEXT("N/A");
		if (AttachedBobber->BobberMeshComponent)
		{
			BobberPhysState = AttachedBobber->BobberMeshComponent->IsSimulatingPhysics() ? TEXT("ON") : TEXT("OFF");
		}
        EBobberState BobberState = AttachedBobber->GetCurrentBobberState();
		FString DebugText = FString::Printf(TEXT("Target Line: %.0f\nActual BobberDist: %.0f\nCustomLine TargetLength: %.0f\nBobber State: %s (Phys: %s)\nLineIsCastOut: %s\nReeling: %s, Extending: %s\nForceOnRodTip: %s (Mag: %.2f)"),
			CurrentLineLengthSetting,
			FVector::Dist(LineAttachPointComponent->GetComponentLocation(), AttachedBobber->GetActorLocation()),
			FishingLineComponent->TargetCableLength,
			BobberState != EBobberState::Idle ? *UEnum::GetValueAsString(BobberState) : TEXT("Idle/Unknown"), 
            *BobberPhysState,
			bLineIsCastOut ? TEXT("TRUE") : TEXT("FALSE"),
            bIsActivelyReeling ? TEXT("TRUE") : TEXT("FALSE"),
            bIsActivelyExtending ? TEXT("TRUE") : TEXT("FALSE"),
			*ForceOnRodTip.ToString(),
			ForceOnRodTip.Size()
			);
		GEngine->AddOnScreenDebugMessage(1, 0.0f, FColor::Cyan, DebugText);
	}
}

// --- PUBLIC API - ROD ACTIONS & STATE ---

void AFishingRod::Equip(ACharacter* OwningCharacter, FName SocketName)
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s Equip called by %s to socket %s."), *GetName(), OwningCharacter ? *OwningCharacter->GetName() : TEXT("NULL"), *SocketName.ToString());

	if (!FishingLineComponent)
	{
        // OnConstruction might not have run yet if spawning and equipping in the same frame before first tick.
        // Or FishingLineClass might be None. Force OnConstruction to run now.
        UE_LOG(LogFishingSystemSetup, Warning, TEXT("%s Equip(): FishingLineComponent is NULL at START. Forcing OnConstruction."), *GetName());
        OnConstruction(GetTransform()); // Ensure component is created if possible
        if (!FishingLineComponent) {
		    UE_LOG(LogFishingSystemSetup, Error, TEXT("%s CRITICAL ERROR in Equip(): FishingLineComponent is STILL NULL after OnConstruction! Cannot proceed with equip."), *GetName());
            return;
        }
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Equip(): FishingLineComponent is NOW VALID after OnConstruction call."), *GetName());
	}
	else
	{
		UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Equip(): FishingLineComponent is VALID at the START of Equip method."), *GetName());
	}

	if (!OwningCharacter)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("%s OwningCharacter is null during Equip. Cannot equip."), *GetName());
		return;
	}

	CurrentOwnerCharacter = OwningCharacter;
	bIsEquipped = true;
	SetOwner(OwningCharacter); 

	if (USkeletalMeshComponent* CharMesh = OwningCharacter->GetMesh())
	{
		FAttachmentTransformRules AttachmentRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
		AttachToComponent(CharMesh, AttachmentRules, SocketName);
		UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Rod attached to %s's mesh at socket %s."), *GetName(), *OwningCharacter->GetName(), *SocketName.ToString());
	} else {
		UE_LOG(LogFishingSystemSetup, Warning, TEXT("%s OwningCharacter %s has no mesh to attach rod to."), *GetName(), *OwningCharacter->GetName());
	}

    if (RodMeshComponent) {
        RodMeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        RodMeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s RodMeshComponent set to ignore Pawn and Camera collision."), *GetName());
    }

	SpawnAndPrepareBobber(); 

	bLineIsCastOut = false; 
	bIsPreparingToCast = false;
    bIsActivelyReeling = false;
    bIsActivelyExtending = false;
	CurrentLineLengthSetting = MinLineLength; 

    if (FishingLineComponent) // Should be valid now
    {
        FishingLineComponent->SetCableLength(CurrentLineLengthSetting);
        FishingLineComponent->SetVisibility(AttachedBobber != nullptr);
    }
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Equip finished. LineIsCastOut: False, CurrentLineLength: %.1f"), *GetName(), CurrentLineLengthSetting);
}

void AFishingRod::Unequip()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Unequip called."), *GetName());
	if (!bIsEquipped)
    {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s Unequip called but not equipped. No action taken."), *GetName());
        return;
    }

	if (FishingLineComponent)
    {
        FishingLineComponent->AttachCableEndTo(nullptr, NAME_None);
        FishingLineComponent->SetVisibility(false);
        UE_LOG(LogFishingSystemLine, Log, TEXT("%s Unequip: Detached FishingLineComponent end and set invisible."), *GetName());
    }

	if (AttachedBobber)
	{
        UE_LOG(LogFishingSystemBobber, Log, TEXT("%s Unequip: Setting bobber %s to Idle State and destroying."), *GetName(), *AttachedBobber->GetName());
		AttachedBobber->SetBobberState(EBobberState::Idle);
        AttachedBobber->Destroy();
        AttachedBobber = nullptr;
	}
    
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    bIsEquipped = false;
	bIsPreparingToCast = false;
	bLineIsCastOut = false;
    bIsActivelyReeling = false;
    bIsActivelyExtending = false;
	CurrentOwnerCharacter = nullptr; 
    SetOwner(nullptr);

    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Unequip finished. All states reset."), *GetName());
}

void AFishingRod::InitiateCastAttempt()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s InitiateCastAttempt called."), *GetName());
	if (!bIsEquipped || bIsPreparingToCast || bLineIsCastOut || !AttachedBobber || !FishingLineComponent)
	{
		UE_LOG(LogFishingSystemRod, Warning, TEXT("%s InitiateCastAttempt: Invalid state. Equipped: %d, Preparing: %d, CastOut: %d, Bobber: %p, Line: %p"),
            *GetName(), bIsEquipped, bIsPreparingToCast, bLineIsCastOut, AttachedBobber, FishingLineComponent);
		return;
	}
	bIsPreparingToCast = true;
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s InitiateCastAttempt: bIsPreparingToCast set to true."), *GetName());
}

void AFishingRod::ExecuteLaunchFromAnimation()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s ExecuteLaunchFromAnimation called."), *GetName());
	if (!bIsEquipped || !bIsPreparingToCast || !AttachedBobber || !LineAttachPointComponent || !FishingLineComponent)
	{
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s ExecuteLaunchFromAnimation: Invalid state or missing components. Equipped: %d, Preparing: %d, Bobber: %p, LineAttach: %p, Line: %p"),
            *GetName(), bIsEquipped, bIsPreparingToCast, AttachedBobber, LineAttachPointComponent, FishingLineComponent);
		bIsPreparingToCast = false; 
		return;
	}

    FVector LaunchDirection;
	FVector CastOrigin = LineAttachPointComponent->GetComponentLocation();
    
    if (CurrentOwnerCharacter)
	{
		APlayerController* PC = Cast<APlayerController>(CurrentOwnerCharacter->GetController());
		if (PC)
		{
			FVector CamLoc;
			FRotator CamRot;
			PC->GetPlayerViewPoint(CamLoc, CamRot); 
            
            FVector CamRight = CamRot.Quaternion().GetRightVector();
            FVector InitialAimDirection = CamRot.Vector();
            FQuat PitchQuat = FQuat(CamRight, FMath::DegreesToRadians(CastAimPitchAdjustment));
            FVector AdjustedAimDirection = PitchQuat.RotateVector(InitialAimDirection);
            AdjustedAimDirection.Normalize(); 

			float TraceDistance = MaxLineLength + 10000.0f; 
			FVector TraceStart = CamLoc; 
			FVector TraceEnd = CamLoc + AdjustedAimDirection * TraceDistance;
			
			FHitResult HitResult;
			FCollisionQueryParams QueryParams;
			QueryParams.AddIgnoredActor(this); 
			QueryParams.AddIgnoredActor(CurrentOwnerCharacter);
			if(AttachedBobber) QueryParams.AddIgnoredActor(AttachedBobber); 
			
			FVector AimTargetPoint = TraceEnd; 
			if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
			{
				AimTargetPoint = HitResult.Location; 
			}
			
            LaunchDirection = (AimTargetPoint - CastOrigin).GetSafeNormal();
		} else { 
            LaunchDirection = LineAttachPointComponent->GetForwardVector(); 
            UE_LOG(LogFishingSystemRod, Warning, TEXT("%s ExecuteLaunch: No PlayerController found for aiming, using LineAttachPoint forward vector."), *GetName());
        }
	} else { 
        LaunchDirection = LineAttachPointComponent->GetForwardVector(); 
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s ExecuteLaunch: No OwningCharacter, using LineAttachPoint forward vector."), *GetName());
    }

	if (LaunchDirection.IsNearlyZero()) { 
        LaunchDirection = GetActorForwardVector(); 
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s ExecuteLaunch: LaunchDirection was zero, using ActorForwardVector."), *GetName());
    }
    
    DrawDebugLine(GetWorld(), CastOrigin, CastOrigin + LaunchDirection * 300.0f, FColor::Magenta, false, 5.0f, 0, 3.0f);
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s ExecuteLaunch: LaunchDir: %s, Origin: %s, Using DefaultLaunchImpulse: %.1f, PitchAdjust: %.1f deg"), 
        *GetName(), *LaunchDirection.ToString(), *CastOrigin.ToString(), DefaultLaunchImpulse, CastAimPitchAdjustment);

	DetachAndLaunchBobberLogic(LaunchDirection, DefaultLaunchImpulse);
	bIsPreparingToCast = false; 
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s ExecuteLaunchFromAnimation finished successfully."), *GetName());
}

void AFishingRod::CancelCastAttempt()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s CancelCastAttempt called."), *GetName());
	if (bIsPreparingToCast)
    {
        bIsPreparingToCast = false;
        UE_LOG(LogFishingSystemRod, Log, TEXT("%s Cast attempt cancelled. bIsPreparingToCast set to false."), *GetName());
    } else {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s CancelCastAttempt called, but not preparing to cast."), *GetName());
    }
}

void AFishingRod::FullReelIn()
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s FullReelIn called."), *GetName());
	if (!bIsEquipped || !AttachedBobber || !FishingLineComponent) {
		UE_LOG(LogFishingSystemRod, Error, TEXT("%s FullReelIn: Cannot reel in. Rod not equipped or bobber/line missing."), *GetName());
		return;
	}

	bIsActivelyReeling = false;
	bIsActivelyExtending = false;
    
	CurrentLineLengthSetting = MinLineLength; 
    FishingLineComponent->SetCableLength(CurrentLineLengthSetting);

	SetBobberToDangle(); 

	UE_LOG(LogFishingSystemRod, Log, TEXT("%s Bobber fully reeled in, CurrentLineLengthSetting set to Min (%.1f), set to Dangle state."), *GetName(), CurrentLineLengthSetting);
}

void AFishingRod::StartIncrementalReel()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel. Equipped: %d, Bobber: %p, LineCastOut: %d, Extending: %d, LineComp: %p"),
        *GetName(), bIsEquipped, AttachedBobber, bLineIsCastOut, bIsActivelyExtending, FishingLineComponent);

    if (!bIsEquipped || !AttachedBobber || !FishingLineComponent) {
        UE_LOG(LogFishingSystemRod, Error, TEXT("%s StartIncrementalReel: Cannot reel. Rod not equipped or bobber/line missing."), *GetName());
        return;
    }

    if (bIsActivelyExtending) {
        bIsActivelyExtending = false; 
        UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Was extending, stopping extend to start reel."), *GetName());
    }

    bIsActivelyReeling = true;
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Started incremental reel. bIsActivelyReeling is now TRUE."), *GetName());

    if (bLineIsCastOut && AttachedBobber->BobberMeshComponent) 
    {
        if (AttachedBobber->GetCurrentBobberState() == EBobberState::Flying) {
            AttachedBobber->SetBobberState(EBobberState::Idle); 
            UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Bobber was Flying, set to Idle."), *GetName());
        }
        UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Line simulation will pull bobber."), *GetName());
    }
    else if (!bLineIsCastOut && AttachedBobber->BobberMeshComponent) 
    {
        UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Reeling while bobber is dangling. Line sim handles pull."), *GetName());
    }
}

void AFishingRod::StopIncrementalReel()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s StopIncrementalReel called."), *GetName());
	if (bIsActivelyReeling)
	{
		bIsActivelyReeling = false;
		UE_LOG(LogFishingSystemRod, Log, TEXT("%s Stopped incremental reel."), *GetName());
        
        if (AttachedBobber && AttachedBobber->BobberMeshComponent) 
        {
            if (bLineIsCastOut) 
            {
                if (AttachedBobber->GetCurrentBobberState() != EBobberState::InWater) 
                {
                     AttachedBobber->SetBobberState(EBobberState::Idle); 
                }
                AttachedBobber->BobberMeshComponent->WakeRigidBody();
                UE_LOG(LogFishingSystemRod, Log, TEXT("%s StopIncrementalReel: Bobber physics state updated (line was cast)."), *GetName());
            }
            else 
            {
                if (!AttachedBobber->BobberMeshComponent->IsSimulatingPhysics())
                {
                    AttachedBobber->BobberMeshComponent->SetSimulatePhysics(true);
                    AttachedBobber->BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); 
                    AttachedBobber->SetBobberState(EBobberState::DanglingAtTip); 
                    AttachedBobber->BobberMeshComponent->WakeRigidBody();
                    UE_LOG(LogFishingSystemRod, Log, TEXT("%s StopIncrementalReel: Re-enabled bobber physics for dangling."), *GetName());
                }
            }
        }
	} else {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s StopIncrementalReel called, but not actively reeling."), *GetName());
    }
}

void AFishingRod::StartExtendingLine()
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartExtendingLine. Equipped: %d, Bobber: %p, Reeling: %d, LineComp: %p"),
		*GetName(), bIsEquipped, AttachedBobber, bIsActivelyReeling, FishingLineComponent);

	if (!bIsEquipped || !AttachedBobber || !FishingLineComponent) {
		UE_LOG(LogFishingSystemRod, Error, TEXT("%s StartExtendingLine: Cannot extend. Rod not equipped or bobber/line missing."), *GetName());
		return;
	}

	if (!bIsActivelyReeling) 
	{
		bIsActivelyExtending = true;
		UE_LOG(LogFishingSystemRod, Log, TEXT("%s Started extending line. bIsActivelyExtending is now TRUE."), *GetName());
	} else {
		UE_LOG(LogFishingSystemRod, Warning, TEXT("%s StartExtendingLine: Conditions not met (actively reeling: %d)."), *GetName(), bIsActivelyReeling);
	}
}

void AFishingRod::StopExtendingLine()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s StopExtendingLine called."), *GetName());
	if (bIsActivelyExtending)
	{
		bIsActivelyExtending = false;
		UE_LOG(LogFishingSystemRod, Log, TEXT("%s Stopped extending line."), *GetName());
	} else {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s StopExtendingLine called, but not actively extending."), *GetName());
    }
}

// --- INTERNAL LOGIC & HELPER FUNCTIONS ---

void AFishingRod::SpawnAndPrepareBobber()
{
    UE_LOG(LogFishingSystemSetup, Log, TEXT("--- %s: SpawnAndPrepareBobber START ---"), *GetName());

    if (AttachedBobber)
    {
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: AttachedBobber (%s) already exists. Destroying old one."), *GetName(), *AttachedBobber->GetName());
        if (FishingLineComponent)
        {
            UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: Detaching line from old bobber %s before destruction."), *GetName(), *AttachedBobber->GetName());
            FishingLineComponent->AttachCableEndTo(nullptr, NAME_None);
        }
        AttachedBobber->Destroy();
        AttachedBobber = nullptr;
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: Old bobber destroyed and nulled."), *GetName());
    }

    if (!BobberClass)
    {
        UE_LOG(LogFishingSystemSetup, Error, TEXT("%s: BobberClass is not set! Cannot spawn bobber. --- SpawnAndPrepareBobber END ---"), *GetName());
        return;
    }
    UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: BobberClass is %s."), *GetName(), *BobberClass->GetName());


    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogFishingSystemSetup, Error, TEXT("%s: GetWorld() returned null! --- SpawnAndPrepareBobber END ---"), *GetName());
        return;
    }
    UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: World is valid."), *GetName());

    if (!LineAttachPointComponent)
    {
        UE_LOG(LogFishingSystemSetup, Error, TEXT("%s: LineAttachPointComponent is null! --- SpawnAndPrepareBobber END ---"), *GetName());
        return;
    }
    UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: LineAttachPointComponent is valid."), *GetName());
	
    FVector SpawnLoc = LineAttachPointComponent->GetComponentLocation() - LineAttachPointComponent->GetUpVector() * MinLineLength * 0.5f;
    FRotator SpawnRot = LineAttachPointComponent->GetComponentRotation(); 
	
    UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: Spawning Bobber of class %s at Location: %s."), 
        *GetName(), *BobberClass->GetName(), *SpawnLoc.ToString());

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.Instigator = CurrentOwnerCharacter ? CurrentOwnerCharacter->GetInstigator() : GetInstigator();
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			
    AttachedBobber = World->SpawnActor<AFishingBobber>(BobberClass, SpawnLoc, SpawnRot, SpawnParams);
			
    if (AttachedBobber)
    {
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: Bobber %s SPAWNED SUCCESSFULLY."), *GetName(), *AttachedBobber->GetName());

        bool bFishingLineComponentValid = (FishingLineComponent != nullptr);
        bool bBobberRootComponentValid = (AttachedBobber->GetRootComponent() != nullptr);

        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: Checking components before attaching line to bobber: FishingLineComponent Valid: %s, BobberRootComponent Valid: %s"),
            *GetName(),
            bFishingLineComponentValid ? TEXT("YES") : TEXT("NO"),
            bBobberRootComponentValid ? TEXT("YES") : TEXT("NO")
        );

        if (bFishingLineComponentValid && bBobberRootComponentValid)
        {
            FishingLineComponent->AttachCableEndTo(AttachedBobber->GetRootComponent(), NAME_None); // Pass socket name if needed
            UE_LOG(LogFishingSystemSetup, Log, TEXT("%s: SUCCESS - Called AttachCableEndTo on FishingLineComponent with Bobber %s's RootComponent (%s)."),
                *GetName(),
                *AttachedBobber->GetName(),
                *AttachedBobber->GetRootComponent()->GetName()
            );
        }
        else
        {
            FString ReasonStr = TEXT("");
            if (!bFishingLineComponentValid) ReasonStr += TEXT("FishingLineComponent is NULL. ");
            if (!bBobberRootComponentValid) ReasonStr += TEXT("Bobber's RootComponent is NULL.");
            UE_LOG(LogFishingSystemSetup, Error, TEXT("%s: FAILURE - Could not attach FishingLineComponent to bobber %s. Reason: %s"),
                *GetName(), *AttachedBobber->GetName(), *ReasonStr);
        }
        SetBobberToDangle();
    }
    else 
    {
        UE_LOG(LogFishingSystemSetup, Error, TEXT("%s: FAILED TO SPAWN BOBBER of class %s!"), *GetName(), BobberClass ? *BobberClass->GetName() : TEXT("Unknown"));
    }
    UE_LOG(LogFishingSystemSetup, Log, TEXT("--- %s: SpawnAndPrepareBobber END ---"), *GetName());
}

void AFishingRod::SetBobberToDangle()
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s SetBobberToDangle called."), *GetName());
	if (!AttachedBobber || !LineAttachPointComponent || !FishingLineComponent)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("%s SetBobberToDangle: Critical component missing! Bobber:%p, LAP:%p, Line:%p"),
			*GetName(), AttachedBobber, LineAttachPointComponent, FishingLineComponent);
		return;
	}

	AttachedBobber->SetBobberState(EBobberState::DanglingAtTip); 
	AttachedBobber->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); 
    
	FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
	FVector DesiredBobberLocation = RodTipLocation - LineAttachPointComponent->GetUpVector() * FMath::Min(CurrentLineLengthSetting, MinLineLength);
	AttachedBobber->SetActorLocation(DesiredBobberLocation, false, nullptr, ETeleportType::TeleportPhysics); 
	UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Bobber %s teleported to initial dangle location: %s (Line length: %.1f)"), *GetName(), *AttachedBobber->GetName(), *DesiredBobberLocation.ToString(), CurrentLineLengthSetting);
    
	AttachedBobber->SetActorHiddenInGame(false); 

	bLineIsCastOut = false; 
	FishingLineComponent->SetCableLength(CurrentLineLengthSetting);

	if (AttachedBobber->GetRootComponent())
	{
		UE_LOG(LogFishingSystemSetup, Log, TEXT("%s SetBobberToDangle: Ensuring FishingLineComponent is attached to bobber %s's RootComponent."), *GetName(), *AttachedBobber->GetName());
		FishingLineComponent->AttachCableEndTo(AttachedBobber->GetRootComponent(), NAME_None);
	}
	else
	{
		UE_LOG(LogFishingSystemSetup, Warning, TEXT("%s SetBobberToDangle: AttachedBobber %s has no RootComponent! Cannot attach line."), *GetName(), *AttachedBobber->GetName());
	}

	UE_LOG(LogFishingSystemRod, Log, TEXT("%s Bobber %s set to Dangle. LineIsCastOut: False. CurrentLineLength: %.1f."), *GetName(), *AttachedBobber->GetName(), CurrentLineLengthSetting);
}


void AFishingRod::DetachAndLaunchBobberLogic(const FVector& LaunchDirection, float LaunchImpulseStrength)
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s DetachAndLaunchBobberLogic. Dir: %s, Impulse: %.2f"), *GetName(), *LaunchDirection.ToString(), LaunchImpulseStrength);
	if (!AttachedBobber || !LineAttachPointComponent || !FishingLineComponent)
	{
        UE_LOG(LogFishingSystemRod, Error, TEXT("%s DetachAndLaunchBobberLogic: Critical component missing! Bobber:%p, LAP:%p, Line:%p"),
            *GetName(), AttachedBobber, LineAttachPointComponent, FishingLineComponent);
		return;
	}

	AttachedBobber->SetActorHiddenInGame(false);
	AttachedBobber->LaunchAsPhysicsActor(LaunchDirection, LaunchImpulseStrength, this);
		
    bLineIsCastOut = true;
    CurrentLineLengthSetting = FVector::Dist(LineAttachPointComponent->GetComponentLocation(), AttachedBobber->GetActorLocation());
    CurrentLineLengthSetting = FMath::Clamp(CurrentLineLengthSetting, MinLineLength, MaxLineLength); 
    FishingLineComponent->SetCableLength(CurrentLineLengthSetting);

    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Bobber launched. LineIsCastOut: True. Initial CurrentLineLength: %.1f"), *GetName(), CurrentLineLengthSetting);
}
	
void AFishingRod::UpdateLineAndBobberWhenCast(float DeltaTime)
{
    if (!AttachedBobber || !LineAttachPointComponent || !FishingLineComponent) return;

    FishingLineComponent->SetCableLength(CurrentLineLengthSetting);

    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
    FVector BobberLocation = AttachedBobber->GetActorLocation();
    float ActualDistanceToBobber = FVector::Dist(RodTipLocation, BobberLocation);
    EBobberState CurrentBobberState = AttachedBobber->GetCurrentBobberState(); // Cache for multiple uses

    if (bIsActivelyReeling && CurrentLineLengthSetting > MinLineLength + KINDA_SMALL_NUMBER) 
    {
        UE_LOG(LogFishingSystemLine, Verbose, TEXT("%s UpdateLineCast (Reeling): Line will pull bobber to new length %.1f."), *GetName(), CurrentLineLengthSetting);
    }
    else if (CurrentBobberState == EBobberState::Flying) 
    {
        CurrentLineLengthSetting = FMath::Min(ActualDistanceToBobber, MaxLineLength);
        FishingLineComponent->SetCableLength(CurrentLineLengthSetting); // Update line length as it pays out
        if (ActualDistanceToBobber >= MaxLineLength) {
             UE_LOG(LogFishingSystemRod, Log, TEXT("%s UpdateLineCast (Flying): Bobber hit MaxLineLength (%.1f)."), *GetName(), MaxLineLength);
        }
    }
}

void AFishingRod::UpdateLineWhenDangling(float DeltaTime)
{
	if (!AttachedBobber || !LineAttachPointComponent || !FishingLineComponent || !AttachedBobber->BobberMeshComponent) {
        UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s UpdateLineWhenDangling: Missing components, returning."), *GetName());
        return;
    }

    if(AttachedBobber->GetCurrentBobberState() != EBobberState::DanglingAtTip) {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s UpdateLineWhenDangling: Bobber not in DanglingAtTip state (current: %s). Forcing to DanglingAtTip."), 
            *GetName(), *UEnum::GetValueAsString(AttachedBobber->GetCurrentBobberState()));
        SetBobberToDangle(); 
        return; 
    }
    
	FishingLineComponent->SetCableLength(CurrentLineLengthSetting);

    const TArray<FVector>& ParticleLocations = FishingLineComponent->GetParticleLocations();
    if (ParticleLocations.Num() > 0)
    {
        FVector LastParticleWorldPos = ParticleLocations.Last();
        AttachedBobber->SetActorLocation(LastParticleWorldPos, false, nullptr, ETeleportType::None); 

        if (ParticleLocations.Num() > 1)
        {
            FVector PenultimateParticlePos = ParticleLocations[ParticleLocations.Num() - 2];
            FVector LineEndDirection = (LastParticleWorldPos - PenultimateParticlePos).GetSafeNormal();
            if (!LineEndDirection.IsNearlyZero())
            {
                FRotator BobberTargetRot = UKismetMathLibrary::MakeRotFromZ(-LineEndDirection); 
                AttachedBobber->SetActorRotation(BobberTargetRot);
            }
        }
        else if (LineAttachPointComponent) 
        {
            FVector RodTipPos = LineAttachPointComponent->GetComponentLocation();
            FVector LineEndDirection = (LastParticleWorldPos - RodTipPos).GetSafeNormal();
            if (!LineEndDirection.IsNearlyZero())
            {
                FRotator BobberTargetRot = UKismetMathLibrary::MakeRotFromZ(-LineEndDirection); 
                AttachedBobber->SetActorRotation(BobberTargetRot);
            }
        }
        UE_LOG(LogFishingSystemRod, VeryVerbose, TEXT("%s UpdateLineWhenDangling: Moved Bobber to line's last particle at %s."), *GetName(), *LastParticleWorldPos.ToString());
    }
}

void AFishingRod::EnsureBobberStaysWithinLineLength()
{
    // This function is considered redundant with UFishingLineComponent
    // as its simulation should handle line tension and length constraints.
}

void AFishingRod::CalculateForceOnRodTip()
{
    if (!AttachedBobber || !LineAttachPointComponent || !AttachedBobber->BobberMeshComponent || !FishingLineComponent)
    {
        ForceOnRodTip = FVector::ZeroVector;
        return;
    }

    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
    FVector FirstParticleLocation;
    const TArray<FVector>& ParticleLocations = FishingLineComponent->GetParticleLocations();

    if (ParticleLocations.Num() > 1)
    {
        FirstParticleLocation = ParticleLocations[1]; 
    }
    else if (ParticleLocations.Num() == 1) // Only one particle (at tip), use bobber for direction
    {
        FirstParticleLocation = AttachedBobber->GetActorLocation();
         UE_LOG(LogFishingSystemRod, Verbose, TEXT("CalculateForceOnRodTip: Line has only 1 particle (at tip). Using bobber location for line direction."));
    }
    else // No particles, line not simulated properly
    {
        ForceOnRodTip = FVector::ZeroVector;
        UE_LOG(LogFishingSystemRod, Verbose, TEXT("CalculateForceOnRodTip: Line has no particles. Force set to zero."));
        return;
    }

    FVector LineDirectionFromTip = (FirstParticleLocation - RodTipLocation);
    if (LineDirectionFromTip.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        LineDirectionFromTip = (AttachedBobber->GetActorLocation() - RodTipLocation);
        if (LineDirectionFromTip.SizeSquared() < KINDA_SMALL_NUMBER)
        {
            LineDirectionFromTip = -LineAttachPointComponent->GetUpVector();
        }
    }
    LineDirectionFromTip.Normalize();

    EBobberState BobberState = AttachedBobber->GetCurrentBobberState();
    float BobberMass = AttachedBobber->BobberMeshComponent->GetMass(); 
    FVector GravityVector = FVector(0, 0, GetWorld()->GetGravityZ());
    FVector GravityForceOnBobber = GravityVector * BobberMass;
    float TensionMagnitude = 0.0f;

    if (!bLineIsCastOut) // Dangling
    {
        if (BobberState == EBobberState::DanglingAtTip)
        {
            TensionMagnitude = FMath::Abs(FVector::DotProduct(GravityForceOnBobber, LineDirectionFromTip));
            if (AttachedBobber->BobberMeshComponent->IsSimulatingPhysics())
            {
                FVector BobberVelocity = AttachedBobber->BobberMeshComponent->GetPhysicsLinearVelocity();
                float BobberSpeedSqr = BobberVelocity.SizeSquared();
                if (BobberSpeedSqr > 100.0f && CurrentLineLengthSetting > KINDA_SMALL_NUMBER)
                {
                    float DynamicForceApproximation = (BobberMass * BobberSpeedSqr) / FMath::Max(1.0f, CurrentLineLengthSetting * 0.01f);
                    TensionMagnitude += DynamicForceApproximation * 0.05f;
                }
            }
        }
    }
    else // Line is Cast Out
    {
        if (BobberState == EBobberState::Flying)
        {
            TensionMagnitude = FMath::Clamp(BobberMass * DefaultLaunchImpulse * 0.005f, 10.0f, 100.0f); 
        }
        else 
        {
            float ActualDistanceToBobber = FVector::Dist(RodTipLocation, AttachedBobber->GetActorLocation());
            if (ActualDistanceToBobber > CurrentLineLengthSetting + FishingLineComponent->DesiredSegmentLength * 0.5f)
            {
                float LineStiffnessApproximation = 100.0f + FishingLineComponent->StiffnessFactor * 50.0f;
                float DeltaDistance = ActualDistanceToBobber - CurrentLineLengthSetting;
                TensionMagnitude = LineStiffnessApproximation * FMath::Max(0.f, DeltaDistance * 0.01f);

                if (AttachedBobber->BobberMeshComponent->IsSimulatingPhysics()) {
                    FVector BobberVelocity = AttachedBobber->BobberMeshComponent->GetPhysicsLinearVelocity();
                    FVector DirTipToBobber = (AttachedBobber->GetActorLocation() - RodTipLocation).GetSafeNormal();
                    float VelocityAlongLine = FVector::DotProduct(BobberVelocity, DirTipToBobber);
                    TensionMagnitude += FishingLineComponent->DampingFactor * 20.0f * FMath::Abs(VelocityAlongLine);
                }
            }
            
            if (bIsActivelyReeling) {
                float ReelForceMagnitude = BobberMass * ReelInSpeed * 0.025f; 
                TensionMagnitude = FMath::Max(TensionMagnitude, ReelForceMagnitude + 50.0f);
            }

            if (BobberState == EBobberState::InWater || (BobberState == EBobberState::Idle && ActualDistanceToBobber > KINDA_SMALL_NUMBER)) {
                 TensionMagnitude = FMath::Max(TensionMagnitude, FMath::Abs(FVector::DotProduct(GravityForceOnBobber, LineDirectionFromTip)));
            }
        }
    }
    ForceOnRodTip = LineDirectionFromTip * TensionMagnitude;
}

void AFishingRod::DrawDebugForceOnRodTip() const
{
	if (!GetWorld() || !LineAttachPointComponent || !FishingLineComponent) return; // Added Line check

    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
    float ForceMagnitude = ForceOnRodTip.Size();

    if (ForceMagnitude < KINDA_SMALL_NUMBER) return; 

    FVector DebugLineEnd = RodTipLocation + ForceOnRodTip * 0.1f;
    float LineThickness = FMath::Clamp(ForceMagnitude * 0.1f / 20.0f, 1.0f, 8.0f); 

    DrawDebugLine(GetWorld(), RodTipLocation, DebugLineEnd, FColor::Red, false, 0.0f, 0, LineThickness);
}