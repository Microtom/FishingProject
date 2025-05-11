#include "FishingRod.h"
#include "FishingBobber.h" // Includes EBobberState
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "CableComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/ProjectileMovementComponent.h" // Though bobber handles it, rod might need to know about it
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

/**
 * @brief Constructor for AFishingRod.
 * Initializes components, default values, and logging.
 */
AFishingRod::AFishingRod()
{
	PrimaryActorTick.bCanEverTick = true;

	// Initialize Components
	RodRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RodRoot"));
	RootComponent = RodRootComponent;

	RodMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RodMesh"));
	RodMeshComponent->SetupAttachment(RodRootComponent);
    RodMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LineAttachPointComponent = CreateDefaultSubobject<USceneComponent>(TEXT("LineAttachPoint"));
	LineAttachPointComponent->SetupAttachment(RodMeshComponent); // Often attached to a socket on the rod mesh

	FishingLineComponent = CreateDefaultSubobject<UCableComponent>(TEXT("FishingLine"));
	FishingLineComponent->SetupAttachment(LineAttachPointComponent);
	FishingLineComponent->SetVisibility(false);
	FishingLineComponent->CableLength = 10.0f; // Initial small length
	FishingLineComponent->NumSegments = 15;
	FishingLineComponent->EndLocation = FVector(0,0,0); // Relative to attach point if bAttachEnd is false
	FishingLineComponent->bAttachEnd = true; // End will be attached to the bobber
    FishingLineComponent->SolverIterations = 10; 
    FishingLineComponent->SubstepTime = 0.01f;   
    FishingLineComponent->CableWidth = 2.0f;

	// Initialize Configuration Variables
	BobberClass = AFishingBobber::StaticClass(); // Default bobber class
	DefaultLaunchImpulse = 1000.0f; 
	ReelInSpeed = 250.0f;
	ExtendSpeed = 150.0f;
	MinLineLength = 75.0f;
	MaxLineLength = 5000.0f;

	// Initialize State Variables
	bIsEquipped = false;
	CurrentOwnerCharacter = nullptr;
	AttachedBobber = nullptr;
	bIsPreparingToCast = false;
	bLineIsCastOut = false;
	bIsActivelyReeling = false;
	bIsActivelyExtending = false;
	CurrentLineLengthSetting = MinLineLength;
	ForceOnRodTip = FVector::ZeroVector;

	UE_LOG(LogFishingSystemSetup, Log, TEXT("AFishingRod Constructor: Initialized."));
}

// --- UE4 LIFECYCLE ---

/**
 * @brief Called when the game starts or when spawned.
 * Initial setup, like ensuring the fishing line is hidden.
 */
void AFishingRod::BeginPlay()
{
	Super::BeginPlay();
	if (FishingLineComponent)
	{
		FishingLineComponent->SetVisibility(false);
	}
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s BeginPlay."), *GetName());
}

/**
 * @brief Called every frame.
 * Main update loop for the fishing rod, handling line updates, bobber interaction, and force calculations.
 * @param DeltaTime Game time elapsed during last frame.
 */
