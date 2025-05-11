// FishingLogChannels.h
#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// --- Declare FishingSystem Log Categories ---
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemGeneral, Log, All);     // For general messages
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemRod, Log, All);         // For Fishing Rod specific logs
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemBobber, Log, All);      // For Fishing Bobber specific logs
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemLine, Log, All);        // For Fishing Line/Cable specific logs
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemInteraction, Log, All); // For interactions like fish biting, etc.
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemSetup, Log, All);       // For setup, initialization, component checks
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemComponent, Log, All);   // For player component checks
DECLARE_LOG_CATEGORY_EXTERN(LogFishingSystemInput, Log, All);		// For input checks

// --- Blueprint Enum for Selecting Category ---
UENUM(BlueprintType)
enum class EFishingLogCategory : uint8
{
	General     UMETA(DisplayName = "General"),
	Rod         UMETA(DisplayName = "Rod"),
	Bobber      UMETA(DisplayName = "Bobber"),
	Line        UMETA(DisplayName = "Line"),
	Interaction UMETA(DisplayName = "Interaction"),
	Setup       UMETA(DisplayName = "Setup"),
	Component   UMETA(DisplayName = "Component"),
	Input	    UMETA(DisplayName = "Input")
};

// --- Blueprint Enum for Selecting Verbosity (Kept generic as ESolaraqLogVerbosity was fine) ---
UENUM(BlueprintType)
enum class EFishingLogVerbosity : uint8
{
	Fatal       UMETA(DisplayName = "Fatal"),
	Error       UMETA(DisplayName = "Error"),
	Warning     UMETA(DisplayName = "Warning"),
	Display     UMETA(DisplayName = "Display"),
	Log         UMETA(DisplayName = "Log"),
	Verbose     UMETA(DisplayName = "Verbose"),
	VeryVerbose UMETA(DisplayName = "Very Verbose")
};