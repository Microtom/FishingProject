// FishingLineComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "FishingLineComponent.generated.h"

// Forward declarations
class UProceduralMeshComponent;
class UMaterialInterface;
// class AFishingBobber; // No longer needed here as line doesn't spawn/manage bobbers

USTRUCT(BlueprintType)
struct FVerletPoint
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VerletPoint")
    FVector Position;

    FVector OldPosition;
    FVector Acceleration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VerletPoint", meta = (ClampMin = "0.001"))
    float Mass;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VerletPoint")
    bool bIsFixed;

    FVerletPoint()
        : Position(FVector::ZeroVector)
        , OldPosition(FVector::ZeroVector)
        , Acceleration(FVector::ZeroVector)
        , Mass(0.02f)
        , bIsFixed(false)
    {}

    FVerletPoint(const FVector& InPosition, float InMass = 0.02f, bool bInIsFixed = false)
        : Position(InPosition)
        , OldPosition(InPosition)
        , Acceleration(FVector::ZeroVector)
        , Mass(InMass)
        , bIsFixed(bInIsFixed)
    {}

    void Integrate(float DeltaTime, float DampingFactor, const FVector& Gravity)
    {
        if (bIsFixed)
        {
            Acceleration = FVector::ZeroVector;
            return;
        }
        Acceleration += Gravity;
        FVector Velocity = Position - OldPosition;
        OldPosition = Position;
        Position += Velocity * (1.0f - DampingFactor) + Acceleration * DeltaTime * DeltaTime;
        Acceleration = FVector::ZeroVector;
    }

    void AddForce(const FVector& Force)
    {
        if (bIsFixed || Mass < KINDA_SMALL_NUMBER) return;
        Acceleration += Force / Mass;
    }
};


UCLASS(ClassGroup=(Fishing), meta=(BlueprintSpawnableComponent, DisplayName="Fishing Line"), Blueprintable, BlueprintType)
class FISHINGPROJECT_API UFishingLineComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UFishingLineComponent();

    //~ Begin UActorComponent Interface
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    //~ End UActorComponent Interface

    //~ Begin USceneComponent Interface
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    //~ End USceneComponent Interface