void AFishingRod::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ForceOnRodTip = FVector::ZeroVector; // Reset force each tick before recalculation

	// Early exit if not equipped or essential components are missing
	if (!bIsEquipped || !AttachedBobber || !LineAttachPointComponent || !FishingLineComponent)
	{
		if (FishingLineComponent && FishingLineComponent->IsVisible())
		{
			FishingLineComponent->SetVisibility(false);
		}
		return;
	}

	// Ensure line is visible and attached to the bobber
	FishingLineComponent->SetVisibility(true);
	FishingLineComponent->SetAttachEndTo(AttachedBobber, NAME_None, NAME_None); // Attach to bobber's root

	// --- START: Centralized Line Length Adjustment Logic ---
    if (bIsActivelyReeling)
    {
        // Stop extending if we start reeling (this check is also in StartIncrementalReel, defensive here)
        if (bIsActivelyExtending) {
            bIsActivelyExtending = false;
            UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s Tick: Was extending, but actively reeling. Stopping extend."), *GetName());
        }

        // Physics state and collision are now managed by Start/StopIncrementalReel.
        // The log "%s Tick (Reeling): Line cast, bobber physics OFF for direct control." is removed from here.

        float DistToPull = ReelInSpeed * DeltaTime;
        CurrentLineLengthSetting -= DistToPull;
        CurrentLineLengthSetting = FMath::Max(CurrentLineLengthSetting, MinLineLength);

        if (CurrentLineLengthSetting <= MinLineLength + KINDA_SMALL_NUMBER)
        {
            if (bLineIsCastOut) // Only transition to FullReelIn if the line was actually cast
            {
                UE_LOG(LogFishingSystemRod, Log, TEXT("%s Tick (Reeling): Reached MinLineLength (%.1f) while cast. Switching to FullReelIn (Dangle)."), *GetName(), MinLineLength);
                FullReelIn(); // This sets bLineIsCastOut = false, bIsActivelyReeling = false, and handles bobber state
            } else {
                 UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s Tick (Reeling): Reached MinLineLength (%.1f) while already dangling."), *GetName(), MinLineLength);
                 // If already dangling and reeling, and hit min length, effectively stop reeling action
                 if (bIsActivelyReeling) StopIncrementalReel(); // Explicitly stop reeling state
            }
        }
    }
    else if (bIsActivelyExtending)
    {
        // Can only extend if bobber is not in its uncontrolled flight phase OR if line is not cast (dangling)
        if (!bLineIsCastOut || (bLineIsCastOut && AttachedBobber->GetCurrentBobberState() != EBobberState::Flying) )
        {
            CurrentLineLengthSetting += ExtendSpeed * DeltaTime;
            CurrentLineLengthSetting = FMath::Min(CurrentLineLengthSetting, MaxLineLength);
            UE_LOG(LogFishingSystemLine, Verbose, TEXT("%s Extending line in Tick. New CurrentLineLengthSetting: %.1f. bLineIsCastOut: %d"), *GetName(), CurrentLineLengthSetting, bLineIsCastOut);
        }
        else if (bLineIsCastOut && AttachedBobber->GetCurrentBobberState() == EBobberState::Flying)
        {
             UE_LOG(LogFishingSystemLine, Verbose, TEXT("%s Tick: Tried to extend line, but bobber is flying. No manual extension, line pays out."), *GetName());
        }
    }
    // --- END: Centralized Line Length Adjustment Logic ---

	
	// Update line and bobber based on whether the line is cast out or dangling
	if (bLineIsCastOut)
	{
		UpdateLineAndBobberWhenCast(DeltaTime);
	}
	else 
	{
		UpdateLineWhenDangling(DeltaTime);
	}

	// Calculate and optionally draw debug forces on the rod tip
	CalculateForceOnRodTip();
	if (CVarDrawDebugFishingForces.GetValueOnGameThread() > 0)
	{
		DrawDebugForceOnRodTip();
	}

	// On-screen debug information
	if (GEngine && AttachedBobber && LineAttachPointComponent)
	{
		FString BobberPhysState = TEXT("N/A");
		if (AttachedBobber->BobberMeshComponent)
		{
			BobberPhysState = AttachedBobber->BobberMeshComponent->IsSimulatingPhysics() ? TEXT("ON") : TEXT("OFF");
		}

		FString DebugText = FString::Printf(TEXT("Target Line: %.0f\nActual BobberDist: %.0f\nCableComp Length: %.0f\nBobber State: %s (Phys: %s)\nLineIsCastOut: %s\nReeling: %s, Extending: %s\nForceOnRodTip: %s (Mag: %.2f)"),
			CurrentLineLengthSetting,
			FVector::Dist(LineAttachPointComponent->GetComponentLocation(), AttachedBobber->GetActorLocation()),
			FishingLineComponent->CableLength,
			AttachedBobber->GetCurrentBobberState() != EBobberState::Idle ? *UEnum::GetValueAsString(AttachedBobber->GetCurrentBobberState()) : TEXT("Idle/Unknown"), 
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

/**
 * @brief Equips the fishing rod to a character.
 * @param OwningCharacter The character equipping the rod.
 * @param SocketName The name of the socket on the character's mesh to attach the rod to.
 */
void AFishingRod::Equip(ACharacter* OwningCharacter, FName SocketName)
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Equip called by %s to socket %s."), *GetName(), OwningCharacter ? *OwningCharacter->GetName() : TEXT("NULL"), *SocketName.ToString());
	if (!OwningCharacter)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("%s OwningCharacter is null during Equip."), *GetName());
		return;
	}

	CurrentOwnerCharacter = OwningCharacter;
	bIsEquipped = true;
	SetOwner(OwningCharacter); // Set actor ownership

	// Attach rod to character's mesh
	if (USkeletalMeshComponent* CharMesh = OwningCharacter->GetMesh())
	{
		FAttachmentTransformRules AttachmentRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
		AttachToComponent(CharMesh, AttachmentRules, SocketName);
		UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Rod attached to %s's mesh at socket %s."), *GetName(), *OwningCharacter->GetName(), *SocketName.ToString());
	} else {
		UE_LOG(LogFishingSystemSetup, Warning, TEXT("%s OwningCharacter %s has no mesh to attach rod to."), *GetName(), *OwningCharacter->GetName());
	}

    // Adjust rod mesh collision if needed (e.g., to prevent blocking player camera)
    if (RodMeshComponent) {
        RodMeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        RodMeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        UE_LOG(LogFishingSystemSetup, Log, TEXT("%s RodMeshComponent set to ignore Pawn and Camera collision."), *GetName());
    }

	SpawnAndPrepareBobber(); 

	// Reset fishing state variables
	bLineIsCastOut = false; 
	bIsPreparingToCast = false;
    bIsActivelyReeling = false;
    bIsActivelyExtending = false;
	CurrentLineLengthSetting = MinLineLength; 

    if (FishingLineComponent) FishingLineComponent->SetVisibility(AttachedBobber != nullptr);
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Equip finished. LineIsCastOut: False, CurrentLineLength: %.1f"), *GetName(), CurrentLineLengthSetting);
}

/**
 * @brief Unequips the fishing rod from the character.
 * Detaches the rod, hides/destroys the bobber, and resets state.
 */
void AFishingRod::Unequip()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Unequip called."), *GetName());
	if (!bIsEquipped)
    {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s Unequip called but not equipped. No action taken."), *GetName());
        return;
    }

	// Handle the bobber
	if (AttachedBobber)
	{
        UE_LOG(LogFishingSystemBobber, Log, TEXT("%s Unequip: Setting bobber %s to Idle State and hiding."), *GetName(), *AttachedBobber->GetName());
		AttachedBobber->SetBobberState(EBobberState::Idle);
		AttachedBobber->SetActorHiddenInGame(true);
		// Detach bobber from line simulation; it might be destroyed or pooled
        // For now, let's destroy it. A pooling system could be implemented for performance.
        AttachedBobber->Destroy();
        AttachedBobber = nullptr;
	}
    
	// Hide fishing line and detach rod from character
	if (FishingLineComponent) FishingLineComponent->SetVisibility(false);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    // Reset state variables
    bIsEquipped = false;
	bIsPreparingToCast = false;
	bLineIsCastOut = false;
    bIsActivelyReeling = false;
    bIsActivelyExtending = false;
	CurrentOwnerCharacter = nullptr; // Clear owner reference
    SetOwner(nullptr);

    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Unequip finished. All states reset."), *GetName());
}

/**
 * @brief Initiates the casting process.
 * Sets a flag indicating the rod is preparing to cast.
 */
void AFishingRod::InitiateCastAttempt()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s InitiateCastAttempt called."), *GetName());
	if (!bIsEquipped || bIsPreparingToCast || bLineIsCastOut || !AttachedBobber)
	{
		UE_LOG(LogFishingSystemRod, Warning, TEXT("%s InitiateCastAttempt: Invalid state. Equipped: %d, Preparing: %d, CastOut: %d, Bobber: %p"),
            *GetName(), bIsEquipped, bIsPreparingToCast, bLineIsCastOut, AttachedBobber);
		return;
	}
	bIsPreparingToCast = true;
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s InitiateCastAttempt: bIsPreparingToCast set to true."), *GetName());
}

