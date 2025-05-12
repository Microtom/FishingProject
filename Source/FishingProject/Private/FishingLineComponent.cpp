// FishingLineComponent.cpp

#include "FishingLineComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
// #include "FishingBobber.h" // No longer needed here
#include "FishingBobber.h"
#include "FishingLogChannels.h"
// #include "PrimitiveSceneProxy.h" // Keep if you were planning advanced rendering

// --- CONSTRUCTOR ---
UFishingLineComponent::UFishingLineComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;

    TargetCableLength = 100.0f; // Initial default, rod will override
    DesiredSegmentLength = 10.0f;
    NumSegments = 0;
    SolverIterations = 10;
    StiffnessFactor = 0.85f; // Made a bit stiffer by default
    DampingFactor = 0.1f;  // Increased damping slightly
    CableGravityScale = 1.0f;
    DefaultParticleMass = 0.01f; // Made lighter by default

    bUseBezierInitialization = false;
    BezierSagMagnitude = 0.2f;

    DefaultParticleMass = 0.01f;
    AttachedEndMassMultiplier = 20.0f; // Give it a default value, e.g., 20x the normal particle mass
    
    CableWidth = 2.0f;
    MeshTessellation = 4;
    bSmoothNormals = true;

    FreeEndRelativeOffset = FVector(0,0,-100.0f); // Default free end hangs down a bit

    bRequiresParticleRebuild = true;
    LocalBounds = FBoxSphereBounds(ForceInit);

    EndAttachmentComponent = nullptr;
    EndAttachmentSocketName = NAME_None;

    UE_LOG(LogFishingSystemSetup, Log, TEXT("UFishingLineComponent Constructor: Initialized."));
}

// --- UE LIFECYCLE ---
void UFishingLineComponent::OnRegister()
{
    Super::OnRegister();
    if (!ProceduralMesh)
    {
        ProceduralMesh = NewObject<UProceduralMeshComponent>(this, TEXT("CableProceduralMesh"));
        ProceduralMesh->SetupAttachment(this);
        ProceduralMesh->RegisterComponent();
        ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void UFishingLineComponent::OnUnregister()
{
    if (ProceduralMesh)
    {
        ProceduralMesh->DestroyComponent();
        ProceduralMesh = nullptr;
    }
    Super::OnUnregister();
}

void UFishingLineComponent::BeginPlay()
{
    Super::BeginPlay();
    bRequiresParticleRebuild = true; // Force a rebuild on BeginPlay to use latest properties

    // REMOVED Bobber Spawning Logic from here
    // if (bAutoSpawnAndAttachBobber && BobberClassToSpawn)
    // {
    //     SpawnAndAttachBobber();
    // }
    UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': BeginPlay. Initial TargetCableLength: %.1f"), *GetName(), TargetCableLength);
}

void UFishingLineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // REMOVED Bobber Destruction Logic from here
    // DetachAndDestroyManagedBobber();

    Particles.Empty();
    if (ProceduralMesh)
    {
        ProceduralMesh->ClearAllMeshSections();
    }
    Super::EndPlay(EndPlayReason);
}

void UFishingLineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bRequiresParticleRebuild)
    {
        UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': Tick - bRequiresParticleRebuild is TRUE. Calling RebuildParticles."), *GetName());
        RebuildParticles();
        // bRequiresParticleRebuild is set to false inside RebuildParticles upon successful completion
        if (Particles.Num() == 0 && TargetCableLength > 0) { // If rebuild failed to create particles but should have
             UE_LOG(LogFishingSystemLine, Error, TEXT("UFishingLineComponent '%s': Tick - RebuildParticles resulted in 0 particles despite TargetCableLength > 0. Line will not simulate."), *GetName());
             return;
        }
    }
    
    if (Particles.Num() < 2)
    {
        if (ProceduralMesh && ProceduralMesh->GetNumSections() > 0)
        {
            ProceduralMesh->ClearMeshSection(0);
        }
        return;
    }

    SimulateCable(DeltaTime);
    SolveConstraints(DeltaTime);
    UpdateCableMesh();
}

