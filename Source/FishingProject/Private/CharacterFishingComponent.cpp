#include "CharacterFishingComponent.h"
#include "FishingRod.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h" // For input
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "FishingLogChannels.h" // Your custom log channels
#include "Animation/AnimNotify_ExecuteFishingLaunch.h"
#include "Misc/UObjectToken.h"

// --- CONSTRUCTOR ---
UCharacterFishingComponent::UCharacterFishingComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // This component doesn't need to tick itself; rod does.

	DefaultFishingRodClass = AFishingRod::StaticClass(); // Default to base AFishingRod
	DefaultHandSocketName = FName("hand_r_socket"); // Example socket name
	OwnerCharacter = nullptr;
	EquippedFishingRod = nullptr;

#if WITH_EDITORONLY_DATA
	EditorPreviewRod = nullptr;
#endif

	bInputBindingsInitialized = false; // Initialize the flag
}

// --- UE4 LIFECYCLE ---
void UCharacterFishingComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("FishingComponent is not owned by an ACharacter! This component requires an ACharacter owner."));
	}

#if WITH_EDITOR
	// Destroy any editor preview actor when the game starts
	DestroyEditorPreviewRod();
#endif

	// Attempt to self-initialize input bindings if not already done
	if (!bInputBindingsInitialized)
	{
		TryAutoSetupPlayerInputBindings();
	}
}

void UCharacterFishingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Ensure rod is unequipped and cleaned up if the component is destroyed
    if (EquippedFishingRod)
    {
        UE_LOG(LogFishingSystemComponent, Log, TEXT("FishingComponent EndPlay: Unequipping rod %s."), *EquippedFishingRod->GetName());
        UnequipRod(); // This will call the rod's unequip
    }
    Super::EndPlay(EndPlayReason);

#if WITH_EDITOR
	// Also ensure editor preview is cleaned up if it somehow persisted
	DestroyEditorPreviewRod();
#endif
}

#if WITH_EDITOR
void UCharacterFishingComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
}

void UCharacterFishingComponent::OnRegister()
{
	Super::OnRegister();
}

void UCharacterFishingComponent::OnUnregister()
{
	DestroyEditorPreviewRod();
	Super::OnUnregister();
}

void UCharacterFishingComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Check if the game is running. We only want to do this in the editor.
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		return;
	}

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
    
	// UE_LOG(LogFishingSystemEditor, Log, TEXT("FishingComponent PostEditChangeProperty: Prop=%s, MemberProp=%s"), *PropertyName.ToString(), *MemberPropertyName.ToString());

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCharacterFishingComponent, DefaultFishingRodClass) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UCharacterFishingComponent, DefaultHandSocketName))
	{
		UE_LOG(LogFishingSystemComponent, Log, TEXT("FishingComponent: DefaultFishingRodClass or DefaultHandSocketName changed. Updating preview."), *GetName());
		UpdateEditorPreviewRod();
	}
	// Also update if the component is added to an actor or the actor is moved
	// This is often handled by OnRegister or if the owning actor's transform changes,
	// but a direct call here can be a fallback.
	// This might be too aggressive if not needed.
	// else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Transform)
	// {
	//    UpdateEditorPreviewRod();
	// }
}