/**
 * @brief Executes the bobber launch, typically called from an animation notify.
 * Calculates launch direction and speed, then launches the bobber.
 */
void AFishingRod::ExecuteLaunchFromAnimation()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s ExecuteLaunchFromAnimation called."), *GetName());
	if (!bIsEquipped || !bIsPreparingToCast || !AttachedBobber || !LineAttachPointComponent)
	{
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s ExecuteLaunchFromAnimation: Invalid state or missing components. Equipped: %d, Preparing: %d, Bobber: %p, LineAttach: %p"),
            *GetName(), bIsEquipped, bIsPreparingToCast, AttachedBobber, LineAttachPointComponent);
		bIsPreparingToCast = false; // Reset flag if launch fails
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
            
            // Get camera's right vector to rotate around for pitch adjustment
            FVector CamRight = CamRot.Quaternion().GetRightVector();
            // Get camera's initial forward vector
            FVector InitialAimDirection = CamRot.Vector();

            // Apply the pitch adjustment
            // Create a quaternion for the pitch rotation around the camera's right axis
            FQuat PitchQuat = FQuat(CamRight, FMath::DegreesToRadians(CastAimPitchAdjustment));
            // Rotate the initial aim direction by this quaternion
            FVector AdjustedAimDirection = PitchQuat.RotateVector(InitialAimDirection);
            AdjustedAimDirection.Normalize(); // Ensure it's a unit vector

            // --- Original Trace Logic (using AdjustedAimDirection) ---
			float TraceDistance = MaxLineLength + 10000.0f; 
			FVector TraceStart = CamLoc; // Trace still originates from camera for visibility check
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
            // --- End of Original Trace Logic ---
			
            // Launch direction is from cast origin (rod tip) to the (potentially hit) aim target point
			LaunchDirection = (AimTargetPoint - CastOrigin).GetSafeNormal();

			// Optional: Ensure some minimum upward angle if desired, even after adjustment
            // This was the old logic, might not be needed if CastAimPitchAdjustment is sufficient
			// if (LaunchDirection.Z < 0.15f && CastAimPitchAdjustment >= 0) // Only apply if original adjustment wasn't significantly downwards
			// {
            //    LaunchDirection = (LaunchDirection + FVector(0,0,0.25f)).GetSafeNormal();
            //    UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s ExecuteLaunch: Applied additional Z safety uplift to LaunchDirection."), *GetName());
			// }
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

/**
 * @brief Cancels an ongoing cast attempt.
 * Resets the bIsPreparingToCast flag.
 */
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

/**
 * @brief Fully reels in the bobber, setting it to dangle at the rod tip.
 */
void AFishingRod::FullReelIn()
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s FullReelIn called."), *GetName());
	if (!bIsEquipped || !AttachedBobber) {
		UE_LOG(LogFishingSystemRod, Error, TEXT("%s FullReelIn: Cannot reel in. Rod not equipped or bobber missing."), *GetName());
		return;
	}

	bIsActivelyReeling = false;
	bIsActivelyExtending = false;
    
	CurrentLineLengthSetting = MinLineLength; // <<< EXPLICITLY SET TO MINIMUM HERE

	SetBobberToDangle(); // This will use the new CurrentLineLengthSetting (MinLineLength)

	UE_LOG(LogFishingSystemRod, Log, TEXT("%s Bobber fully reeled in, CurrentLineLengthSetting set to Min (%.1f), set to Dangle state."), *GetName(), CurrentLineLengthSetting);
}

/**
 * @brief Starts incrementally reeling in the fishing line.
 */
void AFishingRod::StartIncrementalReel()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel called. IsEquipped: %d, BobberValid: %p, LineIsCastOut: %d, IsActivelyExtending: %d"),
        *GetName(), bIsEquipped, AttachedBobber, bLineIsCastOut, bIsActivelyExtending);

    if (!bIsEquipped || !AttachedBobber) {
        UE_LOG(LogFishingSystemRod, Error, TEXT("%s StartIncrementalReel: Cannot reel. Rod not equipped or bobber missing."), *GetName());
        return;
    }

    if (bIsActivelyExtending) {
        bIsActivelyExtending = false; // Stop extending if we start reeling
        UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Was extending, stopping extend to start reel."), *GetName());
    }

    bIsActivelyReeling = true;
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Started incremental reel. bIsActivelyReeling is now TRUE."), *GetName());

    if (bLineIsCastOut && AttachedBobber->BobberMeshComponent) 
    {
        // If bobber was flying, stop its projectile motion by setting it to Idle.
        // Note: EnterIdleState() in Bobber might turn physics ON.
        if (AttachedBobber->GetCurrentBobberState() == EBobberState::Flying) {
            AttachedBobber->SetBobberState(EBobberState::Idle); 
            UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Bobber was Flying, set to Idle."), *GetName());
        }

        // Explicitly disable physics and collision for kinematic control during reeling,
        // regardless of the bobber's state transition above.
        if (AttachedBobber->BobberMeshComponent->IsSimulatingPhysics()) {
             AttachedBobber->BobberMeshComponent->SetSimulatePhysics(true);
             UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Disabled bobber physics for direct reeling control."), *GetName());
        }
        //AttachedBobber->BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Disabled bobber collision for reeling."), *GetName());
    }
    else if (!bLineIsCastOut && AttachedBobber->BobberMeshComponent) // Reeling while dangling
    {
        // When reeling a dangling bobber, we might want it to remain physics-active
        // but the spring force in UpdateLineWhenDangling will pull it in.
        // Or, if precise kinematic control is desired even for dangling reel-in:
        // AttachedBobber->BobberMeshComponent->SetSimulatePhysics(false);
        // AttachedBobber->BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        // For now, let dangling reel-in be physics-driven by UpdateLineWhenDangling's forces.
        UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartIncrementalReel: Reeling while bobber is dangling. Physics remains as per DanglingState."), *GetName());
    }
}

/**
 * @brief Stops incrementally reeling in the fishing line.
 */
