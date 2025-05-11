#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CharacterFishingComponent.generated.h"

struct FInputActionValue;
class AFishingRod;
class ACharacter;
class UInputComponent;
class UInputMappingContext; // Forward declaration
class UInputAction;       // Forward declaration
class UEnhancedInputComponent; // Forward declaration
class APlayerController;

/**
 * @brief Component for managing fishing mechanics on a character.
 * Allows equipping/unequipping a fishing rod, handling casting, reeling, and other fishing actions.
 * Input actions can be configured in the editor.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FISHINGPROJECT_API UCharacterFishingComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// --- CONSTRUCTOR ---
	UCharacterFishingComponent();

protected:
	// --- UE4 LIFECYCLE ---
	/** Called when the game starts */
	virtual void BeginPlay() override;

	/** Called when the component is destroyed or the game ends */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	/** Called when a component is created or its configuration is changed in the editor. */
	virtual void OnComponentCreated() override; // Good for initial setup if needed
	virtual void OnRegister() override;       // Called when component is registered
	virtual void OnUnregister() override;     // Called when component is unregistered
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
public:
	
	// --- ENHANCED INPUT CONFIGURATION ---
	/** The Input Mapping Context to use for fishing controls. This context should contain all fishing-related Input Actions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input")
	UInputMappingContext* FishingInputMappingContext;

	/** Priority for the FishingInputMappingContext. Higher priority contexts are processed first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input")
	int32 InputMappingPriority = 0;

	/** Input Action for toggling Equip/Unequip of the fishing rod. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input|Actions")
	UInputAction* ToggleEquipAction;

	/** Input Action for initiating a cast (starts the cast preparation/animation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input|Actions")
	UInputAction* InitiateCastAction;

	/** Input Action for canceling an ongoing cast attempt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input|Actions")
	UInputAction* CancelCastAction;

	/** Input Action for incrementally reeling in the fishing line. Should be configured for press and release (e.g., Started and Completed triggers). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input|Actions")
	UInputAction* ReelInLineAction;

	/** Input Action for incrementally extending the fishing line. Should be configured for press and release. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input|Actions")
	UInputAction* ExtendLineAction;

	/** Input Action for fully reeling in the bobber to the rod tip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing Component|Input|Enhanced Input|Actions")
	UInputAction* FullReelInAction;
	
	// --- PUBLIC API FOR FISHING ACTIONS ---

	/**
	 * @brief Attempts to equip a new fishing rod of the specified class or the default FishingRodClass.
	 * @param SocketName The socket on the character mesh to attach the rod to.
	 * @return True if a rod was successfully equipped, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Component|Actions")
	bool EquipNewRod(FName SocketName = FName("hand_r_socket"));

	/**
	 * @brief Equips a pre-existing fishing rod actor.
	 * @param RodToEquip The fishing rod actor to equip.
	 * @param SocketName The socket on the character mesh to attach the rod to.
	 * @return True if the rod was successfully equipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Component|Actions")
	bool EquipExistingRod(AFishingRod* RodToEquip, FName SocketName = FName("hand_r_socket"));

	/**
	 * @brief Unequips the currently held fishing rod.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Component|Actions")
	void UnequipRod();

	/**
	 * @brief Initiates a cast attempt with the equipped rod.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Component|Actions")
	void InitiateCast();

	/**
	 * @brief Cancels an ongoing cast attempt.
	 * (Useful if casting is a charged action and player releases early without full charge)
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Component|Actions")
	void CancelCast();
	
	/**
	 * @brief Executes the launch of the bobber from the equipped rod.
	 * Typically called by an animation notify on the character.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Component|Actions")
	void ExecuteLaunchFromAnimation();


	/**
	 * @brief Fully reels in the line of the equipped rod.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Component|Actions")
	void RequestFullReelIn();


	// --- INPUT BINDING ---
public:
	/**
	 * @brief Sets up the necessary input bindings for fishing actions.
	 * This should be called by the owning Actor (e.g., Character) in its SetupPlayerInputComponent.
	 * @param InInputComponent The InputComponent of the owning Actor.
	 */
	void SetupPlayerInputBindings(UEnhancedInputComponent* InEnhancedInputComponent, APlayerController* InPlayerController);

protected:

	UPROPERTY(Transient) // To keep track if our IMC is currently added
	APlayerController* RegisteredPlayerController;
	
	// --- INPUT ACTION HANDLERS ---
	void HandleToggleEquip(const FInputActionValue& Value);
	void HandleInitiateCast(const FInputActionValue& Value);
	void HandleCancelCast(const FInputActionValue& Value);
	void HandleReelInLine_Started(const FInputActionValue& Value);    // Corresponds to IE_Pressed
	void HandleReelInLine_Completed(const FInputActionValue& Value);  // Corresponds to IE_Released
	void HandleExtendLine_Started(const FInputActionValue& Value);   // Corresponds to IE_Pressed
	void HandleExtendLine_Completed(const FInputActionValue& Value); // Corresponds to IE_Released
	void HandleFullReelIn(const FInputActionValue& Value);
	// Note: Cast Pressed/Released might be handled by InitiateCast and ExecuteLaunchFromAnimation directly
	// if casting is tied to animation or a single button press.
    // If cast is a "hold and release" mechanic without anim notify for launch, you'd add:
    // void HandleCastAction_Pressed();
    // void HandleCastAction_Released(); 


	// --- CONFIGURATION ---
public:
	/** The class of AFishingRod to spawn when EquipNewRod is called. Assign this in Blueprints or C++. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fishing Component|Configuration")
	TSubclassOf<AFishingRod> DefaultFishingRodClass;

	/** Default socket name on the character's mesh to attach the fishing rod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fishing Component|Configuration")
	FName DefaultHandSocketName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing Component|Animation")
	UAnimMontage* CastingMontage;

	// --- STATE ---
protected:
	/** Pointer to the ACharacter that owns this component. */
	UPROPERTY(Transient)
	ACharacter* OwnerCharacter;

	/** Pointer to the currently equipped fishing rod. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Component|State", Transient)
	AFishingRod* EquippedFishingRod;

#if WITH_EDITORONLY_DATA
	/**
	 * @brief The fishing rod actor used for editor preview purposes only.
	 * Marked Transient so it's not saved with the level/actor.
	 */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient) // Ensure it's not duplicated or exported
	AFishingRod* EditorPreviewRod;
#endif
	

public:
	/** @return The currently equipped fishing rod, or nullptr if none. */
	UFUNCTION(BlueprintPure, Category = "Fishing Component|State")
	AFishingRod* GetEquippedFishingRod() const { return EquippedFishingRod; }

	/** @return True if a fishing rod is currently equipped. */
	UFUNCTION(BlueprintPure, Category = "Fishing Component|State")
	bool IsRodEquipped() const { return EquippedFishingRod != nullptr; }


private:
#if WITH_EDITOR
	/** Helper function to update the editor preview rod. */
	void UpdateEditorPreviewRod();

	/** Helper function to destroy the current editor preview rod. */
	void DestroyEditorPreviewRod();
#endif

	/** Attempts to find the PlayerController and EnhancedInputComponent to set up input bindings. */
	void TryAutoSetupPlayerInputBindings();

	/** Flag to ensure input bindings are set up only once. */
	bool bInputBindingsInitialized;

	bool HasSpecificAnimNotify(const UAnimMontage* Montage, TSubclassOf<UAnimNotify> NotifyClass);
};