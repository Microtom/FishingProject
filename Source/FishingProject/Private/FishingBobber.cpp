#include "FishingBobber.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/Engine.h" // For GEngine
#include "FishingLogChannels.h" // For custom logging

// --- CONSTRUCTOR ---

/**
 * @brief Constructor for AFishingBobber.
 * Initializes components and default values.
 */
AFishingBobber::AFishingBobber()
{
	PrimaryActorTick.bCanEverTick = true;

	// Initialize Bobber Mesh Component
	BobberMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BobberMesh"));
	RootComponent = BobberMeshComponent;
	BobberMeshComponent->SetSimulatePhysics(true); // Initial state, will be changed by SetBobberState
	BobberMeshComponent->SetCollisionProfileName(TEXT("PhysicsActor")); 
	BobberMeshComponent->SetMassOverrideInKg(NAME_None, DefaultMassKg, true);
	IntendedMass = DefaultMassKg; 
	BobberMeshComponent->SetLinearDamping(0.5f);
	BobberMeshComponent->SetAngularDamping(1.5f);

	// Initialize State Variables
	OwningRod = nullptr;
	CurrentState = EBobberState::Idle; // Default state

	UE_LOG(LogFishingSystemSetup, Log, TEXT("AFishingBobber Constructor: Initialized. CurrentState: Idle."));
}

// --- UE4 LIFECYCLE ---

/**
 * @brief Called when the game starts or when spawned.
 * Sets the initial state of the bobber.
 */
void AFishingBobber::BeginPlay()
{
	Super::BeginPlay();
	SetBobberState(EBobberState::Idle); // Ensure consistent starting state
	UE_LOG(LogFishingSystemBobber, Log, TEXT("%s BeginPlay: Set to Idle State."), *GetName());
}

/**
 * @brief Called every frame.
 * Handles per-frame logic, like checking if the bobber fell out of the world.
 * @param DeltaTime Game time elapsed during last frame.
 */
void AFishingBobber::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Safety check: destroy if fallen out of world
	if (GetActorLocation().Z < -5000.0f)
	{
		UE_LOG(LogFishingSystemBobber, Warning, TEXT("%s fell out of world at Z=%f, destroying."), *GetName(), GetActorLocation().Z);
		Destroy();
	}
    // Verbose logging can be enabled here for debugging specific states or values
    // UE_LOG(LogFishingSystemBobber, VeryVerbose, TEXT("%s Tick. State: %s, Location: %s, Physics: %s, Velocity: %s"),
    //      *GetName(),
    //      *UEnum::GetValueAsString(CurrentState),
    //      *GetActorLocation().ToString(),
    //      BobberMeshComponent->IsSimulatingPhysics() ? TEXT("ON") : TEXT("OFF"),
    //      BobberMeshComponent->IsSimulatingPhysics() ? *BobberMeshComponent->GetPhysicsLinearVelocity().ToString() : TEXT("N/A"));
}

// --- PUBLIC API & STATE MANAGEMENT ---

/**
 * @brief Sets the current state of the bobber, adjusting physics and movement components accordingly.
 * @param NewState The new state to transition to.
 */
void AFishingBobber::SetBobberState(EBobberState NewState)
{
    // Avoid redundant state changes unless it's a re-trigger for a specific state like Flying
	if (CurrentState == NewState) return;

	UE_LOG(LogFishingSystemBobber, Log, TEXT("%s Changing state from %s to %s"), *GetName(), *UEnum::GetValueAsString(CurrentState), *UEnum::GetValueAsString(NewState));
	EBobberState PrevState = CurrentState;
	CurrentState = NewState;

	switch (CurrentState)
	{
	case EBobberState::Idle:
		EnterIdleState();
		break;
	case EBobberState::DanglingAtTip:
		EnterDanglingState();
		break;
	case EBobberState::Flying:
		EnterFlyingState_Physics(); // Call new state entry
		break;
	case EBobberState::InWater:
		// EnterInWaterState(); // Implement this later
			EnterIdleState(); // Placeholder
		break;
	default:
		UE_LOG(LogFishingSystemBobber, Error, TEXT("%s Unknown EBobberState! Defaulting to Idle."), *GetName());
		EnterIdleState();
		break;
	}
}