void AFishingRod::StopIncrementalReel()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s StopIncrementalReel called."), *GetName());
	if (bIsActivelyReeling)
	{
		bIsActivelyReeling = false;
		UE_LOG(LogFishingSystemRod, Log, TEXT("%s Stopped incremental reel."), *GetName());
        
        if (AttachedBobber && AttachedBobber->BobberMeshComponent) 
        {
            if (bLineIsCastOut) // Only restore full physics if line was cast out
            {
                // Restore physics and collision for the bobber
                AttachedBobber->BobberMeshComponent->SetSimulatePhysics(true);
                AttachedBobber->BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                
                // Set to a physics-enabled state to allow physics to take over.
                // If it was InWater, it should ideally return to that. For now, Idle is a safe default.
                // A more sophisticated check (e.g. water check) could be done here to set EBobberState::InWater.
                if (AttachedBobber->GetCurrentBobberState() != EBobberState::InWater) 
                {
                     AttachedBobber->SetBobberState(EBobberState::Idle); // This will call EnterIdleState.
                }
                AttachedBobber->BobberMeshComponent->WakeRigidBody();
                UE_LOG(LogFishingSystemRod, Log, TEXT("%s StopIncrementalReel: Re-enabled bobber physics and collision (line was cast)."), *GetName());
            }
            else // Line was not cast out (i.e., was dangling)
            {
                // If reeling stopped while dangling, ensure it's in a proper dangling physics state.
                // SetBobberToDangle handles this if MinLineLength was reached.
                // If stopped before MinLineLength, just ensure it's physics active.
                if (!AttachedBobber->BobberMeshComponent->IsSimulatingPhysics())
                {
                    AttachedBobber->BobberMeshComponent->SetSimulatePhysics(true);
                    AttachedBobber->BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); // Standard collision for dangle
                    AttachedBobber->SetBobberState(EBobberState::DanglingAtTip); // Re-affirm dangle state
                    AttachedBobber->BobberMeshComponent->WakeRigidBody();
                    UE_LOG(LogFishingSystemRod, Log, TEXT("%s StopIncrementalReel: Re-enabled bobber physics for dangling."), *GetName());
                }
            }
        }
	} else {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s StopIncrementalReel called, but not actively reeling."), *GetName());
    }
}

/**
 * @brief Starts extending the fishing line (if applicable, e.g., letting out slack).
 */
void AFishingRod::StartExtendingLine()
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s StartExtendingLine called. IsEquipped: %d, BobberValid: %p, IsActivelyReeling: %d, bLineIsCastOut: %d"),
		*GetName(), bIsEquipped, AttachedBobber, bIsActivelyReeling, bLineIsCastOut);

	if (!bIsEquipped || !AttachedBobber) {
		UE_LOG(LogFishingSystemRod, Error, TEXT("%s StartExtendingLine: Cannot extend. Rod not equipped or bobber missing."), *GetName());
		return;
	}

	// if (bLineIsCastOut && !bIsActivelyReeling) // OLD Condition
	if (!bIsActivelyReeling) // NEW Condition: Can extend if not reeling, even if dangling
	{
		bIsActivelyExtending = true;
		UE_LOG(LogFishingSystemRod, Log, TEXT("%s Started extending line. bIsActivelyExtending is now TRUE."), *GetName());
	} else {
		UE_LOG(LogFishingSystemRod, Warning, TEXT("%s StartExtendingLine: Conditions not met (actively reeling: %d). bIsActivelyExtending remains FALSE."),
			*GetName(), bIsActivelyReeling);
	}
}

/**
 * @brief Stops extending the fishing line.
 */
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

/**
 * @brief Spawns a new fishing bobber and prepares it for use.
 * Called when the rod is equipped. If a bobber already exists, it's destroyed first.
 */
void AFishingRod::SpawnAndPrepareBobber()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s SpawnAndPrepareBobber called."), *GetName());
	if (AttachedBobber)
	{
		UE_LOG(LogFishingSystemSetup, Warning, TEXT("%s SpawnAndPrepareBobber: AttachedBobber already exists (%s). Destroying old one."), *GetName(), *AttachedBobber->GetName());
		AttachedBobber->Destroy();
		AttachedBobber = nullptr;
	}

	if (!BobberClass)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("%s SpawnAndPrepareBobber: BobberClass is not set! Cannot spawn bobber."), *GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("%s SpawnAndPrepareBobber: GetWorld() returned null!"), *GetName());
		return;
	}
    if (!LineAttachPointComponent)
    {
        UE_LOG(LogFishingSystemSetup, Error, TEXT("%s SpawnAndPrepareBobber: LineAttachPointComponent is null!"), *GetName());
        return;
    }
	
    // Spawn bobber slightly below the rod tip to give physics a good starting point for dangling
	FVector SpawnLoc = LineAttachPointComponent->GetComponentLocation() - LineAttachPointComponent->GetUpVector() * MinLineLength * 0.5f;
	FRotator SpawnRot = LineAttachPointComponent->GetComponentRotation(); // Or FRotator::ZeroRotator if orientation doesn't matter initially
	
	UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Spawning Bobber of class %s at Location: %s for initial dangle."), 
        *GetName(), *BobberClass->GetName(), *SpawnLoc.ToString());

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = CurrentOwnerCharacter ? CurrentOwnerCharacter->GetInstigator() : GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			
	AttachedBobber = World->SpawnActor<AFishingBobber>(BobberClass, SpawnLoc, SpawnRot, SpawnParams);
			
	if (AttachedBobber)
	{
		UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Bobber %s spawned successfully. Setting to Dangle state."), *GetName(), *AttachedBobber->GetName());
        SetBobberToDangle(); // Initialize bobber to dangle at the tip
	} else {
		UE_LOG(LogFishingSystemSetup, Error, TEXT("%s Failed to spawn Bobber! Check BobberClass and spawn parameters."), *GetName());
	}
}

/**
 * @brief Sets the attached bobber to the DanglingAtTip state and positions it accordingly.
 * Also resets line cast state and line length settings for the dangling configuration.
 */
