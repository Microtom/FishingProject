#pragma once
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ExecuteFishingLaunch.generated.h"

UCLASS(DisplayName="Execute Fishing Rod Launch") // Add a nice DisplayName for the editor
class FISHINGPROJECT_API UAnimNotify_ExecuteFishingLaunch : public UAnimNotify
{
	GENERATED_BODY()
public:
	virtual FString GetNotifyName_Implementation() const override; // For a better name in the editor track
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};