void UCharacterFishingComponent::DestroyEditorPreviewRod()
{
	if (EditorPreviewRod)
	{
		UE_LOG(LogFishingSystemComponent, Log, TEXT("FishingComponent: Destroying previous editor preview rod: %s"), *EditorPreviewRod->GetName());
		// Detach from parent first if attached
		if (EditorPreviewRod->GetAttachParentActor())
		{
			EditorPreviewRod->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		// Use GEditor->DestroyActor if available and safe, otherwise World->DestroyActor
		// World->DestroyActor is generally safer for transient actors not fully part of the scene.
		UWorld* World = GetWorld();
		if (World && EditorPreviewRod->IsValidLowLevel()) { // Check IsValidLowLevel before destroying
			World->DestroyActor(EditorPreviewRod);
		}
		EditorPreviewRod = nullptr;
	}
}

void UCharacterFishingComponent::TryAutoSetupPlayerInputBindings()
{
	if (bInputBindingsInitialized) // Should not happen if called from BeginPlay guard, but good for robustness
    {
        return;
    }

    if (!OwnerCharacter)
    {
        UE_LOG(LogFishingSystemSetup, Warning, TEXT("FishingComponent: Cannot auto-setup input bindings, OwnerCharacter is null. Component: %s"), *GetName());
        return;
    }

    APlayerController* PC = Cast<APlayerController>(OwnerCharacter->GetController());
    if (!PC)
    {
        // This is the most common point of failure if BeginPlay is too early or character is not possessed by a PlayerController.
        UE_LOG(LogFishingSystemSetup, Warning,
            TEXT("FishingComponent %s on %s: Could not get PlayerController from OwnerCharacter in TryAutoSetupPlayerInputBindings. "
                 "Input bindings will NOT be set up by the component automatically. "
                 "This can happen if the component's BeginPlay runs before the Character is possessed by a PlayerController, "
                 "or if the owner is controlled by an AIController."),
            *GetName(), *OwnerCharacter->GetName());
        return;
    }

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PC->InputComponent);
    if (!EIC)
    {
        // This can occur if the PlayerController hasn't initialized its InputComponent as an EnhancedInputComponent yet.
        // Ensure your Project Settings -> Engine -> Input -> DefaultClasses are set for Enhanced Input if this persists.
        // Example: DefaultPlayerInputClass=EnhancedPlayerInput, DefaultInputComponentClass=EnhancedInputComponent
        UE_LOG(LogFishingSystemSetup, Warning,
            TEXT("FishingComponent %s on %s: Could not get or cast to EnhancedInputComponent from PlayerController %s. "
                 "Input bindings will NOT be set up. Ensure PlayerController's InputComponent is a UEnhancedInputComponent."),
            *GetName(), *OwnerCharacter->GetName(), *PC->GetName());
        return;
    }

    // Now call your existing SetupPlayerInputBindings function
    UE_LOG(LogFishingSystemSetup, Log, TEXT("FishingComponent %s on %s: Attempting to auto-setup input bindings with PC: %s and EIC: %s."),
        *GetName(), *OwnerCharacter->GetName(), *PC->GetName(), *EIC->GetName());

    SetupPlayerInputBindings(EIC, PC); // This function already contains logging for success/failure of binding

    // Check if the setup was successful (e.g., by seeing if RegisteredPlayerController was set by SetupPlayerInputBindings)
    if (RegisteredPlayerController == PC && FishingInputMappingContext != nullptr)
    {
        bInputBindingsInitialized = true;
        UE_LOG(LogFishingSystemSetup, Log, TEXT("FishingComponent %s: Auto-setup of input bindings appears successful."), *GetName());
    }
    else
    {
        UE_LOG(LogFishingSystemSetup, Warning, TEXT("FishingComponent %s: Auto-setup of input bindings may have failed or an IMC is missing. Check previous logs from SetupPlayerInputBindings."), *GetName());
    }
}