FBoxSphereBounds UFishingLineComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    if (Particles.Num() > 0)
    {
        FBox Box(ForceInit);
        for (const FVerletPoint& Particle : Particles)
        {
            Box += Particle.Position;
        }
        FBox LocalBox = Box.TransformBy(LocalToWorld.Inverse());
        return FBoxSphereBounds(LocalBox);
    }
    return FBoxSphereBounds(FSphere(FVector::ZeroVector, CableWidth)).TransformBy(LocalToWorld);
}

// --- PUBLIC API ---
// REMOVED: void UFishingLineComponent::SetAttachEndTo(...) - Use AttachCableEndTo

void UFishingLineComponent::SetCableLength(float Length)
{
    // Ensure length is at least enough for one segment, or a very small positive value if desired segment length is also tiny.
    float MinPracticalLength = FMath::Max(DesiredSegmentLength * 0.5f, 1.0f);
    float NewLength = FMath::Max(Length, MinPracticalLength);

    if (!FMath::IsNearlyEqual(TargetCableLength, NewLength, 0.1f) || (Particles.Num() == 0 && NewLength > 0))
    {
        UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': SetCableLength changing TargetCableLength from %.1f to %.1f. Setting bRequiresParticleRebuild=true."),
            *GetName(), TargetCableLength, NewLength);
        TargetCableLength = NewLength;
        bRequiresParticleRebuild = true;
    }
}

TArray<FVector> UFishingLineComponent::GetParticleLocations() const
{
    TArray<FVector> Locations;
    Locations.Reserve(Particles.Num());
    for (const FVerletPoint& Particle : Particles)
    {
        Locations.Add(Particle.Position);
    }
    return Locations;
}

void UFishingLineComponent::AttachCableEndTo(USceneComponent* NewEndAttachment, FName NewSocketName)
{
    UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': AttachCableEndTo called. Target: Component '%s', Socket: '%s'."),
       *GetName(), NewEndAttachment ? *NewEndAttachment->GetName() : TEXT("NULL"), *NewSocketName.ToString());

    // Check if attachment is actually changing to avoid unnecessary rebuilds
    if (EndAttachmentComponent.Get() != NewEndAttachment || EndAttachmentSocketName != NewSocketName)
    {
        EndAttachmentComponent = NewEndAttachment;
        EndAttachmentSocketName = NewSocketName;
        bRequiresParticleRebuild = true;
        UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': Attachment changed. bRequiresParticleRebuild=true."), *GetName());
    } else {
        UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': AttachCableEndTo - No change in attachment. Skipping rebuild trigger."), *GetName());
    }
}

// REMOVED: AFishingBobber* UFishingLineComponent::SpawnAndAttachBobber()
// REMOVED: void UFishingLineComponent::DetachAndDestroyManagedBobber()
// REMOVED: AFishingBobber* UFishingLineComponent::GetManagedBobber() const
// REMOVED: void UFishingLineComponent::SetAttachEndToComponent(...)
// REMOVED: void UFishingLineComponent::SetAttachEndToActor(...)


// --- INTERNAL LOGIC ---

FTransform UFishingLineComponent::GetStartTransform() const
{
    return GetComponentTransform();
}

FTransform UFishingLineComponent::GetFreeEndPointTransform() const
{
    // If not attached, the end is relative to this component's transform
    return FTransform(GetComponentTransform().TransformPosition(FreeEndRelativeOffset));
}

FTransform UFishingLineComponent::GetAttachedEndPointTransform() const
{
    USceneComponent* ResolvedEndComp = GetResolvedAttachEndComponent(); // Use the simplified getter
    if (ResolvedEndComp)
    {
        if (EndAttachmentSocketName != NAME_None && ResolvedEndComp->DoesSocketExist(EndAttachmentSocketName))
        {
            return ResolvedEndComp->GetSocketTransform(EndAttachmentSocketName);
        }
        return ResolvedEndComp->GetComponentTransform();
    }
    // Fallback, should ideally not be reached if called correctly
    UE_LOG(LogFishingSystemLine, Warning, TEXT("UFishingLineComponent '%s': GetAttachedEndPointTransform - No resolved end component. Returning FreeEndPointTransform."), *GetName());
    return GetFreeEndPointTransform();
}


