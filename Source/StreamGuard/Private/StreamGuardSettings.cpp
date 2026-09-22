// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuardSettings.h"

UStreamGuardSettings::UStreamGuardSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("StreamGuard");
}

FName UStreamGuardSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UStreamGuardSettings::GetSectionName() const
{
	return TEXT("StreamGuard");
}

const UStreamGuardSettings& UStreamGuardSettings::Get()
{
	const UStreamGuardSettings* Settings = GetDefault<UStreamGuardSettings>();
	check(Settings);
	return *Settings;
}