void AFishingRod::SetBobberToDangle()
{
    UE_LOG(LogFishingSystemRod, Log, TEXT("%s SetBobberToDangle called."), *GetName());
	if (!AttachedBobber)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("%s SetBobberToDangle: AttachedBobber is null! Cannot set to dangle."), *GetName());
		return;
	}
    if (!LineAttachPointComponent)
    {
        UE_LOG(LogFishingSystemSetup, Error, TEXT("%s SetBobberToDangle: LineAttachPointComponent is null! Cannot position bobber."), *GetName());
        return;
    }

    // Set bobber state and ensure it's not attached to anything else directly
    AttachedBobber->SetBobberState(EBobberState::DanglingAtTip);
    AttachedBobber->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); // Ensure it's free for physics
    
    // Position the bobber at the initial dangle location to let physics take over smoothly
    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
	FVector DesiredBobberLocation = RodTipLocation - LineAttachPointComponent->GetUpVector() * CurrentLineLengthSetting;  
    AttachedBobber->SetActorLocation(DesiredBobberLocation, false, nullptr, ETeleportType::TeleportPhysics); // Teleport to start physics simulation correctly
    UE_LOG(LogFishingSystemSetup, Log, TEXT("%s Bobber %s teleported to initial dangle physics location: %s"), *GetName(), *AttachedBobber->GetName(), *DesiredBobberLocation.ToString());
    
    AttachedBobber->SetActorHiddenInGame(false); // Ensure bobber is visible

	// Update rod's state for dangling line
	bLineIsCastOut = false;
	if(FishingLineComponent) FishingLineComponent->CableLength = CurrentLineLengthSetting;

    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Bobber %s set to Dangle. LineIsCastOut: False. CurrentLineLength: %.1f."), *GetName(), *AttachedBobber->GetName(), CurrentLineLengthSetting);
}


void AFishingRod::DetachAndLaunchBobberLogic(const FVector& LaunchDirection, float LaunchImpulseStrength)
{
	UE_LOG(LogFishingSystemRod, Log, TEXT("%s DetachAndLaunchBobberLogic. Direction: %s, ImpulseStrength: %.2f"), *GetName(), *LaunchDirection.ToString(), LaunchImpulseStrength);
	if (!AttachedBobber)
	{
        UE_LOG(LogFishingSystemRod, Error, TEXT("%s DetachAndLaunchBobberLogic: AttachedBobber is null! Cannot launch."), *GetName());
		return;
	}
    if (!LineAttachPointComponent)
    {
        UE_LOG(LogFishingSystemRod, Error, TEXT("%s DetachAndLaunchBobberLogic: LineAttachPointComponent is null! Cannot determine initial line length."), *GetName());
        return;
    }

    // Ensure bobber is visible and launch it
	AttachedBobber->SetActorHiddenInGame(false);
	AttachedBobber->LaunchAsPhysicsActor(LaunchDirection, LaunchImpulseStrength, this);
		
    // Update rod's state for cast line
    bLineIsCastOut = true;
    // Initial line length is the distance from rod tip to bobber's current (just launched) position
    CurrentLineLengthSetting = FVector::Dist(LineAttachPointComponent->GetComponentLocation(), AttachedBobber->GetActorLocation());
    CurrentLineLengthSetting = FMath::Clamp(CurrentLineLengthSetting, MinLineLength, MaxLineLength); // Clamp within limits
    if(FishingLineComponent) FishingLineComponent->CableLength = CurrentLineLengthSetting;

    UE_LOG(LogFishingSystemRod, Log, TEXT("%s Bobber launched. LineIsCastOut: True. Initial CurrentLineLength: %.1f"), *GetName(), CurrentLineLengthSetting);
}
	
/**
 * @brief Updates the fishing line and bobber's position/state when the line is cast out.
 * Handles reeling, extending, line payout during bobber flight, and line tension simulation.
 * @param DeltaTime Game time elapsed during last frame.
 */
void AFishingRod::UpdateLineAndBobberWhenCast(float DeltaTime)
{
    if (!AttachedBobber || !LineAttachPointComponent || !FishingLineComponent) {
        // UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s UpdateLineAndBobberWhenCast: Missing components, returning."), *GetName());
        return;
    }

    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
    FVector BobberLocation = AttachedBobber->GetActorLocation();
    float ActualDistanceToBobber = FVector::Dist(RodTipLocation, BobberLocation);

    FishingLineComponent->CableLength = CurrentLineLengthSetting;

    bool bBobberIsFlying = AttachedBobber->GetCurrentBobberState() == EBobberState::Flying;

    if (bIsActivelyReeling && CurrentLineLengthSetting > MinLineLength + KINDA_SMALL_NUMBER) // If reeling and not at min length
    {
        // Move bobber towards rod tip along the line direction
        FVector DirToBobber = (BobberLocation - RodTipLocation).GetSafeNormal();
        if (DirToBobber.IsNearlyZero()) {
            DirToBobber = -LineAttachPointComponent->GetForwardVector();
        }
        FVector NewBobberLocation = RodTipLocation + DirToBobber * CurrentLineLengthSetting;
        AttachedBobber->SetActorLocation(NewBobberLocation); // Kinematic positioning
    }
    else if (bBobberIsFlying) // Not actively reeling, but bobber is flying
    {
        // Line pays out as bobber flies, CurrentLineLengthSetting tracks this up to MaxLineLength
        CurrentLineLengthSetting = FMath::Min(ActualDistanceToBobber, MaxLineLength);
        if (ActualDistanceToBobber >= MaxLineLength) {
             UE_LOG(LogFishingSystemRod, Log, TEXT("%s UpdateLineCast (Flying): Bobber hit MaxLineLength (%.1f)."), *GetName(), MaxLineLength);
             // Optionally: AttachedBobber->SetBobberState(EBobberState::Idle);
        }
    }
    // No "else if (bIsActivelyExtending)" here, as that's handled in Tick now.

    // After all adjustments, ensure bobber doesn't exceed CurrentLineLengthSetting if not flying and not being actively reeled kinematically
    if (!bBobberIsFlying && !bIsActivelyReeling) // If not flying and not being kinematically reeled
    {
        EnsureBobberStaysWithinLineLength(); // Applies physics forces or snaps
    }
}

/**
 * @brief Updates the fishing line behavior when the bobber is dangling at the rod tip.
 * Applies corrective spring-damper forces to the bobber to simulate it hanging from the line.
 * @param DeltaTime Game time elapsed during last frame.
 */