void UFishingLineComponent::RebuildParticles()
{
    UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': RebuildParticles START. Current Particle Count: %d"), *GetName(), Particles.Num());

    if (DesiredSegmentLength <= 0.f)
    {
        UE_LOG(LogFishingSystemLine, Error, TEXT("UFishingLineComponent '%s': RebuildParticles - DesiredSegmentLength is <= 0 (%.2f). Cannot rebuild."), *GetName(), DesiredSegmentLength);
        Particles.Empty();
        bRequiresParticleRebuild = false;
        return;
    }

    const int32 NewNumSegments = FMath::Max(1, FMath::CeilToInt(TargetCableLength / DesiredSegmentLength));
    NumSegments = NewNumSegments;
    const int32 NumPoints = NumSegments + 1;

    UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': RebuildParticles - TargetCableLength=%.1f, DesiredSegmentLength=%.1f -> NewNumSegments=%d, NumPoints=%d"),
        *GetName(), TargetCableLength, DesiredSegmentLength, NewNumSegments, NumPoints);

    Particles.Reset(NumPoints);

    FTransform StartTM_World = GetStartTransform();
    USceneComponent* ResolvedEndComp = GetResolvedAttachEndComponent();
    FTransform EndTM_World = ResolvedEndComp ? GetAttachedEndPointTransform() : GetFreeEndPointTransform();

    FVector P0_World = StartTM_World.GetLocation();
    FVector P3_World_Target = EndTM_World.GetLocation();

    UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': RebuildParticles - StartPos: %s, TargetEndPos: %s. ResolvedEndComp: %s"),
        *GetName(), *P0_World.ToString(), *P3_World_Target.ToString(), ResolvedEndComp ? *ResolvedEndComp->GetName() : TEXT("NULL"));

    TArray<FVector> InitialWorldPositions;
    InitialWorldPositions.Reserve(NumPoints);

    if (bUseBezierInitialization && NumPoints >= 2)
    {
        GeneratePointsOnBezier(InitialWorldPositions, P0_World, P3_World_Target, NumPoints);
        UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Used Bezier initialization for %d points."), *GetName(), NumPoints);
    }
    else
    {
        for (int32 i = 0; i < NumPoints; ++i)
        {
            float Alpha = (NumPoints > 1) ? (float)i / (float)(NumPoints - 1) : 0.0f;
            InitialWorldPositions.Add(FMath::Lerp(P0_World, P3_World_Target, Alpha));
        }
        UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Used Linear interpolation for %d points."), *GetName(), NumPoints);
    }
    
    for (int32 i = 0; i < NumPoints; ++i)
    {
        float CurrentParticleMass = DefaultParticleMass;
        FVerletPoint NewPoint(InitialWorldPositions[i], CurrentParticleMass);
        NewPoint.bIsFixed = false; 

        if (i == 0) // First particle (rod tip)
        {
            NewPoint.bIsFixed = true;
            UE_LOG(LogFishingSystemLine, VeryVerbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Point %d (Start) set to FIXED."), *GetName(), i);
        }
        else if (i == NumPoints - 1 && ResolvedEndComp) // Last particle AND attached
        {
            // The last particle is NOT fixed if attached to a dangling bobber (whose physics is off).
            // It IS fixed if attached to something with its own active physics.
            NewPoint.bIsFixed = false; // Assume not fixed initially for dangling case
            AFishingBobber* AttachedBobber = ResolvedEndComp->GetOwner() ? Cast<AFishingBobber>(ResolvedEndComp->GetOwner()) : nullptr;
            if (AttachedBobber && AttachedBobber->GetCurrentBobberState() == EBobberState::DanglingAtTip)
            {
                // It's a dangling bobber, so line particle is NOT fixed.
                UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Point %d (End Attached to Dangling Bobber) set to NOT FIXED."), *GetName(), i);
            }
            else if (AttachedBobber) // Bobber in another state (e.g. Flying)
            {
                 NewPoint.bIsFixed = true; // Bobber might be driving its own position
                 UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Point %d (End Attached to Bobber in state %s) set to FIXED."), *GetName(), i, *UEnum::GetValueAsString(AttachedBobber->GetCurrentBobberState()));
            }
            else // Attached to something else that isn't a bobber
            {
                 NewPoint.bIsFixed = true; // Assume other attachments are fixed targets
                 UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Point %d (End Attached to Non-Bobber %s) set to FIXED."), *GetName(), i, *ResolvedEndComp->GetName());
            }
            
            NewPoint.Mass = DefaultParticleMass * FMath::Max(1.0f, AttachedEndMassMultiplier);
            UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Point %d (End Attached) mass set to %.4f. IsFixed: %s"),
                   *GetName(), i, NewPoint.Mass, NewPoint.bIsFixed ? TEXT("TRUE") : TEXT("FALSE"));
        }
        Particles.Add(NewPoint);
    }
    
    if (Particles.Num() > 0)
    {
        Particles[0].Position = P0_World;
        Particles[0].OldPosition = P0_World;
    }

    // Snap the last particle's initial position only if it's truly fixed to a target that dictates its position
    if (Particles.Num() > 1 && Particles.Last().bIsFixed && ResolvedEndComp)
    {
        UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Snapping Last Particle (%d) to %s (TargetEndPos) because it's marked as bIsFixed."),
           *GetName(), Particles.Num() - 1, *P3_World_Target.ToString());
        Particles.Last().Position = P3_World_Target;
        Particles.Last().OldPosition = P3_World_Target;
    }
    else if (Particles.Num() > 1 && !Particles.Last().bIsFixed && ResolvedEndComp)
    {
        // If the last particle is not fixed (e.g., attached to a dangling bobber),
        // initialize its position, but it will be free to move during simulation.
        // P3_World_Target is still a good initial guess from the Lerp/Bezier.
        UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': RebuildParticles - Initializing Last Particle (%d) to %s. It is NOT fixed and will simulate."),
           *GetName(), Particles.Num()-1, *Particles.Last().Position.ToString()); // Use its already set InitialWorldPosition
        // No need to reset OldPosition if we want it to start from rest from its initial calculated position
        Particles.Last().OldPosition = Particles.Last().Position;
    }


    bRequiresParticleRebuild = false;
    UE_LOG(LogFishingSystemLine, Log, TEXT("UFishingLineComponent '%s': RebuildParticles END. New Particle Count: %d. bRequiresParticleRebuild is now false."), *GetName(), Particles.Num());
}

