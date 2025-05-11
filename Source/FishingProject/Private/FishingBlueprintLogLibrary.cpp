// FishingBlueprintLogLibrary.cpp
#include "FishingBlueprintLogLibrary.h" // Use the new header name
#include "FishingLogChannels.h"         // Use the new header name
#include "Engine/Engine.h"
#include "Logging/LogVerbosity.h"
#include "Logging/LogMacros.h"
#include "Kismet/KismetSystemLibrary.h"

// Helper function to convert OUR Blueprint enum to the ENGINE's ELogVerbosity::Type
static ELogVerbosity::Type ConvertFishingVerbosity(EFishingLogVerbosity Verbosity)
{
    return static_cast<ELogVerbosity::Type>(Verbosity);
}

void UFishingBlueprintLogLibrary::LogToFishingChannel(
    const UObject* WorldContextObject,
    EFishingLogCategory Category,
    EFishingLogVerbosity Verbosity,
    const FString& Message,
    bool bPrintToScreen,
    FLinearColor ScreenMessageColor,
    float ScreenMessageDuration)
{
    FName LogCategoryName;
    switch (Category)
    {
        case EFishingLogCategory::General:
            LogCategoryName = LogFishingSystemGeneral.GetCategoryName();
            break;
        case EFishingLogCategory::Rod:
            LogCategoryName = LogFishingSystemRod.GetCategoryName();
            break;
        case EFishingLogCategory::Bobber:
            LogCategoryName = LogFishingSystemBobber.GetCategoryName();
            break;
        case EFishingLogCategory::Line:
            LogCategoryName = LogFishingSystemLine.GetCategoryName();
            break;
        case EFishingLogCategory::Interaction:
            LogCategoryName = LogFishingSystemInteraction.GetCategoryName();
            break;
        case EFishingLogCategory::Setup:
            LogCategoryName = LogFishingSystemSetup.GetCategoryName();
            break;
        case EFishingLogCategory::Component:
            LogCategoryName = LogFishingSystemComponent.GetCategoryName();
            break;
        default:
            LogCategoryName = LogFishingSystemGeneral.GetCategoryName();
            FMsg::Logf(UE_LOG_SOURCE_FILE(__FILE__), __LINE__, LogFishingSystemGeneral.GetCategoryName(), ELogVerbosity::Warning, TEXT("LogToFishingChannel: Invalid Category provided. Falling back to General. Message: %s"), *Message);
    }

    ELogVerbosity::Type ActualVerbosity = ConvertFishingVerbosity(Verbosity);
    FMsg::Logf(UE_LOG_SOURCE_FILE(__FILE__), __LINE__, LogCategoryName, ActualVerbosity, TEXT("%s"), *Message);

    if (bPrintToScreen && GEngine && WorldContextObject)
    {
        FString Prefix = FString::Printf(TEXT("[%s][%s] "), *LogCategoryName.ToString(), ToString(ActualVerbosity));
        FString FinalMessage = Prefix + Message;
        UObject* MutableWorldContext = const_cast<UObject*>(WorldContextObject);
        UKismetSystemLibrary::PrintString(MutableWorldContext, FinalMessage, true, true, ScreenMessageColor, ScreenMessageDuration);
    }
}