// New Launch function using physics impulse
void AFishingBobber::LaunchAsPhysicsActor(const FVector& LaunchDirection, float LaunchImpulseStrength, AActor* RodOwner)
{
	OwningRod = RodOwner;
	UE_LOG(LogFishingSystemBobber, Log, TEXT("%s LaunchAsPhysicsActor. Direction: %s, ImpulseStrength: %.2f"),
		*GetName(), *LaunchDirection.ToString(), LaunchImpulseStrength);

	// Ensure we are in a state ready for launch (or force it)
	SetBobberState(EBobberState::Flying); // This will call EnterFlyingState_Physics

	if (BobberMeshComponent && BobberMeshComponent->IsSimulatingPhysics())
	{
		// Apply the impulse
		// Make sure the bobber is "awake"
		BobberMeshComponent->WakeRigidBody();
		// Add impulse wants a direction * impulse strength
		BobberMeshComponent->AddImpulse(LaunchDirection.GetSafeNormal() * LaunchImpulseStrength, NAME_None, true /*bVelChange - true means it's an instant velocity change*/);
	}
	else
	{
		UE_LOG(LogFishingSystemBobber, Warning, TEXT("%s Tried to LaunchAsPhysicsActor, but BobberMeshComponent is null or not simulating physics!"), *GetName());
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("%s launched with impulse. Strength: %.0f"), *GetName(), LaunchImpulseStrength));
	}
}

// --- PROTECTED STATE ENTRY METHODS ---

/**
 * @brief Configures the bobber for the Idle state.
 * Physics is enabled to allow it to settle naturally if on a surface, or to be affected by minor forces.
 * Damping is increased to make it settle more quickly.
 */
void AFishingBobber::EnterIdleState()
{
	UE_LOG(LogFishingSystemBobber, Log, TEXT("%s Entering Idle State (Physics ON)."), *GetName());
	if (BobberMeshComponent)
	{
		BobberMeshComponent->SetSimulatePhysics(true); // Ensure it's on
		BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BobberMeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
		// Adjust damping for idle
		BobberMeshComponent->SetLinearDamping(2.5f);
		BobberMeshComponent->SetAngularDamping(2.5f);
		// BobberMeshComponent->SetMassOverrideInKg(NAME_None, 0.1f, true); // Mass set in constructor or as needed
		// IntendedMass = 0.1f;
		BobberMeshComponent->WakeRigidBody();
	}
}

/**
 * @brief Configures the bobber for the DanglingAtTip state.
 * Enables physics simulation for realistic dangling from the rod tip.
 * Damping values are set for a responsive but controlled dangle.
 */
void AFishingBobber::EnterDanglingState()
{
	UE_LOG(LogFishingSystemBobber, Log, TEXT("%s Entering DanglingAtTip State (Bobber Physics OFF, Line Controlled)."), *GetName());
	if (BobberMeshComponent)
	{
		BobberMeshComponent->SetSimulatePhysics(false); // Correct: Bobber doesn't simulate its own movement
		BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly); // Or NoCollision if preferred
		// BobberMeshComponent->SetEnableGravity(false); // Redundant
		BobberMeshComponent->WakeRigidBody(); // Harmless
	}
}

/**
 * @brief Configures the bobber for the Flying state.
 * Deactivates direct physics simulation and activates the ProjectileMovementComponent.
 * Collision is set to "Projectile" to allow specific interactions during flight.
 * @param LaunchVelocity The initial velocity vector for the projectile.
 * @param InitialProjectileSpeed The initial speed setting for the projectile component.
 */
void AFishingBobber::EnterFlyingState_Physics()
{
	UE_LOG(LogFishingSystemBobber, Log, TEXT("%s Entering Flying State (Physics ON)."), *GetName());
	if (BobberMeshComponent)
	{
		BobberMeshComponent->SetSimulatePhysics(true); // Ensure it's on
		// During flight, you might want different collision or damping
		BobberMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); // It needs to collide with world
		BobberMeshComponent->SetCollisionProfileName(TEXT("PhysicsActor")); // Or a custom "FlyingBobber" profile
		// Lower damping for longer flight
		BobberMeshComponent->SetLinearDamping(0.1f); // Tune this for air resistance
		BobberMeshComponent->SetAngularDamping(0.1f);
		// BobberMeshComponent->SetMassOverrideInKg(NAME_None, 0.1f, true); // Ensure mass is set for flight
		// IntendedMass = 0.1f;
		BobberMeshComponent->WakeRigidBody(); // Ensure physics body is awake
	}
}

// OnBobberHit still relevant for knowing when to transition out of Flying state
void AFishingBobber::OnBobberHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (CurrentState == EBobberState::Flying)
	{
		UE_LOG(LogFishingSystemBobber, Log, TEXT("%s hit %s while Flying (Physics). Transitioning to Idle."), *GetName(), OtherActor ? *OtherActor->GetName() : TEXT("World"));
		SetBobberState(EBobberState::Idle); // Or InWater, etc.
	}
}