void UFishingLineComponent::SimulateCable(float DeltaTime)
{
    if (Particles.Num() < 1 || DeltaTime <= 0.f) return;

    const FVector Gravity = FVector(0, 0, GetWorld()->GetGravityZ() * CableGravityScale);

    FVector CurrentStartPos = GetStartTransform().GetLocation();
    Particles[0].Position = CurrentStartPos;
    Particles[0].OldPosition = Particles[0].Position; 

    USceneComponent* ResolvedEndComp = GetResolvedAttachEndComponent();
    bool bLastParticleIsTrulyFixedToExternal = false; 

    if (Particles.Num() > 1 && ResolvedEndComp)
    {
        if (Particles.Last().bIsFixed) // Check the particle's own bIsFixed flag
        {
            bLastParticleIsTrulyFixedToExternal = true;
            FVector CurrentEndPos = GetAttachedEndPointTransform().GetLocation();
            Particles.Last().Position = CurrentEndPos;
            Particles.Last().OldPosition = Particles.Last().Position; 
            UE_LOG(LogFishingSystemLine, VeryVerbose, TEXT("%s SimulateCable: Last particle IS fixed to external. Pos: %s"), *GetName(), *CurrentEndPos.ToString());
        }
        else
        {
            UE_LOG(LogFishingSystemLine, VeryVerbose, TEXT("%s SimulateCable: Last particle is attached but NOT fixed (e.g. dangling bobber). It will integrate."), *GetName());
        }
    }

    int32 StartSimIndex = 1; 
    int32 EndSimIndex = Particles.Num();
    if (bLastParticleIsTrulyFixedToExternal) 
    {
        EndSimIndex = Particles.Num() - 1;
    }

    for (int32 i = StartSimIndex; i < EndSimIndex; ++i)
    {
        Particles[i].Integrate(DeltaTime, DampingFactor, Gravity);
    }
}


