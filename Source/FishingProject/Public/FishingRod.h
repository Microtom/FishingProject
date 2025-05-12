#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FishingRod.generated.h"

class UFishingLineComponent;
// Forward declarations
class USceneComponent;
class UStaticMeshComponent;
class UCableComponent;
class ACharacter;
class AFishingBobber;
enum class EBobberState : uint8; // Forward declare enum from FishingBobber.h

/**
 * @class AFishingRod
 * @brief Represents a fishing rod actor that can be equipped by a character.
 *
 * Handles casting, reeling, line physics approximation, and interaction with a FishingBobber.
 */
UCLASS()
class AFishingRod : public AActor
{
	GENERATED_BODY()

public:
	// --- CONSTRUCTOR ---
	AFishingRod();
	void OnConstruction(const FTransform& Transform);

	// --- UE4 LIFECYCLE ---
protected:
	/** Called when the game starts or when spawned. */
	virtual void BeginPlay() override;
public:
	/** Called every frame. */
	virtual void Tick(float DeltaTime) override;

	// --- PUBLIC API - ROD ACTIONS & STATE ---
public:
	/**
	 * @brief Equips the fishing rod to a character.
	 * @param OwningCharacter The character equipping the rod.
	 * @param SocketName The name of the socket on the character's mesh to attach the rod to.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void Equip(ACharacter* OwningCharacter, FName SocketName);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fishing Rod|Casting", meta = (ClampMin = "-45.0", ClampMax = "45.0", UIMin = "-45.0", UIMax = "45.0"))
	float CastAimPitchAdjustment = -32.f;
	
	/**
	 * @brief Unequips the fishing rod from the character.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void Unequip();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fishing Rod|Configuration", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5000.0"))
	float DefaultLaunchImpulse;
	
	/**
	 * @brief Initiates the casting process. Sets a flag indicating the rod is preparing to cast.
	 * Actual launch might be triggered by an animation notify or further input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void InitiateCastAttempt();

	/**
	 * @brief Executes the bobber launch, typically called from an animation notify.
	 * Calculates launch direction and speed, then launches the bobber.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void ExecuteLaunchFromAnimation();

	/**
	 * @brief Cancels an ongoing cast attempt.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void CancelCastAttempt();

	/**
	 * @brief Fully reels in the bobber, setting it to dangle at the rod tip.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void FullReelIn();

	/**
	 * @brief Starts incrementally reeling in the fishing line.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void StartIncrementalReel();

	/**
	 * @brief Stops incrementally reeling in the fishing line.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void StopIncrementalReel();

	/**
	 * @brief Starts extending the fishing line (if applicable, e.g., letting out slack).
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void StartExtendingLine();

	/**
	 * @brief Stops extending the fishing line.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fishing Rod|Actions")
	void StopExtendingLine();
	
	/** @return True if the rod is currently equipped, false otherwise. */
	UFUNCTION(BlueprintPure, Category = "Fishing Rod|State")
	bool IsEquipped() const { return bIsEquipped; }

    /** @return True if the line is currently cast out, false otherwise. */
    UFUNCTION(BlueprintPure, Category = "Fishing Rod|State")
    bool IsLineCastOut() const { return bLineIsCastOut; }

	// --- COMPONENTS ---
protected:
	/** Root component for the rod actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RodRootComponent;

	/** Static mesh component for the rod's visual representation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* RodMeshComponent;

	/** Scene component representing the point where the fishing line attaches to the rod. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* LineAttachPointComponent;

	/** Cable component to visually represent the fishing line. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UFishingLineComponent* FishingLineComponent;
	

	// --- CONFIGURATION ---
protected:
	/** The class of AFishingBobber to spawn. Assign this in Blueprints or C++. */
	UPROPERTY(EditDefaultsOnly, Category = "Fishing Rod|Configuration")
	TSubclassOf<AFishingBobber> BobberClass;

	/** Speed at which the line is reeled in (units per second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fishing Rod|Configuration")
	float ReelInSpeed;

	/** The class of UFishingLineComponent to use for this rod. Assign this in Blueprints. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fishing Rod|Configuration")
	TSubclassOf<UFishingLineComponent> FishingLineClass;
	
	/** Speed at which the line can be extended (units per second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fishing Rod|Configuration")
	float ExtendSpeed;

	/** Minimum length of the fishing line (e.g., when dangling at the tip). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fishing Rod|Configuration")
	float MinLineLength;

	/** Maximum length the fishing line can extend to. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fishing Rod|Configuration")
	float MaxLineLength;

	// --- INTERNAL LOGIC & HELPER FUNCTIONS ---
protected:
	/**
	 * @brief Spawns a new fishing bobber and prepares it for use (e.g., setting it to dangle).
	 * Called when the rod is equipped.
	 */
	void SpawnAndPrepareBobber();

	/**
	 * @brief Sets the attached bobber to the DanglingAtTip state and positions it accordingly.
	 * Resets line cast state.
	 */
	void SetBobberToDangle();


	void DetachAndLaunchBobberLogic(const FVector& LaunchDirection, float LaunchImpulseStrength);
	
	/**
	 * @brief Updates the fishing line and bobber's position/state when the line is cast out.
	 * Handles reeling, extending, and line payout during bobber flight.
	 * @param DeltaTime Game time elapsed during last frame.
	 */
	void UpdateLineAndBobberWhenCast(float DeltaTime);

	/**
	 * @brief Updates the fishing line behavior when the bobber is dangling at the rod tip.
	 * Applies corrective forces to keep the bobber near the MinLineLength distance.
	 * @param DeltaTime Game time elapsed during last frame.
	 */
	void UpdateLineWhenDangling(float DeltaTime);

	/**
	 * @brief Ensures the bobber (if not flying) stays within the CurrentLineLengthSetting from the rod tip.
	 * Applies corrective forces or snaps the bobber to the correct position.
	 */
	void EnsureBobberStaysWithinLineLength();

	/**
	 * @brief Calculates the force exerted on the rod tip by the fishing line and bobber.
	 * This can be used for rod bending animations or other feedback.
	 */
	void CalculateForceOnRodTip();

	/**
	 * @brief Draws a debug line representing the calculated force on the rod tip.
	 * Controlled by the CVar `r.Fishing.DrawDebugForces`.
	 */
	void DrawDebugForceOnRodTip() const;

	// --- STATE VARIABLES ---
protected: // Or private, depending on whether derived classes need direct access
	/** True if the rod is currently equipped by a character. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	bool bIsEquipped;

	/** The character currently owning/using this fishing rod. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	ACharacter* CurrentOwnerCharacter;

	/** The fishing bobber actor attached to this rod's line. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	AFishingBobber* AttachedBobber;
public:
	/** True if the rod is currently in the 'preparing to cast' phase (e.g., player is holding down cast button). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	bool bIsPreparingToCast;
protected:
	/** True if the fishing line has been cast out and the bobber is in flight or in water. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	bool bLineIsCastOut;

	/** True if the player is actively reeling in the line. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	bool bIsActivelyReeling;

    /** True if the player is actively extending the line. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
    bool bIsActivelyExtending;

	/** The current target length of the fishing line, adjusted by reeling/extending. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	float CurrentLineLengthSetting;

	/** The calculated force vector currently being applied to the rod tip. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Fishing Rod|State", Transient)
	FVector ForceOnRodTip;
};