void AFishingRod::UpdateLineWhenDangling(float DeltaTime)
{
	if (!AttachedBobber || !LineAttachPointComponent || !FishingLineComponent || !AttachedBobber->BobberMeshComponent) {
        UE_LOG(LogFishingSystemRod, Verbose, TEXT("%s UpdateLineWhenDangling: Missing components, returning."), *GetName());
        return;
    }

    // Ensure bobber is in the correct state for dangling
    if(AttachedBobber->GetCurrentBobberState() != EBobberState::DanglingAtTip) {
        UE_LOG(LogFishingSystemRod, Warning, TEXT("%s UpdateLineWhenDangling: Bobber is not in DanglingAtTip state (current: %s). Forcing to DanglingAtTip."), 
            *GetName(), *UEnum::GetValueAsString(AttachedBobber->GetCurrentBobberState()));
        SetBobberToDangle(); // This will also call SetBobberState(DanglingAtTip)
    }
    // If SetBobberToDangle was called, it might have teleported the bobber, so physics forces might not be needed this frame.
    // However, the force application below is generally safe.

	// Set visual cable length to MinLineLength for dangling
	FishingLineComponent->CableLength = CurrentLineLengthSetting;

	FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
	FVector BobberLocation = AttachedBobber->GetActorLocation();
	float ActualDistanceToBobber = FVector::Dist(RodTipLocation, BobberLocation);

	float TargetDangleLength = CurrentLineLengthSetting; // The length we want the bobber to dangle at

	// Apply a spring-damper force to keep the bobber "attached"
	float MaxDangleDrift = TargetDangleLength * 0.5f; // Allow 50% drift from current target length
	if (ActualDistanceToBobber > TargetDangleLength + MaxDangleDrift || ActualDistanceToBobber < TargetDangleLength - MaxDangleDrift ||
        FMath::Abs(ActualDistanceToBobber - TargetDangleLength) > 1.0f) // Add a small absolute threshold too for responsiveness
	{
		FVector DirToBobberFromTip = (BobberLocation - RodTipLocation).GetSafeNormal();
		if(DirToBobberFromTip.IsNearlyZero()) {
            DirToBobberFromTip = -LineAttachPointComponent->GetUpVector();
        }

		float Stiffness = 15.0f;
		float DampingFactor = 5.0f;

		float DeltaDistance = ActualDistanceToBobber - TargetDangleLength; // How far off the target length
		FVector BobberVelocity = AttachedBobber->BobberMeshComponent->GetPhysicsLinearVelocity();
		float VelocityAlongLine = FVector::DotProduct(BobberVelocity, DirToBobberFromTip);

		FVector SpringForce = -DirToBobberFromTip * Stiffness * DeltaDistance;
		FVector DampingForce = -DirToBobberFromTip * DampingFactor * VelocityAlongLine;
		FVector CorrectiveForce = SpringForce + DampingForce;

        if (AttachedBobber->BobberMeshComponent->IsSimulatingPhysics()) {
		    AttachedBobber->BobberMeshComponent->AddForce(CorrectiveForce);
        } else {
            UE_LOG(LogFishingSystemLine, Warning, TEXT("%s UpdateLineWhenDangling: Bobber not simulating physics. Snapping instead."), *GetName());
            AttachedBobber->SetActorLocation(RodTipLocation + DirToBobberFromTip * TargetDangleLength);
        }
	}
    // If actively reeling while dangling, physics might fight it. Consider if BobberMeshComponent should SetSimulatePhysics(false)
    // when reeling, even if dangling, if you want precise kinematic control over its position relative to CurrentLineLengthSetting.
    // For now, the spring force will try to pull it in if reeling reduces CurrentLineLengthSetting.
    if (bIsActivelyReeling && AttachedBobber->BobberMeshComponent->IsSimulatingPhysics()) {
        // Optional: If you want more direct control when reeling a dangling bobber:
        // FVector DirToBobber = (BobberLocation - RodTipLocation).GetSafeNormal();
        // if (DirToBobber.IsNearlyZero()) DirToBobber = -LineAttachPointComponent->GetUpVector();
        // AttachedBobber->SetActorLocation(RodTipLocation + DirToBobber * CurrentLineLengthSetting);
    }
}

/**
 * @brief Ensures the bobber (if not flying) stays within the CurrentLineLengthSetting from the rod tip.
 * If the bobber is too far, it applies a corrective spring-damper force (if physics is on) or snaps it.
 * This is primarily for when the line is cast out and taut.
 */
void AFishingRod::EnsureBobberStaysWithinLineLength()
{
    if (!AttachedBobber || !LineAttachPointComponent || AttachedBobber->GetCurrentBobberState() == EBobberState::Flying) {
        // Don't interfere if bobber is flying (line pays out) or components are missing
        return;
    }

    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
    FVector BobberLocation = AttachedBobber->GetActorLocation();
    float ActualDistanceToBobber = FVector::Dist(RodTipLocation, BobberLocation);

    // If bobber is further than the current line setting (plus a small tolerance)
    float Tolerance = 0.5f; // Small tolerance to prevent fighting tiny differences
    if (ActualDistanceToBobber > CurrentLineLengthSetting + Tolerance) 
    {
        FVector DirFromTipToBobber = (BobberLocation - RodTipLocation).GetSafeNormal();
        if (DirFromTipToBobber.IsNearlyZero()) {
            // Safety: If bobber is at the tip, no correction needed, or use a default direction
            return; 
        }
        
        FVector TargetLocation = RodTipLocation + DirFromTipToBobber * CurrentLineLengthSetting;

        // If bobber is simulating physics (e.g., in water and line is taut)
        if (AttachedBobber->BobberMeshComponent && AttachedBobber->BobberMeshComponent->IsSimulatingPhysics())
        {
            // Stronger spring-damper to simulate a taut line pulling the bobber
            float Stiffness = 500.0f; 
            float DampingFactor = 25.0f;  

            float DeltaDistance = ActualDistanceToBobber - CurrentLineLengthSetting;
            FVector BobberVelocity = AttachedBobber->BobberMeshComponent->GetPhysicsLinearVelocity();
            float VelocityAlongLine = FVector::DotProduct(BobberVelocity, DirFromTipToBobber);
            
            // Force applied TO BOBBER is towards the rod tip (negative direction of DirFromTipToBobber)
            FVector SpringForce = -DirFromTipToBobber * Stiffness * DeltaDistance; 
            FVector DampingForce = -DirFromTipToBobber * DampingFactor * VelocityAlongLine;
            FVector CorrectiveForce = SpringForce + DampingForce;
            
            AttachedBobber->BobberMeshComponent->AddForce(CorrectiveForce, NAME_None, true /* accelChange */);
            // UE_LOG(LogFishingSystemLine, Verbose, TEXT("%s Line Tension (Physics): Dist(%.1f) > Set(%.1f). Force On Bobber %s."), 
            //     *GetName(), ActualDistanceToBobber, CurrentLineLengthSetting, *CorrectiveForce.ToString());
        }
        // If bobber is not simulating physics (e.g., being kinematically reeled or idle on land)
        else 
        {
            AttachedBobber->SetActorLocation(TargetLocation);
            // UE_LOG(LogFishingSystemLine, Verbose, TEXT("%s Line Tension (Snap): Dist(%.1f) > Set(%.1f). Snapped to %s."), 
            //     *GetName(), ActualDistanceToBobber, CurrentLineLengthSetting, *TargetLocation.ToString());
        }
    }
}