void UFishingLineComponent::SolveConstraints(float DeltaTime)
{
    if (Particles.Num() < 2 || NumSegments == 0) return;

    const float CurrentDesiredSegmentLength = TargetCableLength / FMath::Max(1, NumSegments);
    // UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': SolveConstraints START. TargetLen=%.1f, NumSegs=%d, CurrentDesiredSegLen=%.2f. Iterations=%d, Stiffness=%.2f"),
    //    *GetName(), TargetCableLength, NumSegments, CurrentDesiredSegmentLength, SolverIterations, StiffnessFactor);

    for (int32 Iter = 0; Iter < SolverIterations; ++Iter)
    {
        for (int32 i = 0; i < NumSegments; ++i)
        {
            FVerletPoint& P1 = Particles[i];
            FVerletPoint& P2 = Particles[i+1];

            FVector Delta = P2.Position - P1.Position;
            float CurrentLength = Delta.Size();

            if (CurrentLength < KINDA_SMALL_NUMBER) continue;

            float Error = CurrentLength - CurrentDesiredSegmentLength;
            FVector CorrectionDirection = Delta / CurrentLength;
            FVector Correction = CorrectionDirection * Error * StiffnessFactor;

            bool bP1IsCableStartFixedPoint = P1.bIsFixed; // Particle 0's bIsFixed should be true
            bool bP2IsCableEndFixedPoint = P2.bIsFixed;   // Last particle's bIsFixed should be true if attached

            // UE_LOG(LogFishingSystemLine, VeryVerbose, TEXT("UFishingLineComponent '%s': SolveConstraints Iter %d, Seg %d (P%d-P%d): Err=%.2f. P1(idx%d)_Fixed=%s, P2(idx%d)_Fixed=%s. CorrectionVec: %s"),
            //    *GetName(), Iter, i, i, i+1, Error,
            //    i, bP1IsCableStartFixedPoint ? TEXT("T") : TEXT("F"),
            //    i+1, bP2IsCableEndFixedPoint ? TEXT("T") : TEXT("F"),
            //    *Correction.ToString());
            
            float P1_MoveRatio, P2_MoveRatio;
            if (bP1IsCableStartFixedPoint && bP2IsCableEndFixedPoint) { P1_MoveRatio = 0.0f; P2_MoveRatio = 0.0f; }
            else if (bP1IsCableStartFixedPoint) { P1_MoveRatio = 0.0f; P2_MoveRatio = 1.0f; }
            else if (bP2IsCableEndFixedPoint) { P1_MoveRatio = 1.0f; P2_MoveRatio = 0.0f; }
            else { P1_MoveRatio = 0.5f; P2_MoveRatio = 0.5f; }

            if (P1_MoveRatio > 0.0f)
            {
                P1.Position += Correction * P1_MoveRatio;
            }
            if (P2_MoveRatio > 0.0f)
            {
                P2.Position -= Correction * P2_MoveRatio;
            }
        }
    }
    // UE_LOG(LogFishingSystemLine, Verbose, TEXT("UFishingLineComponent '%s': SolveConstraints END."), *GetName());
}