void UCharacterFishingComponent::UpdateEditorPreviewRod()
{
	// Only run in the editor and not during gameplay (PIE)
    if (!GIsEditor || (GetWorld() && GetWorld()->IsGameWorld()))
    {
        DestroyEditorPreviewRod(); // Clean up if somehow called at runtime
        return;
    }

    AActor* OwningActor = GetOwner();
    if (!OwningActor)
    {
        UE_LOG(LogFishingSystemComponent, Warning, TEXT("FishingComponent: Cannot update preview rod, OwningActor is null."));
        DestroyEditorPreviewRod();
        return;
    }
    
    OwnerCharacter = Cast<ACharacter>(OwningActor); // Update OwnerCharacter ref for editor context
     if (!OwnerCharacter)
    {
        UE_LOG(LogFishingSystemComponent, Warning, TEXT("FishingComponent: Cannot update preview rod, Owner is not an ACharacter."));
        DestroyEditorPreviewRod();
        return;
    }


    USkeletalMeshComponent* OwnerMesh = OwnerCharacter->GetMesh();
    if (!OwnerMesh)
    {
        UE_LOG(LogFishingSystemComponent, Warning, TEXT("FishingComponent: Cannot update preview rod, OwnerCharacter has no mesh."));
        DestroyEditorPreviewRod();
        return;
    }

    // Destroy any existing preview rod first
    DestroyEditorPreviewRod();

    if (!DefaultFishingRodClass)
    {
        UE_LOG(LogFishingSystemComponent, Verbose, TEXT("FishingComponent: No DefaultFishingRodClass selected, no preview to show."));
        return; // No class, no preview
    }
    
    UWorld* World = OwningActor->GetWorld();
    if (!World) {
        UE_LOG(LogFishingSystemComponent, Error, TEXT("FishingComponent: World is null, cannot spawn preview rod."));
        return;
    }

    // Spawn the new preview rod
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Owner = OwningActor;
    SpawnParams.Instigator = OwnerCharacter;
    SpawnParams.bHideFromSceneOutliner = true; // Keep it clean in the outliner
    SpawnParams.ObjectFlags |= RF_Transient | RF_TextExportTransient | RF_DuplicateTransient; // Editor-only, not saved, not duplicated
#if WITH_EDITOR // This whole block is already within WITH_EDITOR, but good to be explicit for these flags
	SpawnParams.bHideFromSceneOutliner = true; // Keep it clean in the outliner
	// Test without it first, as RF_Transient is generally the key.
#endif


    // Spawn the rod slightly offset initially to avoid issues, then attach
    // Or spawn directly at attach point if attachment is robust
    FTransform SpawnTransform = OwnerMesh->GetSocketTransform(DefaultHandSocketName, RTS_World);

    EditorPreviewRod = World->SpawnActor<AFishingRod>(DefaultFishingRodClass, SpawnTransform, SpawnParams);

    if (EditorPreviewRod)
    {
        UE_LOG(LogFishingSystemComponent, Log, TEXT("FishingComponent: Spawned editor preview rod: %s of class %s"), *EditorPreviewRod->GetName(), *DefaultFishingRodClass->GetName());
        EditorPreviewRod->SetActorLabel(FString::Printf(TEXT("%s_PreviewRod"), *OwningActor->GetName()));
        EditorPreviewRod->SetIsTemporarilyHiddenInEditor(false); // Ensure it's visible

        // Call the rod's OnConstruction manually if needed, though SpawnActor should do it.
        // EditorPreviewRod->OnConstruction(EditorPreviewRod->GetTransform());

        // Attach to the character's mesh socket
        FAttachmentTransformRules AttachmentRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
        EditorPreviewRod->AttachToComponent(OwnerMesh, AttachmentRules, DefaultHandSocketName);

        // Crucially, tell the editor that this actor is part of a construction script modification
        // This helps with undo/redo and editor updates.
        // OwningActor->MarkPackageDirty(); // Might be too broad
        // EditorPreviewRod->RerunConstructionScripts(); // May not be needed if OnConstruction in Rod handles it
        // GEditor->SelectActor(OwningActor, true, true); // Reselect to force update (can be disruptive)

    }
    else
    {
        UE_LOG(LogFishingSystemComponent, Error, TEXT("FishingComponent: Failed to spawn editor preview rod of class %s."), *DefaultFishingRodClass->GetName());
    }
}
#endif // WITH_EDITOR

// --- PUBLIC API FOR FISHING ACTIONS ---

