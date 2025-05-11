// Fill out your copyright notice in the Description page of Project Settings.


#include "Animation/AnimNotify_ExecuteFishingLaunch.h"
#include "CharacterFishingComponent.h" // Your component
#include "FishingLogChannels.h"
#include "FishingRod.h"
#include "GameFramework/Actor.h"       // For GetOwner()
#include "Logging/MessageLog.h"        // For FMessageLog
#include "Misc/UObjectToken.h"         // For FUObjectToken in message log

// Optional: Override GetNotifyName_Implementation for a cleaner display in the montage editor track
FString UAnimNotify_ExecuteFishingLaunch::GetNotifyName_Implementation() const
{
	return TEXT("Execute Fishing Rod Launch");
}

void UAnimNotify_ExecuteFishingLaunch::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor) return;

	UCharacterFishingComponent* FishingComp = OwnerActor->FindComponentByClass<UCharacterFishingComponent>();
	if (FishingComp)
	{
		// Check if the component is actually trying to cast.
		// This notify might fire if the animation is played for other reasons.
		if (FishingComp->GetEquippedFishingRod() && FishingComp->GetEquippedFishingRod()->bIsPreparingToCast)
		{
			FishingComp->ExecuteLaunchFromAnimation();
		}
		else
		{
			// Log a verbose message if the notify fired but rod wasn't preparing (e.g., anim played without input)
			UE_LOG(LogFishingSystemInput, Verbose, TEXT("AnimNotify_ExecuteFishingLaunch: Fired on %s, but rod was not in bIsPreparingToCast state."), *OwnerActor->GetName());
		}
	}
	else
	{
		// This is a more critical setup error if the notify is on a character that *should* have the component
		UE_LOG(LogFishingSystemSetup, Warning, TEXT("AnimNotify_ExecuteFishingLaunch: Fired on %s, but CharacterFishingComponent was not found on the owner."), *OwnerActor->GetName());
	}
}