void UFishingLineComponent::UpdateCableMesh()
{
    if (!ProceduralMesh || Particles.Num() < 2 || CableWidth <= 0.f)
    {
        if (ProceduralMesh && ProceduralMesh->GetNumSections() > 0) ProceduralMesh->ClearMeshSection(0);
        return;
    }
    const FTransform WorldToLocal = GetComponentTransform().Inverse();
    TArray<FVector> LocalVertices;
    TArray<int32> Triangles;
    TArray<FVector> LocalNormals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> LocalTangents; // Not used yet, but good to have

    if (Particles.Num() < 2) return; // Need at least 2 particles for a segment

    FVector PrevParticlePos_World = Particles[0].Position;
    // Initial segment direction from first two particles
    FVector SegmentDirection_World = (Particles.Num() > 1) ? (Particles[1].Position - Particles[0].Position).GetSafeNormal() : GetForwardVector(); // Fallback
    if (SegmentDirection_World.IsNearlyZero()) SegmentDirection_World = GetForwardVector(); // Further fallback

    FVector PrevRight_World = FVector::CrossProduct(SegmentDirection_World, GetUpVector()).GetSafeNormal();
    if (PrevRight_World.IsNearlyZero()) PrevRight_World = FVector::CrossProduct(SegmentDirection_World, FVector::UpVector).GetSafeNormal();
    if (PrevRight_World.IsNearlyZero()) PrevRight_World = GetRightVector();


    float CurrentV = 0.0f;
    const float UVXScale = 1.0f; // Controls V coordinate tiling

    for (int32 i = 0; i < Particles.Num(); ++i)
    {
        const FVector& ParticlePos_World = Particles[i].Position;
        FVector CurrentSegmentDirection_World;

        if (i < Particles.Num() - 1) CurrentSegmentDirection_World = (Particles[i+1].Position - ParticlePos_World).GetSafeNormal();
        else if (Particles.Num() > 1) CurrentSegmentDirection_World = (ParticlePos_World - Particles[i-1].Position).GetSafeNormal();
        else CurrentSegmentDirection_World = SegmentDirection_World; // Use initial if only one particle somehow
        
        if (CurrentSegmentDirection_World.IsNearlyZero()) CurrentSegmentDirection_World = SegmentDirection_World; // Fallback


        // Calculate a consistent right vector along the cable
        FVector RightVector_World = FVector::CrossProduct(CurrentSegmentDirection_World, PrevRight_World).GetSafeNormal(); // This gives a vector mostly perpendicular to both
        RightVector_World = FVector::CrossProduct(RightVector_World, CurrentSegmentDirection_World).GetSafeNormal(); // This makes it truly perpendicular to CurrentSegmentDirection
        
        if(RightVector_World.IsNearlyZero() || !RightVector_World.IsNormalized()) RightVector_World = PrevRight_World; // Fallback if directions align
        else PrevRight_World = RightVector_World; // Update for next segment

        for (int32 Side = 0; Side < MeshTessellation; ++Side)
        {
            float Angle = ((float)Side / MeshTessellation) * 2.0f * PI;
            FVector Offset_World = RightVector_World.RotateAngleAxisRad(Angle, CurrentSegmentDirection_World) * (CableWidth * 0.5f);
            FVector VertexPos_World = ParticlePos_World + Offset_World;

            LocalVertices.Add(WorldToLocal.TransformPosition(VertexPos_World));
            LocalNormals.Add(WorldToLocal.TransformVectorNoScale(Offset_World.GetSafeNormal())); // Normals shouldn't be scaled
            UVs.Add(FVector2D( (float)Side / MeshTessellation, CurrentV));
        }
        CurrentV += (i > 0) ? UVXScale * FVector::Dist(ParticlePos_World, PrevParticlePos_World) / FMath::Max(DesiredSegmentLength, 1.0f) : 0.0f;
        PrevParticlePos_World = ParticlePos_World;
        SegmentDirection_World = CurrentSegmentDirection_World; // Update for next iteration's PrevRight calculation
    }

    for (int32 SegIdx = 0; SegIdx < Particles.Num() - 1; ++SegIdx)
    {
        for (int32 SideIdx = 0; SideIdx < MeshTessellation; ++SideIdx)
        {
            int32 TL = SegIdx * MeshTessellation + SideIdx;
            int32 TR = SegIdx * MeshTessellation + (SideIdx + 1) % MeshTessellation;
            int32 BL = (SegIdx + 1) * MeshTessellation + SideIdx;
            int32 BR = (SegIdx + 1) * MeshTessellation + (SideIdx + 1) % MeshTessellation;
            Triangles.Add(TL); Triangles.Add(BL); Triangles.Add(TR);
            Triangles.Add(TR); Triangles.Add(BL); Triangles.Add(BR);
        }
    }
    
    if (bSmoothNormals && LocalNormals.Num() == LocalVertices.Num() && Triangles.Num() > 0)
    {
        TArray<FVector> SmoothedLocalNormals;
        SmoothedLocalNormals.AddZeroed(LocalVertices.Num());
        for(int32 TriIdx = 0; TriIdx < Triangles.Num(); TriIdx += 3)
        {
            int32 V0Idx = Triangles[TriIdx + 0];
            int32 V1Idx = Triangles[TriIdx + 1];
            int32 V2Idx = Triangles[TriIdx + 2];
            FVector FaceNormal_Local = FVector::CrossProduct(LocalVertices[V1Idx] - LocalVertices[V0Idx], LocalVertices[V2Idx] - LocalVertices[V0Idx]).GetSafeNormal();
            SmoothedLocalNormals[V0Idx] += FaceNormal_Local;
            SmoothedLocalNormals[V1Idx] += FaceNormal_Local;
            SmoothedLocalNormals[V2Idx] += FaceNormal_Local;
        }
        for(int32 i=0; i<SmoothedLocalNormals.Num(); ++i) LocalNormals[i] = SmoothedLocalNormals[i].GetSafeNormal();
    }

    if (LocalVertices.Num() > 0 && Triangles.Num() > 0)
    {
        ProceduralMesh->CreateMeshSection(0, LocalVertices, Triangles, LocalNormals, UVs, TArray<FColor>(), LocalTangents, false);
        if (CableMaterial) ProceduralMesh->SetMaterial(0, CableMaterial);
    }
    else
    {
        if (ProceduralMesh && ProceduralMesh->GetNumSections() > 0) ProceduralMesh->ClearMeshSection(0);
    }
    
    if (LocalVertices.Num() > 0)
    {
        FBox Box(ForceInit);
        for(const FVector& P_Local : LocalVertices) Box += P_Local;
        LocalBounds = FBoxSphereBounds(Box);
    } else {
        LocalBounds = FBoxSphereBounds(ForceInit);
    }
    MarkRenderStateDirty(); // Important to tell the rendering system that bounds or mesh changed
}