bool UCharacterFishingComponent::EquipNewRod(FName SocketName)
{
	if (!OwnerCharacter)
	{
		UE_LOG(LogFishingSystemComponent, Error, TEXT("Cannot equip new rod: OwnerCharacter is null."));
		return false;
	}
	if (EquippedFishingRod)
	{
		UE_LOG(LogFishingSystemComponent, Warning, TEXT("Cannot equip new rod: A rod (%s) is already equipped. Unequip first."), *EquippedFishingRod->GetName());
		return false;
	}
	if (!DefaultFishingRodClass)
	{
		UE_LOG(LogFishingSystemComponent, Error, TEXT("Cannot equip new rod: DefaultFishingRodClass is not set."));
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogFishingSystemComponent, Error, TEXT("Cannot equip new rod: World is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter; // The character owns the rod actor
	SpawnParams.Instigator = OwnerCharacter; // The character is the instigator

	FVector SpawnLocation = OwnerCharacter->GetActorLocation() + OwnerCharacter->GetActorForwardVector() * 100.0f; // Adjust as needed
	FRotator SpawnRotation = OwnerCharacter->GetActorRotation();

	AFishingRod* NewRod = World->SpawnActor<AFishingRod>(DefaultFishingRodClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (NewRod)
	{
		return EquipExistingRod(NewRod, SocketName.IsNone() ? DefaultHandSocketName : SocketName);
	}
	else
	{
		UE_LOG(LogFishingSystemComponent, Error, TEXT("Failed to spawn fishing rod of class %s."), *DefaultFishingRodClass->GetName());
	}
	return false;
}

bool UCharacterFishingComponent::EquipExistingRod(AFishingRod* RodToEquip, FName SocketName)
{
	if (!OwnerCharacter)
	{
		UE_LOG(LogFishingSystemComponent, Error, TEXT("Cannot equip existing rod: OwnerCharacter is null."));
		return false;
	}
	if (!RodToEquip)
	{
		UE_LOG(LogFishingSystemComponent, Warning, TEXT("Cannot equip existing rod: RodToEquip is null."));
		return false;
	}
	if (EquippedFishingRod)
	{
		UE_LOG(LogFishingSystemComponent, Warning, TEXT("Cannot equip rod %s: Another rod (%s) is already equipped. Unequip first."), *RodToEquip->GetName(),*EquippedFishingRod->GetName());
		return false;
	}

	EquippedFishingRod = RodToEquip;
	EquippedFishingRod->Equip(OwnerCharacter, SocketName.IsNone() ? DefaultHandSocketName : SocketName);
	UE_LOG(LogFishingSystemComponent, Log, TEXT("%s's FishingComponent equipped rod %s to socket %s."), *OwnerCharacter->GetName(), *EquippedFishingRod->GetName(), *(SocketName.IsNone() ? DefaultHandSocketName : SocketName).ToString());
	return true;
}

void UCharacterFishingComponent::UnequipRod()
{
	if (EquippedFishingRod)
	{
		UE_LOG(LogFishingSystemComponent, Log, TEXT("%s's FishingComponent unequipped rod %s."), OwnerCharacter ? *OwnerCharacter->GetName() : TEXT("Unknown Owner"), *EquippedFishingRod->GetName());
		EquippedFishingRod->Unequip();

		// Now, destroy the rod actor itself
		if (EquippedFishingRod->IsValidLowLevel()) // Good practice to check before destroying
		{
			UE_LOG(LogFishingSystemComponent, Log, TEXT("Destroying unequipped rod: %s"), *EquippedFishingRod->GetName());
			EquippedFishingRod->Destroy();
		}
		
		EquippedFishingRod = nullptr;
	}
	else
	{
		UE_LOG(LogFishingSystemComponent, Log, TEXT("FishingComponent: Tried to unequip rod, but none was equipped."));
	}
}

void UCharacterFishingComponent::InitiateCast()
{
    if (!OwnerCharacter)
    {
        UE_LOG(LogFishingSystemInput, Warning, TEXT("FishingComponent: InitiateCast - OwnerCharacter is null."));
        return;
    }

    if (EquippedFishingRod && EquippedFishingRod->IsEquipped() && !EquippedFishingRod->IsLineCastOut() && !EquippedFishingRod->bIsPreparingToCast)
    {
        EquippedFishingRod->InitiateCastAttempt();
        UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: InitiateCast called on %s. Rod is preparing."), *EquippedFishingRod->GetName());

        if (EquippedFishingRod->bIsPreparingToCast && CastingMontage)
        {
            // --- VALIDATION STEP ---
            if (!HasSpecificAnimNotify(CastingMontage, UAnimNotify_ExecuteFishingLaunch::StaticClass()))
            {
                FString ErrorMsg = FString::Printf(
                    TEXT("CharacterFishingComponent on '%s': The assigned 'CastingMontage' ('%s') is MISSING the required 'Execute Fishing Rod Launch' AnimNotify. The bobber will not be launched."),
                    *OwnerCharacter->GetName(),
                    *CastingMontage->GetName()
                );
                UE_LOG(LogFishingSystemSetup, Error, TEXT("%s"), *ErrorMsg);

                // Display an on-screen message for the designer and a clickable Message Log entry
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, ErrorMsg);
                }
                FMessageLog("PIE").Error(FText::FromString(ErrorMsg))
                    ->AddToken(FUObjectToken::Create(this)) // Token for this component
                    ->AddToken(FUObjectToken::Create(CastingMontage)); // Token for the problematic montage

                // Optionally, you could also prevent the montage from playing or cancel the cast attempt here:
                // EquippedFishingRod->CancelCastAttempt(); // Reset bIsPreparingToCast
                // return; // Don't play the montage
            }
            // --- END VALIDATION STEP ---


            UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance();
            if (AnimInstance)
            {
                if (!AnimInstance->Montage_IsPlaying(CastingMontage))
                {
                    float PlayRate = 1.0f;
                    AnimInstance->Montage_Play(CastingMontage, PlayRate);
                    UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: Playing CastingMontage '%s' on %s."), *CastingMontage->GetName(), *OwnerCharacter->GetName());
                }
                // ... (else already playing)
            }
            // ... (else no AnimInstance)
        }
        else if (!CastingMontage)
        {
            UE_LOG(LogFishingSystemInput, Warning, TEXT("FishingComponent on %s: Rod is preparing to cast, but no CastingMontage is assigned. Bobber launch will not occur via animation."), *OwnerCharacter->GetName());
            // You might want to immediately cancel the cast if a montage is essential
            // EquippedFishingRod->CancelCastAttempt();
        }
    }
    // ... (else conditions not met for casting) ...
}

void UCharacterFishingComponent::CancelCast()
{
	if (EquippedFishingRod && EquippedFishingRod->IsEquipped() && EquippedFishingRod->bIsPreparingToCast)
	{
		EquippedFishingRod->CancelCastAttempt(); // This sets bIsPreparingToCast = false

		// Stop the montage if it's playing
		if (CastingMontage && OwnerCharacter)
		{
			UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance();
			if (AnimInstance && AnimInstance->Montage_IsPlaying(CastingMontage))
			{
				AnimInstance->Montage_Stop(0.25f, CastingMontage); // 0.25f blend out time
				UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: Cancelled cast and stopped CastingMontage '%s'."), *CastingMontage->GetName());
			}
		}
		UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: CancelCast called on %s"), *EquippedFishingRod->GetName());
	}
}

void UCharacterFishingComponent::ExecuteLaunchFromAnimation()
{
    if (EquippedFishingRod && EquippedFishingRod->IsEquipped() && EquippedFishingRod->bIsPreparingToCast)
    {
        EquippedFishingRod->ExecuteLaunchFromAnimation();
        UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: ExecuteLaunchFromAnimation called on %s"), *EquippedFishingRod->GetName());
    }
     else
    {
        UE_LOG(LogFishingSystemInput, Verbose, TEXT("FishingComponent: ExecuteLaunchFromAnimation - Rod not ready for launch."));
    }
}

void UCharacterFishingComponent::RequestFullReelIn()
{
    if (EquippedFishingRod && EquippedFishingRod->IsEquipped() && EquippedFishingRod->IsLineCastOut())
    {
        EquippedFishingRod->FullReelIn();
        UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: RequestFullReelIn called on %s"), *EquippedFishingRod->GetName());
    }
    else
    {
        UE_LOG(LogFishingSystemInput, Verbose, TEXT("FishingComponent: RequestFullReelIn - Rod not ready or line not cast."));
    }
}


// --- INPUT BINDING ---
void UCharacterFishingComponent::SetupPlayerInputBindings(UEnhancedInputComponent* InEnhancedInputComponent, APlayerController* InPlayerController)
{
	if (!InEnhancedInputComponent)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("FishingComponent: InEnhancedInputComponent is null in SetupEnhancedInputBindings."));
		return;
	}
	if (!InPlayerController)
	{
		UE_LOG(LogFishingSystemSetup, Error, TEXT("FishingComponent: InPlayerController is null in SetupEnhancedInputBindings."));
		return;
	}
	if (!OwnerCharacter)
	{
		UE_LOG(LogFishingSystemSetup, Warning, TEXT("FishingComponent: OwnerCharacter is null in SetupEnhancedInputBindings. Context is correct but owner ref missing."));
	}

    // Add Input Mapping Context
    if (FishingInputMappingContext)
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(InPlayerController->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(FishingInputMappingContext, InputMappingPriority);
            RegisteredPlayerController = InPlayerController; // Store the controller we registered with
            UE_LOG(LogFishingSystemSetup, Log, TEXT("FishingComponent: Added FishingInputMappingContext '%s' with priority %d."), *FishingInputMappingContext->GetName(), InputMappingPriority);
        }
        else
        {
            UE_LOG(LogFishingSystemSetup, Error, TEXT("FishingComponent: Failed to get UEnhancedInputLocalPlayerSubsystem. Cannot add IMC."));
            return; // Cannot bind actions if IMC cannot be added
        }
    }
    else
    {
        UE_LOG(LogFishingSystemSetup, Warning, TEXT("FishingComponent: FishingInputMappingContext is not set. No input actions will be bound."));
        return; // No IMC, no bindings
    }

    // Bind actions
    if (ToggleEquipAction)
    {
        InEnhancedInputComponent->BindAction(ToggleEquipAction, ETriggerEvent::Started, this, &UCharacterFishingComponent::HandleToggleEquip);
        UE_LOG(LogFishingSystemSetup, Log, TEXT("Bound ToggleEquipAction '%s'"), *ToggleEquipAction->GetName());
    }
    if (InitiateCastAction)
    {
        InEnhancedInputComponent->BindAction(InitiateCastAction, ETriggerEvent::Started, this, &UCharacterFishingComponent::HandleInitiateCast);
        UE_LOG(LogFishingSystemSetup, Log, TEXT("Bound InitiateCastAction '%s'"), *InitiateCastAction->GetName());
    }
    if (CancelCastAction)
    {
        InEnhancedInputComponent->BindAction(CancelCastAction, ETriggerEvent::Started, this, &UCharacterFishingComponent::HandleCancelCast);
        UE_LOG(LogFishingSystemSetup, Log, TEXT("Bound CancelCastAction '%s'"), *CancelCastAction->GetName());
    }
    if (ReelInLineAction)
    {
        InEnhancedInputComponent->BindAction(ReelInLineAction, ETriggerEvent::Started, this, &UCharacterFishingComponent::HandleReelInLine_Started); // On press
        InEnhancedInputComponent->BindAction(ReelInLineAction, ETriggerEvent::Completed, this, &UCharacterFishingComponent::HandleReelInLine_Completed); // On release
        InEnhancedInputComponent->BindAction(ReelInLineAction, ETriggerEvent::Canceled, this, &UCharacterFishingComponent::HandleReelInLine_Completed); // Also stop on cancel
        UE_LOG(LogFishingSystemSetup, Log, TEXT("Bound ReelInLineAction '%s' for Started, Completed, Canceled"), *ReelInLineAction->GetName());
    }
    if (ExtendLineAction)
    {
        InEnhancedInputComponent->BindAction(ExtendLineAction, ETriggerEvent::Started, this, &UCharacterFishingComponent::HandleExtendLine_Started);
        InEnhancedInputComponent->BindAction(ExtendLineAction, ETriggerEvent::Completed, this, &UCharacterFishingComponent::HandleExtendLine_Completed);
        InEnhancedInputComponent->BindAction(ExtendLineAction, ETriggerEvent::Canceled, this, &UCharacterFishingComponent::HandleExtendLine_Completed);
        UE_LOG(LogFishingSystemSetup, Log, TEXT("Bound ExtendLineAction '%s' for Started, Completed, Canceled"), *ExtendLineAction->GetName());
    }
    if (FullReelInAction)
    {
        InEnhancedInputComponent->BindAction(FullReelInAction, ETriggerEvent::Started, this, &UCharacterFishingComponent::HandleFullReelIn);
        UE_LOG(LogFishingSystemSetup, Log, TEXT("Bound FullReelInAction '%s'"), *FullReelInAction->GetName());
    }

	UE_LOG(LogFishingSystemSetup, Log, TEXT("FishingComponent: Enhanced Input binding setup complete."));
}

