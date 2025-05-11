#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FishingBobber.generated.h"

// Forward declarations
class UStaticMeshComponent;
class AFishingRod; // Though OwningRod is AActor type, it's contextually a FishingRod

/**
 * @enum EBobberState
 * @brief Defines the possible states of the fishing bobber.
 */
UENUM(BlueprintType)
enum class EBobberState : uint8
{
	Idle			UMETA(DisplayName = "Idle"),			// Not active, potentially on ground or just spawned
	DanglingAtTip	UMETA(DisplayName = "DanglingAtTip"),	// Hanging from the rod tip, physics active for sway
	Flying			UMETA(DisplayName = "Flying"),			// Being cast, projectile movement active
	InWater			UMETA(DisplayName = "InWater"),			// Floating in water, physics active for buoyancy (future)
	// Add more states like Hooked, BeingReeled, etc. as needed
};

/**
 * @class AFishingBobber
 * @brief Represents the bobber at the end of the fishing line.
 *
 * Handles its own physics for dangling, flying (as a projectile), and future states like floating in water.
 */
UCLASS()
class AFishingBobber : public AActor
{
	GENERATED_BODY()

public:
	// --- CONSTRUCTOR ---
	AFishingBobber();

	// --- UE4 LIFECYCLE ---
protected:
	/** Called when the game starts or when spawned. */
	virtual void BeginPlay() override;
public:
	/** Called every frame. */
	virtual void Tick(float DeltaTime) override;

	// --- PUBLIC API & STATE MANAGEMENT ---
public:
	/**
	 * @brief Sets the current state of the bobber, adjusting physics and movement components accordingly.
	 * @param NewState The new state to transition to.
	 */
	void SetBobberState(EBobberState NewState);

	/**
	 * @brief Gets the current state of the bobber.
	 * @return The current EBobberState.
	 */
	UFUNCTION(BlueprintPure, Category = "Fishing Bobber")
	EBobberState GetCurrentBobberState() const { return CurrentState; }

	
	void LaunchAsPhysicsActor(const FVector& LaunchDirection, float LaunchImpulseStrength, AActor* RodOwner);

	// --- COMPONENTS ---
public:
	/** Static mesh component for the bobber's visual representation and physics body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* BobberMeshComponent;

	// --- PROTECTED STATE ENTRY METHODS ---
protected:
	/**
	 * @brief Configures the bobber for the Idle state.
	 * Physics may be on or off depending on desired idle behavior (e.g., resting on ground).
	 * Currently enables physics for a more dynamic "settled" state.
	 */
	void EnterIdleState();

	/**
	 * @brief Configures the bobber for the DanglingAtTip state.
	 * Enables physics simulation for realistic dangling from the rod tip.
	 */
	void EnterDanglingState();

	
	void EnterFlyingState_Physics();
	void OnBobberHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	                 FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(EditDefaultsOnly, Category = "BobberPhysics", meta = (ClampMin = "0.01"))
	float DefaultMassKg = 0.1f;
	

	// --- PRIVATE MEMBER VARIABLES ---
private:
	/** Pointer to the fishing rod that owns this bobber. Set during launch or equip. */
	UPROPERTY(Transient) // Avoid saving this reference, it's runtime state
	AActor* OwningRod;

	/** The current operational state of the bobber. */
	UPROPERTY(VisibleAnywhere, Category = "Fishing Bobber|State", Transient)
	EBobberState CurrentState;

	float IntendedMass;
};