USceneComponent* UFishingLineComponent::GetResolvedAttachEndComponent() const
{
    // This is now the single source of truth for the attached component.
    return EndAttachmentComponent.Get();
}

// --- BEZIER UTILITIES ---
FVector UFishingLineComponent::EvaluateCubicBezier(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float t) const
{
    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;
    FVector Point = uuu * P0;
    Point += 3 * uu * t * P1;
    Point += 3 * u * tt * P2;
    Point += ttt * P3;
    return Point;
}

void UFishingLineComponent::GeneratePointsOnBezier(TArray<FVector>& OutPoints, const FVector& P0_World, const FVector& P3_World, int32 PointsToGenerate)
{
    OutPoints.Empty(PointsToGenerate);
    if (PointsToGenerate < 2)
    {
        if (PointsToGenerate == 1) OutPoints.Add(P0_World);
        return;
    }
    FVector Dir_P0_P3 = (P3_World - P0_World);
    float Dist_P0_P3 = Dir_P0_P3.Size();
    if (Dist_P0_P3 > KINDA_SMALL_NUMBER) Dir_P0_P3 /= Dist_P0_P3; else Dir_P0_P3 = FVector(1,0,0);
    FVector DownVector = FVector(0, 0, -1.0f);
    if (FMath::Abs(FVector::DotProduct(Dir_P0_P3, DownVector)) > 0.95f)
    {
        DownVector = FVector::CrossProduct(Dir_P0_P3, FVector(0,1,0)).GetSafeNormal() * -1.0f;
        if(DownVector.IsNearlyZero()) DownVector = FVector::CrossProduct(Dir_P0_P3, FVector(1,0,0)).GetSafeNormal() * -1.0f;
    }
    float SagOffsetDist = Dist_P0_P3 * BezierSagMagnitude;
    FVector P1_World = P0_World + Dir_P0_P3 * (Dist_P0_P3 * 0.25f) + DownVector * SagOffsetDist;
    FVector P2_World = P3_World - Dir_P0_P3 * (Dist_P0_P3 * 0.25f) + DownVector * SagOffsetDist;
    if (TargetCableLength > Dist_P0_P3 * 1.1f)
    {
        float ExcessLengthFactor = (TargetCableLength / FMath::Max(Dist_P0_P3, 1.0f)) - 1.0f;
        P1_World += DownVector * SagOffsetDist * ExcessLengthFactor * 2.0f;
        P2_World += DownVector * SagOffsetDist * ExcessLengthFactor * 2.0f;
    }
    for (int32 i = 0; i < PointsToGenerate; ++i)
    {
        float t = (PointsToGenerate > 1) ? (float)i / (float)(PointsToGenerate - 1) : 0.0f;
        OutPoints.Add(EvaluateCubicBezier(P0_World, P1_World, P2_World, P3_World, t));
    }
}