/**
 * @brief Calculates the force exerted on the rod tip by the fishing line and bobber.
 * This force is an approximation based on the bobber's state, mass, and line tension.
 * It can be used for visual feedback like rod bending.
 */
void AFishingRod::CalculateForceOnRodTip()
{
    // --- TOP LEVEL DIAGNOSTIC ---
    // UE_LOG(LogFishingSystemRod, Warning, TEXT("--- CalculateForceOnRodTip START --- bLineIsCastOut: %s, BobberState: %s"),
    //     bLineIsCastOut ? TEXT("TRUE") : TEXT("FALSE"),
    //     AttachedBobber ? *UEnum::GetValueAsString(AttachedBobber->GetCurrentBobberState()) : TEXT("NO BOBBER"));

    if (!AttachedBobber || !LineAttachPointComponent || !AttachedBobber->BobberMeshComponent)
    {
        ForceOnRodTip = FVector::ZeroVector;
        // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForceOnRodTip: Early exit - missing components. Force set to Zero."));
        return;
    }

    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
    FVector BobberLocation = AttachedBobber->GetActorLocation();
    FVector LineDirectionFromTipToBobber = (BobberLocation - RodTipLocation); 
    float ActualDistanceToBobber = LineDirectionFromTipToBobber.Size();

    // Normalize line direction, with a fallback if bobber is at the tip
    if (ActualDistanceToBobber < KINDA_SMALL_NUMBER) 
    {
        LineDirectionFromTipToBobber = -LineAttachPointComponent->GetUpVector(); // Default if bobber is at tip (e.g. dangling straight down)
        // UE_LOG(LogFishingSystemRod, Verbose, TEXT("CalculateForce: Bobber at tip, LineDir set to -RodUp."));
    }
    else
    {
        LineDirectionFromTipToBobber.Normalize();
    }

    EBobberState BobberState = AttachedBobber->GetCurrentBobberState();
    float BobberMass = AttachedBobber->BobberMeshComponent->GetMass(); // Actual mass being simulated
    FVector GravityVector = FVector(0, 0, GetWorld()->GetGravityZ());
    FVector GravityForceOnBobber = GravityVector * BobberMass;

    ForceOnRodTip = FVector::ZeroVector; // Initialize force

    // --- Force calculation based on whether line is cast out or dangling ---
    if (!bLineIsCastOut) // Dangling state
    {
        // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForce: Path taken: !bLineIsCastOut (Dangling Path)"));
        if (BobberState == EBobberState::DanglingAtTip)
        {
            // Force on rod tip is primarily due to bobber's weight, directed along the line.
            // The line tension counteracts gravity components and dynamic forces on the bobber.
            // Force on rod tip = - (Force applied by line on bobber)
            // For a simple dangle, assume line tension primarily supports weight.
            float BaseTensionMag = GravityForceOnBobber.Size() * FMath::Abs(FVector::DotProduct(LineDirectionFromTipToBobber, FVector(0,0,-1))); // Component of gravity along line
            
            // Add a dynamic component based on bobber's swing (centripetal-like force)
            if (AttachedBobber->BobberMeshComponent->IsSimulatingPhysics())
            {
                FVector BobberVelocity = AttachedBobber->BobberMeshComponent->GetPhysicsLinearVelocity();
                float BobberSpeed = BobberVelocity.Size();
                if (BobberSpeed > 10.0f && MinLineLength > KINDA_SMALL_NUMBER)
                {
                    // Simplified centripetal force approximation if swinging
                    float CentripetalApproximation = (BobberMass * BobberSpeed * BobberSpeed) / FMath::Max(MinLineLength, 1.0f);
                    BaseTensionMag += CentripetalApproximation * 0.1f; // Add a scaled portion
                }
            }
            ForceOnRodTip = LineDirectionFromTipToBobber * BaseTensionMag;
            // UE_LOG(LogFishingSystemRod, Verbose, TEXT("CalculateForce (Dangling): BaseTension: %.2f, LineDir: %s, Force: %s"),
            //     BaseTensionMag, *LineDirectionFromTipToBobber.ToString(), *ForceOnRodTip.ToString());
        }
        else
        {
            //  UE_LOG(LogFishingSystemRod, Verbose, TEXT("CalculateForce (Not Cast Out, Not Dangling State): BobberState: %s. Force remains Zero."),
            //     *UEnum::GetValueAsString(BobberState));
        }
    }
    else // bLineIsCastOut is TRUE
    {
        // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForce: Path taken: bLineIsCastOut (Cast Out Path)"));
        float TensionMagnitude = 0.0f;

        if (BobberState == EBobberState::Flying)
        {
            // During flight, force is mainly due to drag of line and bobber pulling back.
            // This is a simplified approximation.
            float FlyingPullMagnitude = FMath::Clamp(BobberMass * DefaultLaunchImpulse * 0.005f, 10.0f, 50.0f); // Scaled by launch speed and mass
            TensionMagnitude = FlyingPullMagnitude;
            // UE_LOG(LogFishingSystemRod, Verbose, TEXT("CalculateForce (Flying): PullMag: %.2f"), FlyingPullMagnitude);
        }
        else // Not flying, but line is cast (e.g., in water, being reeled, idle on ground)
        {
            // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForce (Cast Out, Not Flying): ActualDist: %.1f, CurrentLineLen: %.1f"), ActualDistanceToBobber, CurrentLineLengthSetting);
            
            // Tension due to line stretch (if bobber is further than line setting)
            if (ActualDistanceToBobber > CurrentLineLengthSetting + 0.1f) 
            {
                float Stiffness = 500.0f; // Stiffness of the line when taut
                float DeltaDistance = ActualDistanceToBobber - CurrentLineLengthSetting;
                TensionMagnitude = Stiffness * DeltaDistance;

                // Add damping if bobber is physics-simulated and moving along the line
                if (AttachedBobber->BobberMeshComponent->IsSimulatingPhysics()) {
                    float DampingFactor = 25.0f;
                    FVector BobberVelocity = AttachedBobber->BobberMeshComponent->GetPhysicsLinearVelocity();
                    float VelocityAlongLine = FVector::DotProduct(BobberVelocity, LineDirectionFromTipToBobber); // Positive if moving away from tip
                    // Damping opposes motion, so if VelocityAlongLine is positive (moving away), damping force is towards tip.
                    // We are calculating tension magnitude, so take absolute contribution.
                    TensionMagnitude += DampingFactor * FMath::Abs(VelocityAlongLine); 
                }
                // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForce (Cast Out, TAUT LINE): Tension from stretch/damp: %.2f"), TensionMagnitude);
            } else {
                // Line is slack or at desired length, minimal tension from stretch
                // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForce (Cast Out, SLACK LINE): ActualDist <= CurrentLineLen. No stretch tension calculation here."), TensionMagnitude);
            }
            
            // Add base tension if reeling
            if (bIsActivelyReeling) {
                // Force due to accelerating the bobber's mass during reeling
                float ReelForceMagnitude = BobberMass * ReelInSpeed * 0.025f; // Small multiplier, ReelInSpeed is velocity
                TensionMagnitude = FMath::Max(TensionMagnitude, ReelForceMagnitude); // Ensure reeling adds some base tension
                // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForce (Cast Out, Reeling): Base reeling tension considered. TensionMag now: %.2f"), TensionMagnitude);
            }

            // If bobber is in water and stationary/drifting, consider buoyancy and drag if detailed.
            // For now, if it's just sitting in water, gravity is the main static force component.
            // This part can be expanded if EBobberState::InWater implies specific forces.
            if (BobberState == EBobberState::InWater || (BobberState == EBobberState::Idle && ActualDistanceToBobber > KINDA_SMALL_NUMBER)) {
                 // Add component of bobber's weight if line is somewhat taut and supporting it
                TensionMagnitude = FMath::Max(TensionMagnitude, GravityForceOnBobber.Size() * FMath::Abs(FVector::DotProduct(LineDirectionFromTipToBobber, FVector(0,0,-1))));
            }
        }
        ForceOnRodTip = LineDirectionFromTipToBobber * TensionMagnitude;
        // UE_LOG(LogFishingSystemRod, Warning, TEXT("CalculateForce (Cast Out, Not Flying): FinalTensionMag: %.2f, Force: %s"),
        //     TensionMagnitude, *ForceOnRodTip.ToString());
    }
    // UE_LOG(LogFishingSystemRod, Warning, TEXT("--- CalculateForceOnRodTip END --- Final Force Mag: %.2f"), ForceOnRodTip.Size());
}

