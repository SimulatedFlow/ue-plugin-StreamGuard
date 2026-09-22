// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuard.h"
#include "StreamGuardLog.h"

DEFINE_LOG_CATEGORY(LogStreamGuard);

#define LOCTEXT_NAMESPACE "FStreamGuardModule"

void FStreamGuardModule::StartupModule()
{
	UE_LOG(LogStreamGuard, Log, TEXT("StreamGuard started."));
}

void FStreamGuardModule::ShutdownModule()
{
	UE_LOG(LogStreamGuard, Log, TEXT("StreamGuard shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FStreamGuardModule, StreamGuard)