// --- INPUT ACTION HANDLERS (ENHANCED INPUT) ---
// The FInputActionValue parameter is present but may not be used if the action is a simple bool trigger.
// It's included for completeness and if you later use Axis values from actions.

void UCharacterFishingComponent::HandleToggleEquip(const FInputActionValue& Value)
{
	UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: HandleToggleEquip triggered."));
	if (!OwnerCharacter) return;

	if (IsRodEquipped())
	{
		UnequipRod();
	}
	else
	{
		EquipNewRod(DefaultHandSocketName);
	}
}

void UCharacterFishingComponent::HandleInitiateCast(const FInputActionValue& Value)
{
	UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: HandleInitiateCast triggered."));
	InitiateCast();
}

void UCharacterFishingComponent::HandleCancelCast(const FInputActionValue& Value)
{
	UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: HandleCancelCast triggered."));
	CancelCast();
}

void UCharacterFishingComponent::HandleReelInLine_Started(const FInputActionValue& Value)
{
	UE_LOG(LogFishingSystemInput, Warning, TEXT("--- FishingComponent: HandleReelInLine_Started ENTERED ---"));

	// if (EquippedFishingRod && EquippedFishingRod->IsEquipped() && EquippedFishingRod->IsLineCastOut()) // OLD Check
	if (EquippedFishingRod && EquippedFishingRod->IsEquipped()) // NEW Check: Only care if rod is equipped
	{
		EquippedFishingRod->StartIncrementalReel();
		UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: ReelInLine Started, called StartIncrementalReel on %s"), *EquippedFishingRod->GetName());
	} else {
		UE_LOG(LogFishingSystemInput, Warning, TEXT("FishingComponent: ReelInLine Started, BUT ROD NOT READY/VALID. Conditions:"));
		if (!EquippedFishingRod) {
			UE_LOG(LogFishingSystemInput, Warning, TEXT("  - EquippedFishingRod is NULL"));
		} else {
			UE_LOG(LogFishingSystemInput, Warning, TEXT("  - EquippedFishingRod valid: %s"), *EquippedFishingRod->GetName());
			UE_LOG(LogFishingSystemInput, Warning, TEXT("  - Rod->IsEquipped(): %s"), EquippedFishingRod->IsEquipped() ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}
}

void UCharacterFishingComponent::HandleReelInLine_Completed(const FInputActionValue& Value)
{
	if (EquippedFishingRod && EquippedFishingRod->IsEquipped()) // Check if equipped even on release
	{
		EquippedFishingRod->StopIncrementalReel();
		UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: ReelInLine Completed/Canceled, called StopIncrementalReel on %s"), *EquippedFishingRod->GetName());
	}
}

void UCharacterFishingComponent::HandleExtendLine_Started(const FInputActionValue& Value)
{
	UE_LOG(LogFishingSystemInput, Warning, TEXT("--- FishingComponent: HandleExtendLine_Started ENTERED ---"));

	// if (EquippedFishingRod && EquippedFishingRod->IsEquipped() && EquippedFishingRod->IsLineCastOut()) // OLD Check
	if (EquippedFishingRod && EquippedFishingRod->IsEquipped()) // NEW Check: Only care if rod is equipped
	{
		EquippedFishingRod->StartExtendingLine();
		UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: ExtendLine Started, called StartExtendingLine on %s"), *EquippedFishingRod->GetName());
	} else {
		UE_LOG(LogFishingSystemInput, Warning, TEXT("FishingComponent: ExtendLine Started, BUT ROD NOT READY/VALID. Conditions:"));
		if (!EquippedFishingRod) {
			UE_LOG(LogFishingSystemInput, Warning, TEXT("  - EquippedFishingRod is NULL"));
		} else {
			UE_LOG(LogFishingSystemInput, Warning, TEXT("  - EquippedFishingRod valid: %s"), *EquippedFishingRod->GetName());
			UE_LOG(LogFishingSystemInput, Warning, TEXT("  - Rod->IsEquipped(): %s"), EquippedFishingRod->IsEquipped() ? TEXT("TRUE") : TEXT("FALSE"));
			// No longer logging IsLineCastOut here as we removed it from the component's direct check
		}
	}
}

void UCharacterFishingComponent::HandleExtendLine_Completed(const FInputActionValue& Value)
{
	if (EquippedFishingRod && EquippedFishingRod->IsEquipped())
	{
		EquippedFishingRod->StopExtendingLine(); // Calls rod function
		UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: ExtendLine Completed/Canceled, called StopExtendingLine on %s"), *EquippedFishingRod->GetName());
	}
}

void UCharacterFishingComponent::HandleFullReelIn(const FInputActionValue& Value)
{
	UE_LOG(LogFishingSystemInput, Log, TEXT("FishingComponent: HandleFullReelIn triggered."));
	RequestFullReelIn();
}

bool UCharacterFishingComponent::HasSpecificAnimNotify(const UAnimMontage* Montage, TSubclassOf<UAnimNotify> NotifyClass)
{
	if (!Montage || !NotifyClass)
	{
		return false;
	}

	for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
	{
		if (NotifyEvent.NotifyStateClass) // If it's an AnimNotifyState
		{
			// Not directly checking NotifyStateClass here, but you could if needed
		}
		else if (NotifyEvent.Notify) // If it's a UAnimNotify
		{
			if (NotifyEvent.Notify->IsA(NotifyClass))
			{
				return true;
			}
		}
	}
	return false;
}