public:
    USceneComponent* GetResolvedAttachEndComponent() const; // This will now simply return EndAttachmentComponent.Get()
    
    // --- ATTACHMENT PROPERTIES (Simplified: Rod will set these) ---
    // This is the single point of truth for what the line's end is attached to.
    // Set by AFishingRod via AttachCableEndTo().
    UPROPERTY(Transient) // Transient because it's dynamically set by the rod, not saved with the line component itself.
    TWeakObjectPtr<USceneComponent> EndAttachmentComponent;

    UPROPERTY(Transient)
    FName EndAttachmentSocketName;

    /** If EndAttachmentComponent is not specified, this is the relative offset of the end point from this component's origin.
     *  Used for a free-hanging line if nothing is attached. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Attachment", meta=(EditCondition="EndAttachmentComponent==nullptr"))
    FVector FreeEndRelativeOffset; // Renamed from EndLocationOffset for clarity

    // --- REMOVED ATTACHMENT PROPERTIES ---
    // UPROPERTY() TWeakObjectPtr<AActor> AttachEndToActor; // REMOVED
    // UPROPERTY() FName AttachEndToComponentOnActorName; // REMOVED
    // UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> AttachEndToComponent_Internal; // REMOVED (use EndAttachmentComponent directly)
    // UPROPERTY() TWeakObjectPtr<USceneComponent> AttachEndToComponent; // This was also redundant/confusing with other names, consolidated to EndAttachmentComponent
    // UPROPERTY() FName AttachEndToSocketName; // Consolidated to EndAttachmentSocketName
    // UPROPERTY() FVector EndLocation; // Replaced by FreeEndRelativeOffset logic

    // --- REMOVED BOBBER SPAWNING PROPERTIES ---
    // UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Bobber") TSubclassOf<AFishingBobber> BobberClassToSpawn; // REMOVED
    // UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Bobber") bool bAutoSpawnAndAttachBobber; // REMOVED
    
    // --- CABLE PARAMETERS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Parameters", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float TargetCableLength;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Parameters", meta = (ClampMin = "0.1", UIMin = "0.1"))
    float DesiredSegmentLength;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cable|Parameters")
    int32 NumSegments;

    /** Effective mass to simulate for the particle at the attached end. This makes the line sag more appropriately if a bobber is attached. Multiplies DefaultParticleMass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable Particles", meta = (ClampMin = "1.0", UIMin = "1.0", EditCondition = "EndAttachmentComponent != nullptr", EditConditionHides))
    float AttachedEndMassMultiplier;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Physics", meta = (ClampMin = "1", UIMin = "1"))
    int32 SolverIterations;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Physics", meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax="1.0", UIMax="1.0"))
    float StiffnessFactor; 

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Physics", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float DampingFactor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Physics")
    float CableGravityScale;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Physics")
    float DefaultParticleMass;

    // --- BEZIER PROPERTIES ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Bezier")
    bool bUseBezierInitialization;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Bezier", meta = (EditCondition = "bUseBezierInitialization", ClampMin="0.0", UIMin="0.0"))
    float BezierSagMagnitude;

    // --- RENDERING PROPERTIES ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Rendering", meta = (ClampMin = "0.1", UIMin = "0.1"))
    float CableWidth;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Rendering")
    TObjectPtr<UMaterialInterface> CableMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Rendering")
    int32 MeshTessellation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cable|Rendering")
    bool bSmoothNormals;


    // --- PUBLIC FUNCTIONS ---
    // UFUNCTION(BlueprintCallable, Category = "Cable") void SetAttachEndTo(USceneComponent* EndComponent, FName EndSocketName, FVector RelativeEndLocation = FVector::ZeroVector); // REMOVED - Use AttachCableEndTo

    UFUNCTION(BlueprintCallable, Category = "Cable")
    void SetCableLength(float Length);

    UFUNCTION(BlueprintPure, Category = "Cable")
    float GetCurrentCableLength() const { return TargetCableLength; }

    UFUNCTION(BlueprintPure, Category = "Cable")
    TArray<FVector> GetParticleLocations() const; // Keep GetParticles() const TArray<FVerletPoint>& if needed by rod for forces

    /**
     * Programmatically sets the SceneComponent (e.g., a spawned bobber's root) to attach the end of the cable to.
     * This is the primary method for the FishingRod to attach the line to a bobber.
     * @param NewEndAttachment The component to attach to. If nullptr, the cable end becomes free.
     * @param NewSocketName Optional socket name on the NewEndAttachment.
     */
    UFUNCTION(BlueprintCallable, Category = "Cable")
    void AttachCableEndTo(USceneComponent* NewEndAttachment, FName NewSocketName = NAME_None);

    // --- REMOVED BOBBER FUNCTIONS ---
    // UFUNCTION(BlueprintCallable, Category = "Cable|Bobber") AFishingBobber* SpawnAndAttachBobber(); // REMOVED
    // UFUNCTION(BlueprintCallable, Category = "Cable|Bobber") void DetachAndDestroyManagedBobber(); // REMOVED
    // UFUNCTION(BlueprintPure, Category = "Cable") AFishingBobber* GetManagedBobber() const; // REMOVED

    // --- REMOVED ALTERNATE ATTACHMENT FUNCTIONS (as we simplify to one method for rod) ---
    // UFUNCTION(BlueprintCallable, Category = "Cable") void SetAttachEndToComponent(USceneComponent* EndComponent, FName InSocketName = NAME_None); // REMOVED
    // UFUNCTION(BlueprintCallable, Category = "Cable") void SetAttachEndToActor(AActor* EndActor, FName SpecificComponentName = NAME_None, FName InSocketName = NAME_None); // REMOVED
    
protected:
    void RebuildParticles();
    void SimulateCable(float DeltaTime);
    void SolveConstraints(float DeltaTime);
    void UpdateCableMesh();
    
    

    FVector EvaluateCubicBezier(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float t) const;
    void GeneratePointsOnBezier(TArray<FVector>& OutPoints, const FVector& P0_World, const FVector& P3_World, int32 PointsToGenerate);

    FTransform GetStartTransform() const;
    FTransform GetFreeEndPointTransform() const;
    FTransform GetAttachedEndPointTransform() const;

private:
    UPROPERTY(Transient)
    TObjectPtr<UProceduralMeshComponent> ProceduralMesh;

    TArray<FVerletPoint> Particles;
    FBoxSphereBounds LocalBounds;
    
    // UPROPERTY(Transient, DuplicateTransient) TObjectPtr<AFishingBobber> ManagedBobber; // REMOVED
    
    bool bRequiresParticleRebuild;
};