/**
 * @brief Draws a debug line representing the calculated force on the rod tip.
 * The line points in the direction the rod tip is being pulled.
 * Controlled by the CVar `r.Fishing.DrawDebugForces`.
 */
void AFishingRod::DrawDebugForceOnRodTip() const
{
	if (!GetWorld() || !LineAttachPointComponent)
    {
        return; // Cannot draw without world or attach point
    }

    FVector RodTipLocation = LineAttachPointComponent->GetComponentLocation();
    float ForceMagnitude = ForceOnRodTip.Size();

    if (ForceMagnitude < KINDA_SMALL_NUMBER) 
    {
        return; // Don't draw if force is negligible
    }

    // The debug line should represent the force vector itself.
    // ForceOnRodTip is the force *on* the rod tip, so the line starts at the tip and extends along this force vector.
    FVector DebugLineEnd = RodTipLocation + ForceOnRodTip; // End point is tip + force vector scaled by some factor for visibility

    // Scale the visual length of the debug line for better visibility
    // The ForceOnRodTip vector already has magnitude, so we can scale it directly for visualization.
    float VisualScaleFactor = 0.1f; // Adjust this to make forces more/less visible. (e.g. 1 unit of force = 0.1 UU line length)
                                    // If ForceOnRodTip units are already small (e.g. <100), this might need to be 1.0 or higher.
                                    // Let's make it dependent on typical force magnitudes. If forces are ~500N, 0.1 is 50UU.

    DebugLineEnd = RodTipLocation + ForceOnRodTip * VisualScaleFactor;

    // Make the line thicker if the force is larger
    float LineThickness = FMath::Clamp(ForceMagnitude * VisualScaleFactor / 20.0f, 1.0f, 8.0f); // Scale thickness based on visual length

    DrawDebugLine(
        GetWorld(),
        RodTipLocation,     // Start of the line
        DebugLineEnd,       // End of the line (direction and magnitude of force)
        FColor::Red,        // Color of the debug line
        false,              // Persistent lines (false = redraw each frame)
        0.0f,               // Lifetime (0 = one frame)
        0,                  // Depth Priority
        LineThickness       // Thickness of the line
    );

    // UE_LOG(LogFishingSystemRod, VeryVerbose, TEXT("DrawDebugForce: TipLoc:%s, EndLoc:%s, ForceMag:%.2f, VisualLength:%.2f, Thick:%.1f"),
    //     *RodTipLocation.ToString(), *DebugLineEnd.ToString(), ForceMagnitude, (DebugLineEnd - RodTipLocation).Size(), LineThickness);
}