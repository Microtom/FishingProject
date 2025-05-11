// FishingBlueprintLogLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FishingLogChannels.h" // Use the new header name
#include "FishingBlueprintLogLibrary.generated.h"

// Forward declaration from FishingLogChannels.h
enum class EFishingLogCategory : uint8;
enum class EFishingLogVerbosity : uint8;

UCLASS() // Add YOURPROJECT_API if this is in a module that needs to be exported
class UFishingBlueprintLogLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Logs a message to a specific FishingSystem log channel.
	 * @param WorldContextObject Provides the world context.
	 * @param Category The FishingSystem log category to use.
	 * @param Verbosity The logging severity level.
	 * @param Message The string message to log.
	 * @param bPrintToScreen Should the message also be printed to the screen?
	 * @param ScreenMessageColor Color of the message if printed to screen.
	 * @param ScreenMessageDuration Duration the message stays on screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "FishingSystem|Logging", meta = (WorldContext = "WorldContextObject", Keywords = "log print debug fishing", AdvancedDisplay = "bPrintToScreen,ScreenMessageColor,ScreenMessageDuration"))
	static void LogToFishingChannel(
		const UObject* WorldContextObject,
		EFishingLogCategory Category,
		EFishingLogVerbosity Verbosity,
		const FString& Message = FString(TEXT("Hello Fishing System!")),
		bool bPrintToScreen = false,
		FLinearColor ScreenMessageColor = FLinearColor::Green,
		float ScreenMessageDuration = 3.f);
};